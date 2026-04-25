//===--- IREmitCXXLayout.cpp - Class Layout Computation -------*- C++ -*-===//

#include "blocktype/Frontend/IREmitCXX.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IRTypeMapper.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

#include "llvm/Support/MathExtras.h"

namespace blocktype {
namespace frontend {

//===----------------------------------------------------------------------===//
// Static helpers
//===----------------------------------------------------------------------===//

bool IREmitCXXLayout::hasVirtualFunctions(const CXXRecordDecl* RD) {
  if (!RD) return false;
  for (CXXMethodDecl* MD : RD->methods()) {
    if (MD->isVirtual()) return true;
  }
  return false;
}

bool IREmitCXXLayout::hasVirtualFunctionsInHierarchy(const CXXRecordDecl* RD) {
  if (!RD) return false;
  if (hasVirtualFunctions(RD)) return true;
  for (const auto& Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto* RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto* BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BaseCXX)) return true;
      }
    }
  }
  return false;
}

IREmitCXXLayout::IREmitCXXLayout(ASTToIRConverter& C) : Converter_(C) {}

//===----------------------------------------------------------------------===//
// ComputeClassLayout
//===----------------------------------------------------------------------===//

ir::IRStructType* IREmitCXXLayout::ComputeClassLayout(const CXXRecordDecl* RD) {
  if (!RD) return nullptr;

  // Check cache
  auto It = StructTypeCache_.find(RD);
  if (It != StructTypeCache_.end()) return It->second;

  auto& TypeCtx = Converter_.getTypeContext();
  auto& Layout = Converter_.getTargetLayout();
  auto& TypeMapper = Converter_.getTypeMapper();

  // Collect field types for IRStructType
  llvm::SmallVector<ir::IRType*, 16> FieldTypes;
  llvm::SmallVector<uint64_t, 16> FieldOffsets;

  uint64_t CurrentOffset = 0;

  // 1. vptr pointer (always first if class has virtual functions)
  bool HasVPtr = hasVirtualFunctionsInHierarchy(RD);
  bool HasVirtualBase = false;
  for (const auto& Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto* RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto* BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BaseCXX)) {
          HasVirtualBase = true;
          break;
        }
      }
    }
  }

  if (HasVPtr && !HasVirtualBase) {
    uint64_t PtrSizeBits = Layout.getPointerSizeInBits();
    uint64_t PtrSize = PtrSizeBits / 8;
    uint64_t PtrAlign = PtrSize; // pointer alignment = pointer size

    CurrentOffset = llvm::alignTo(CurrentOffset, PtrAlign);

    auto* PtrTy = TypeCtx.getPointerType(TypeCtx.getInt8Ty());
    FieldTypes.push_back(PtrTy);
    FieldOffsets.push_back(CurrentOffset);
    VPtrIndexCache_[RD] = static_cast<unsigned>(FieldTypes.size() - 1);

    CurrentOffset += PtrSize;
  }

  // 2. Non-virtual base class subobjects
  for (const auto& Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto* RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto* BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (Base.isVirtual()) continue;

        uint64_t BaseSize = GetClassSize(BaseCXX);

        // Empty Base Optimization
        bool IsEmptyBase = (BaseSize <= 1 && BaseCXX->fields().empty() &&
                            !hasVirtualFunctionsInHierarchy(BaseCXX));

        if (!IsEmptyBase || CurrentOffset == 0) {
          uint64_t BaseAlign = 1;
          if (hasVirtualFunctionsInHierarchy(BaseCXX)) {
            BaseAlign = std::max(BaseAlign, Layout.getPointerSizeInBits() / 8);
          }
          for (FieldDecl* F : BaseCXX->fields()) {
            auto* IRFieldTy = TypeMapper.mapType(F->getType());
            if (IRFieldTy) {
              uint64_t FA = IRFieldTy->getAlignInBits(Layout) / 8;
              BaseAlign = std::max(BaseAlign, FA > 0 ? FA : 1);
            }
          }
          CurrentOffset = llvm::alignTo(CurrentOffset, BaseAlign);
          BaseOffsetCache_[{RD, BaseCXX}] = CurrentOffset;

          if (!IsEmptyBase) {
            // Add base fields
            auto* BaseStructTy = GetOrCreateStructType(BaseCXX);
            if (BaseStructTy) {
              for (unsigned i = 0; i < BaseStructTy->getNumFields(); ++i) {
                FieldTypes.push_back(BaseStructTy->getFieldType(i));
                FieldOffsets.push_back(
                    CurrentOffset +
                    BaseStructTy->getFieldOffset(i, Layout) / 8);
              }
            }
            CurrentOffset += BaseSize;
          }
        } else {
          // Empty base at non-zero offset — EBO
          BaseOffsetCache_[{RD, BaseCXX}] = CurrentOffset;
        }
      }
    }
  }

  // 3. Own fields
  for (FieldDecl* FD : RD->fields()) {
    auto* IRFieldTy = TypeMapper.mapType(FD->getType());
    if (!IRFieldTy) continue;

    uint64_t FieldSize = IRFieldTy->getSizeInBits(Layout) / 8;
    uint64_t FieldAlign = IRFieldTy->getAlignInBits(Layout) / 8;
    FieldAlign = std::max(FieldAlign, (uint64_t)1);

    CurrentOffset = llvm::alignTo(CurrentOffset, FieldAlign);
    FieldTypes.push_back(IRFieldTy);
    FieldOffsets.push_back(CurrentOffset);

    CurrentOffset += FieldSize;
  }

  // 4. Virtual bases (at end) — simplified for now
  for (const auto& Base : RD->bases()) {
    if (!Base.isVirtual()) continue;
    QualType BaseType = Base.getType();
    if (auto* RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto* BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        uint64_t BaseSize = GetClassSize(BaseCXX);
        uint64_t BaseAlign = 1;
        if (hasVirtualFunctionsInHierarchy(BaseCXX)) {
          BaseAlign = std::max(BaseAlign, Layout.getPointerSizeInBits() / 8);
        }
        CurrentOffset = llvm::alignTo(CurrentOffset, BaseAlign);
        BaseOffsetCache_[{RD, BaseCXX}] = CurrentOffset;

        auto* BaseStructTy = GetOrCreateStructType(BaseCXX);
        if (BaseStructTy) {
          for (unsigned i = 0; i < BaseStructTy->getNumFields(); ++i) {
            FieldTypes.push_back(BaseStructTy->getFieldType(i));
            FieldOffsets.push_back(
                CurrentOffset + BaseStructTy->getFieldOffset(i, Layout) / 8);
          }
        }
        CurrentOffset += BaseSize;
      }
    }
  }

  // Ensure minimum size of 1 byte
  if (CurrentOffset == 0) CurrentOffset = 1;

  // Cache class size
  ClassSizeCache_[RD] = CurrentOffset;
  FieldOffsetsCache_[RD] = FieldOffsets;

  // Create IRStructType
  std::string StructName = RD->getName().str();
  if (StructName.empty()) StructName = "__anon_class";

  ir::SmallVector<ir::IRType*, 16> IRFieldTypes;
  for (auto* FT : FieldTypes) IRFieldTypes.push_back(FT);
  auto* StructTy = TypeCtx.getStructType(StructName, std::move(IRFieldTypes));
  StructTypeCache_[RD] = StructTy;

  return StructTy;
}

