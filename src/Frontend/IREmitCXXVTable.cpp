//===--- IREmitCXXVTable.cpp - VTable/RTTI Emission -----------*- C++ -*-===//

#include "blocktype/Frontend/IREmitCXX.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IRMangler.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRTypeContext.h"

namespace blocktype {
namespace frontend {

IREmitCXXVTable::IREmitCXXVTable(ASTToIRConverter& C) : Converter_(C) {}

//===----------------------------------------------------------------------===//
// EmitVTable
//===----------------------------------------------------------------------===//

void IREmitCXXVTable::EmitVTable(const CXXRecordDecl* RD) {
  GetOrCreateVTable(RD);
}

//===----------------------------------------------------------------------===//
// GetVTableType
//===----------------------------------------------------------------------===//

ir::IRType* IREmitCXXVTable::GetVTableType(const CXXRecordDecl* RD) {
  return Converter_.getTypeContext().getOpaqueType("vtable");
}

//===----------------------------------------------------------------------===//
// GetVTableIndex
//===----------------------------------------------------------------------===//

uint64_t IREmitCXXVTable::GetVTableIndex(const CXXMethodDecl* MD) {
  if (!MD) return 0;

  auto It = VTableIndexCache_.find(MD);
  if (It != VTableIndexCache_.end()) return It->second;

  auto* RD = MD->getParent();
  if (!RD) return 0;

  // VTable layout: [offset-to-top, RTTI-ptr, virtual-method-1, ...]
  // Index starts at 2 (after offset-to-top and RTTI)
  uint64_t Index = 2;
  for (CXXMethodDecl* M : RD->methods()) {
    if (!M->isVirtual()) continue;
    if (M == MD) {
      VTableIndexCache_[MD] = Index;
      return Index;
    }
    // Virtual destructors take 2 entries (complete + deleting)
    if (llvm::isa<CXXDestructorDecl>(M)) {
      Index += 2;
    } else {
      ++Index;
    }
  }

  return Index;
}

//===----------------------------------------------------------------------===//
// InitializeVTablePtr
//===----------------------------------------------------------------------===//

void IREmitCXXVTable::InitializeVTablePtr(ir::IRValue* Object,
                                           const CXXRecordDecl* RD) {
  if (!Object || !RD) return;

  auto& Builder = Converter_.getBuilder();
  auto& TypeCtx = Converter_.getTypeContext();
  auto& Layout = Converter_.getCxxEmitter()->getLayoutEmitter();

  auto* VTableGV = GetOrCreateVTable(RD);
  if (!VTableGV) return;

  auto* StructTy = Layout.GetOrCreateStructType(RD);
  if (!StructTy) return;

  unsigned VPtrIdx = Layout.GetVPtrIndex(RD);

  // GEP to vptr field: GEP(StructTy, Object, {0, VPtrIdx})
  ir::IRValue* Zero = Builder.getInt32(0);
  ir::IRValue* VPtrIdxVal = Builder.getInt32(VPtrIdx);
  ir::SmallVector<ir::IRValue*, 2> Indices;
  Indices.push_back(Zero);
  Indices.push_back(VPtrIdxVal);
  auto* VPtrPtr = Builder.createGEP(StructTy, Object, Indices, "vptr");

  // Store VTable address into vptr
  auto* VTableRef = new ir::IRConstantGlobalRef(VTableGV);
  Builder.createStore(VTableRef, VPtrPtr);
}

//===----------------------------------------------------------------------===//
// EmitVTableInitialization
//===----------------------------------------------------------------------===//

void IREmitCXXVTable::EmitVTableInitialization(const CXXRecordDecl* RD) {
  // No-op in IR layer. Actual VTable content filling is Phase C.
}

//===----------------------------------------------------------------------===//
// EmitRTTI
//===----------------------------------------------------------------------===//

void IREmitCXXVTable::EmitRTTI(const CXXRecordDecl* RD) {
  if (!RD) return;

  // Check cache
  auto It = RTTICache_.find(RD);
  if (It != RTTICache_.end()) return;

  auto& TypeCtx = Converter_.getTypeContext();
  auto* Module = Converter_.getModule();
  if (!Module) return;

  auto* Mangler = Converter_.getMangler();
  std::string MangledName = Mangler->mangleTypeInfo(RD);

  auto* OpaqueTy = TypeCtx.getOpaqueType("typeinfo");
  auto* RTTIGV = Module->getOrInsertGlobal(MangledName, OpaqueTy);
  if (RTTIGV) {
    RTTICache_[RD] = RTTIGV;
  }
}

//===----------------------------------------------------------------------===//
// EmitCatchTypeInfo
//===----------------------------------------------------------------------===//

void IREmitCXXVTable::EmitCatchTypeInfo(const CXXCatchStmt* CS) {
  if (!CS) return;

  // For catch-all, no typeinfo needed
  if (CS->isCatchAll()) return;

  auto* ExDecl = CS->getExceptionDecl();
  if (!ExDecl) return;

  // The caught type is ExDecl->getType()
  // For now, just emit RTTI placeholder
  // Full implementation in Phase C
}

//===----------------------------------------------------------------------===//
// GetOrCreateVTable
//===----------------------------------------------------------------------===//

ir::IRGlobalVariable* IREmitCXXVTable::GetOrCreateVTable(
    const CXXRecordDecl* RD) {
  if (!RD) return nullptr;

  // Check cache
  auto It = VTableCache_.find(RD);
  if (It != VTableCache_.end()) return It->second;

  auto& TypeCtx = Converter_.getTypeContext();
  auto* Module = Converter_.getModule();
  if (!Module) return nullptr;

  auto* Mangler = Converter_.getMangler();
  std::string MangledName = Mangler->mangleVTable(RD);

  // Create VTable as opaque-type global variable
  auto* OpaqueTy = TypeCtx.getOpaqueType("vtable");
  auto* VTableGV = Module->getOrInsertGlobal(MangledName, OpaqueTy);
  if (VTableGV) {
    VTableCache_[RD] = VTableGV;

    // Also emit RTTI for this class
    EmitRTTI(RD);

    // Mark module as requiring virtual dispatch feature
    Module->addRequiredFeature(ir::IRFeature::VirtualDispatch);
  }

  return VTableGV;
}

} // namespace frontend
} // namespace blocktype