//===----------------------------------------------------------------------===//
// GetFieldOffset
//===----------------------------------------------------------------------===//

uint64_t IREmitCXXLayout::GetFieldOffset(const FieldDecl* FD) {
  if (!FD) return 0;
  auto* Parent = FD->getParent();
  if (!Parent) return 0;

  // Ensure layout is computed
  ComputeClassLayout(Parent);

  // Find the field index
  unsigned Idx = 0;
  for (const FieldDecl* F : Parent->fields()) {
    if (F == FD) break;
    ++Idx;
  }

  auto It = FieldOffsetsCache_.find(Parent);
  if (It == FieldOffsetsCache_.end()) return 0;

  const auto& Offsets = It->second;

  // Account for vptr and base fields
  unsigned VPtrCount = 0;
  if (hasVirtualFunctionsInHierarchy(Parent)) {
    bool ParentHasVirtualBase = false;
    for (const auto& B : Parent->bases()) {
      QualType BT = B.getType();
      if (auto* RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
        if (auto* BRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          if (hasVirtualFunctionsInHierarchy(BRD)) {
            ParentHasVirtualBase = true;
            break;
          }
        }
      }
    }
    if (!ParentHasVirtualBase)
      VPtrCount = 1;
  }

  unsigned BaseFieldCount = 0;
  for (const auto& Base : Parent->bases()) {
    QualType BaseType = Base.getType();
    if (auto* RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto* BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        auto* BaseST = GetOrCreateStructType(BaseCXX);
        if (BaseST) BaseFieldCount += BaseST->getNumFields();
      }
    }
  }

  unsigned ActualIdx = VPtrCount + BaseFieldCount + Idx;
  if (ActualIdx < Offsets.size()) return Offsets[ActualIdx];
  return 0;
}

//===----------------------------------------------------------------------===//
// GetClassSize
//===----------------------------------------------------------------------===//

uint64_t IREmitCXXLayout::GetClassSize(const CXXRecordDecl* RD) {
  if (!RD) return 0;
  auto It = ClassSizeCache_.find(RD);
  if (It != ClassSizeCache_.end()) return It->second;
  ComputeClassLayout(RD);
  auto It2 = ClassSizeCache_.find(RD);
  if (It2 != ClassSizeCache_.end()) return It2->second;
  return 1;
}

//===----------------------------------------------------------------------===//
// GetBaseOffset
//===----------------------------------------------------------------------===//

uint64_t IREmitCXXLayout::GetBaseOffset(const CXXRecordDecl* Derived,
                                        const CXXRecordDecl* Base) {
  if (!Derived || !Base) return 0;
  if (Derived == Base) return 0;
  auto It = BaseOffsetCache_.find({Derived, Base});
  if (It != BaseOffsetCache_.end()) return It->second;
  ComputeClassLayout(Derived);
  It = BaseOffsetCache_.find({Derived, Base});
  if (It != BaseOffsetCache_.end()) return It->second;
  return 0;
}

//===----------------------------------------------------------------------===//
// GetVirtualBaseOffset
//===----------------------------------------------------------------------===//

uint64_t IREmitCXXLayout::GetVirtualBaseOffset(const CXXRecordDecl* Derived,
                                               const CXXRecordDecl* VBase) {
  // Simplified: return cached offset if available
  return GetBaseOffset(Derived, VBase);
}

//===----------------------------------------------------------------------===//
// GetOrCreateStructType
//===----------------------------------------------------------------------===//

ir::IRStructType* IREmitCXXLayout::GetOrCreateStructType(
    const CXXRecordDecl* RD) {
  if (!RD) return nullptr;
  auto It = StructTypeCache_.find(RD);
  if (It != StructTypeCache_.end()) return It->second;
  return ComputeClassLayout(RD);
}

//===----------------------------------------------------------------------===//
// GetVPtrIndex
//===----------------------------------------------------------------------===//

unsigned IREmitCXXLayout::GetVPtrIndex(const CXXRecordDecl* RD) {
  if (!RD) return 0;
  auto It = VPtrIndexCache_.find(RD);
  if (It != VPtrIndexCache_.end()) return It->second;
  ComputeClassLayout(RD);
  It = VPtrIndexCache_.find(RD);
  if (It != VPtrIndexCache_.end()) return It->second;
  return 0;
}

} // namespace frontend
} // namespace blocktype
