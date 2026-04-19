//===--- CGCXX.cpp - C++ Specific Code Generation --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/CGCXX.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/CodeGen/Mangler.h"
#include "blocktype/CodeGen/TargetInfo.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Casting.h"
#include <string>

using namespace blocktype;

//===----------------------------------------------------------------------===//
// 辅助方法
//===----------------------------------------------------------------------===//

bool CGCXX::hasVirtualFunctions(CXXRecordDecl *RD) {
  if (!RD) return false;
  for (CXXMethodDecl *MD : RD->methods()) {
    if (MD->isVirtual()) return true;
  }
  return false;
}

bool CGCXX::hasVirtualFunctionsInHierarchy(CXXRecordDecl *RD) {
  if (!RD) return false;
  if (hasVirtualFunctions(RD)) return true;
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BaseCXX)) return true;
      }
    }
  }
  return false;
}

//===----------------------------------------------------------------------===//
// 覆盖检测辅助方法（Issue 8）
//===----------------------------------------------------------------------===//

bool CGCXX::isMethodOverride(const CXXMethodDecl *DerivedMD,
                              const CXXMethodDecl *BaseMD) const {
  if (!DerivedMD || !BaseMD) return false;
  if (!DerivedMD->isVirtual() || !BaseMD->isVirtual()) return false;
  if (DerivedMD->getName() != BaseMD->getName()) return false;
  if (DerivedMD->getNumParams() != BaseMD->getNumParams()) return false;
  // cv 限定符必须匹配
  if (DerivedMD->isConst() != BaseMD->isConst()) return false;
  if (DerivedMD->isVolatile() != BaseMD->isVolatile()) return false;
  // ref-qualifier 必须匹配
  if (DerivedMD->getRefQualifier() != BaseMD->getRefQualifier()) return false;
  return true;
}

CXXMethodDecl *CGCXX::findOverride(CXXRecordDecl *RD, CXXMethodDecl *BaseMD) {
  for (CXXMethodDecl *MD : RD->methods()) {
    if (isMethodOverride(MD, BaseMD)) return MD;
  }
  return nullptr;
}

bool CGCXX::methodMatchesInHierarchy(CXXMethodDecl *MD,
                                      CXXRecordDecl *BaseRD) {
  if (!MD || !BaseRD) return false;
  // 先检查 BaseRD 自身的方法
  for (CXXMethodDecl *BMD : BaseRD->methods()) {
    if (isMethodOverride(MD, BMD)) return true;
  }
  // 递归检查 BaseRD 的基类
  for (const auto &Base : BaseRD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (methodMatchesInHierarchy(MD, BaseCXX)) return true;
      }
    }
  }
  return false;
}

bool CGCXX::isMethodInAnyBase(CXXMethodDecl *MD, CXXRecordDecl *RD) {
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (methodMatchesInHierarchy(MD, BaseRD)) return true;
      }
    }
  }
  return false;
}

//===----------------------------------------------------------------------===//
// 虚析构函数辅助（Issue 9）
//===----------------------------------------------------------------------===//

bool CGCXX::isVirtualDestructor(CXXMethodDecl *MD) {
  return MD && MD->isVirtual() && llvm::isa<CXXDestructorDecl>(MD);
}

unsigned CGCXX::vtableEntryCount(CXXMethodDecl *MD) {
  // 虚析构函数在 vtable 中占 2 个条目：D1 (complete) + D0 (deleting)
  return isVirtualDestructor(MD) ? 2 : 1;
}

llvm::Function *CGCXX::EmitDeletingDestructor(CXXRecordDecl *RD) {
  if (!RD) return nullptr;
  auto It = DeletingDtorCache.find(RD);
  if (It != DeletingDtorCache.end()) return It->second;

  // 查找 D1 析构函数
  llvm::Function *D1Fn = GetDestructor(RD);
  if (!D1Fn) {
    // 类没有显式析构函数 — D0 不需要
    return nullptr;
  }

  // 生成 D0 mangled name
  std::string D0Name = CGM.getMangler().getMangledDtorName(RD, DtorVariant::Deleting);
  auto *D0Fn = CGM.getModule()->getFunction(D0Name);
  if (D0Fn) {
    DeletingDtorCache[RD] = D0Fn;
    return D0Fn;
  }

  auto &Ctx = CGM.getLLVMContext();
  auto *VoidTy = llvm::Type::getVoidTy(Ctx);
  auto *PtrTy = llvm::PointerType::get(Ctx, 0);
  auto *SizeTy = llvm::Type::getInt64Ty(Ctx);

  auto *FnTy = llvm::FunctionType::get(VoidTy, {PtrTy}, false);
  D0Fn = llvm::Function::Create(FnTy, llvm::GlobalValue::ExternalLinkage,
                                 D0Name, CGM.getModule());

  // 生成 D0 函数体：call D1(this) + call operator delete(this, sizeof(Class))
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", D0Fn);
  llvm::IRBuilder<> Builder(Entry);
  llvm::Value *This = &*D0Fn->arg_begin();

  // 1. 调用 D1 (complete destructor)
  Builder.CreateCall(D1Fn, {This});

  // 2. 调用 operator delete(this, sizeof(Class))
  uint64_t ClassSize = GetClassSize(RD);
  auto *OpDeleteTy = llvm::FunctionType::get(VoidTy, {PtrTy, SizeTy}, false);
  llvm::FunctionCallee OpDelete =
      CGM.getModule()->getOrInsertFunction("_ZdlPvm", OpDeleteTy);
  Builder.CreateCall(OpDelete,
                     {This, llvm::ConstantInt::get(SizeTy, ClassSize)});

  Builder.CreateRetVoid();

  DeletingDtorCache[RD] = D0Fn;
  return D0Fn;
}

//===----------------------------------------------------------------------===//
// 多重继承 vtable 辅助（Issue 10）
//===----------------------------------------------------------------------===//

CXXRecordDecl *CGCXX::getPrimaryBase(CXXRecordDecl *RD) {
  if (!RD) return nullptr;
  // 主基类 = 第一个具有虚函数的直接基类
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BaseRD)) return BaseRD;
      }
    }
  }
  return nullptr;
}

unsigned CGCXX::getBaseFieldCount(CXXRecordDecl *RD) {
  if (!RD) return 0;
  unsigned Count = 0;

  // 基类自身的 vptr
  bool HasVFunc = hasVirtualFunctions(RD);
  bool HasVirtualBase = false;
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BaseRD)) {
          HasVirtualBase = true;
          break;
        }
      }
    }
  }
  if (HasVFunc && !HasVirtualBase) ++Count; // vptr

  // 递归基类字段
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        Count += getBaseFieldCount(BaseRD);
      }
    }
  }

  // 自身字段
  Count += RD->fields().size();
  return Count;
}

unsigned CGCXX::getBaseVPtrStructIndex(CXXRecordDecl *Derived,
                                        CXXRecordDecl *Base) {
  auto Key = std::pair<const CXXRecordDecl *, const CXXRecordDecl *>(
      Derived, Base);
  auto It = BaseVPtrIndexCache.find(Key);
  if (It != BaseVPtrIndexCache.end()) return It->second;

  // 计算主 vptr 是否在 Derived 层面
  bool OwnVPtr = hasVirtualFunctionsInHierarchy(Derived);
  bool HasVirtualBase = false;
  for (const auto &B : Derived->bases()) {
    QualType BT = B.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BRD)) {
          HasVirtualBase = true;
          break;
        }
      }
    }
  }

  unsigned Idx = 0;
  if (OwnVPtr && !HasVirtualBase) ++Idx; // 跳过自身 vptr

  // 遍历基类，找到 Base 的位置
  for (const auto &B : Derived->bases()) {
    QualType BT = B.getType();
    auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr());
    if (!RT) continue;
    auto *BRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
    if (!BRD) continue;

    if (hasVirtualFunctionsInHierarchy(BRD)) {
      // collectBaseClassFields(BRD) 的第一个元素是 BRD 的 vptr
      unsigned Result = Idx;
      BaseVPtrIndexCache[Key] = Result;
      if (BRD == Base) return Result;
    }
    Idx += getBaseFieldCount(BRD);
  }

  BaseVPtrIndexCache[Key] = 0;
  return 0;
}

unsigned CGCXX::getPrimaryVPtrIndex(CXXRecordDecl *RD) {
  if (!RD) return 0;
  // 如果有自身 vptr → index 0
  bool OwnVPtr = hasVirtualFunctionsInHierarchy(RD);
  bool HasVirtualBase = false;
  for (const auto &B : RD->bases()) {
    QualType BT = B.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BRD)) { HasVirtualBase = true; break; }
      }
    }
  }
  if (OwnVPtr && !HasVirtualBase) return 0; // 自身 vptr

  // 主基类的 vptr 也在 index 0
  CXXRecordDecl *Primary = getPrimaryBase(RD);
  if (Primary) return getBaseVPtrStructIndex(RD, Primary);
  return 0;
}

CXXRecordDecl *CGCXX::findOwningBaseForMethod(CXXRecordDecl *RD,
                                                CXXMethodDecl *MD) {
  if (!RD || !MD) return nullptr;
  // 遍历 RD 的直接基类，看 MD 是否覆盖了某个基类的方法
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (methodMatchesInHierarchy(MD, BaseRD)) return BaseRD;
      }
    }
  }
  return nullptr; // MD 是 RD 自身新增的虚函数
}

unsigned CGCXX::computeIndexInBaseGroup(CXXMethodDecl *MD,
                                         CXXRecordDecl *BaseRD) {
  unsigned Idx = 2; // 跳过 offset-to-top + RTTI

  // 递归基类的基类的虚函数
  for (const auto &Base : BaseRD->bases()) {
    QualType BT = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BBD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        for (CXXMethodDecl *BMD : BBD->methods()) {
          if (!BMD->isVirtual()) continue;
          if (MD->getName() == BMD->getName()) return Idx;
          Idx += vtableEntryCount(BMD);
        }
      }
    }
  }

  // BaseRD 自身的虚函数
  for (CXXMethodDecl *BMD : BaseRD->methods()) {
    if (!BMD->isVirtual()) continue;
    if (MD->getName() == BMD->getName()) return Idx;
    Idx += vtableEntryCount(BMD);
  }

  return 2;
}

unsigned CGCXX::computeVTableGroupOffset(CXXRecordDecl *RD,
                                          CXXRecordDecl *Base) {
  auto Key = std::pair<const CXXRecordDecl *, const CXXRecordDecl *>(
      RD, Base);
  auto It = VTableGroupOffsetCache.find(Key);
  if (It != VTableGroupOffsetCache.end()) return It->second;

  CXXRecordDecl *Primary = getPrimaryBase(RD);

  // 如果 Base 就是主基类（或 Base 为 nullptr 表示主组），偏移 = 0
  if (Base == Primary || Base == nullptr) {
    VTableGroupOffsetCache[Key] = 0;
    return 0;
  }

  // 主组大小 = 2(ott+RTTI) + 主基类虚函数条目 + 自身新增虚函数条目
  unsigned Offset = 2; // ott + RTTI
  if (Primary) {
    for (CXXMethodDecl *MD : Primary->methods()) {
      if (MD->isVirtual()) {
        // 检查是否有覆盖
        CXXMethodDecl *Ov = findOverride(RD, MD);
        (void)Ov; // 覆盖不影响条目数量
        Offset += vtableEntryCount(MD);
      }
    }
  }
  // 自身新增的虚函数也在主组
  for (CXXMethodDecl *MD : RD->methods()) {
    if (!MD->isVirtual()) continue;
    if (!isMethodInAnyBase(MD, RD)) {
      Offset += vtableEntryCount(MD);
    }
  }

  // 遍历后续基类，计算到 Base 之前的组大小
  bool PastPrimary = false;
  for (const auto &B : RD->bases()) {
    QualType BT = B.getType();
    auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr());
    if (!RT) continue;
    auto *BRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
    if (!BRD) continue;

    if (!hasVirtualFunctionsInHierarchy(BRD)) continue;

    if (BRD == Primary) { PastPrimary = true; continue; }
    if (!PastPrimary) continue;

    if (BRD == Base) {
      VTableGroupOffsetCache[Key] = Offset;
      return Offset;
    }

    // 累加这个基类的组大小
    Offset += 2; // ott + RTTI
    for (CXXMethodDecl *MD : BRD->methods()) {
      if (MD->isVirtual()) Offset += vtableEntryCount(MD);
    }
  }

  VTableGroupOffsetCache[Key] = Offset;
  return Offset;
}

//===----------------------------------------------------------------------===//
// 类布局
//===----------------------------------------------------------------------===//

llvm::SmallVector<uint64_t, 16> CGCXX::ComputeClassLayout(CXXRecordDecl *RD) {
  llvm::SmallVector<uint64_t, 16> FieldOffsets;
  if (!RD) return FieldOffsets;

  uint64_t CurrentOffset = 0;

  // 1. vptr 指针（始终在最前面，索引 0，与 GetRecordType 一致）
  // Clang 的 Itanium ABI：vptr 始终在对象起始位置
  bool HasVPtr = hasVirtualFunctionsInHierarchy(RD);
  bool HasVirtualBase = false;
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BaseCXX)) {
          HasVirtualBase = true;
          break;
        }
      }
    }
  }

  // 如果自身有虚函数且没有带虚函数的基类，vptr 在索引 0
  if (HasVPtr && !HasVirtualBase) {
    uint64_t PtrSize = CGM.getTarget().getPointerSize();
    uint64_t PtrAlign = CGM.getTarget().getPointerAlign();
    CurrentOffset = llvm::alignTo(CurrentOffset, PtrAlign);
    CurrentOffset += PtrSize;
  }

  // 2. 非虚基类子对象（按声明顺序排列）
  // 虚基类将在后面单独处理（放在末尾）
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        // 跳过虚基类，稍后处理
        if (Base.isVirtual()) continue;
        
        uint64_t BaseSize = GetClassSize(BaseCXX);
        
        // Empty Base Optimization (EBO): 空基类不占用空间
        // 但如果当前偏移为0且还没有放置任何内容，仍需保留1字节以确保地址唯一性
        bool IsEmptyBase = (BaseSize == 1 && BaseCXX->fields().empty() && 
                           !hasVirtualFunctionsInHierarchy(BaseCXX));
        
        if (!IsEmptyBase || CurrentOffset == 0) {
          uint64_t BaseAlign = 1;
          if (hasVirtualFunctionsInHierarchy(BaseCXX)) {
            BaseAlign = std::max(BaseAlign, CGM.getTarget().getPointerAlign());
          }
          for (FieldDecl *F : BaseCXX->fields()) {
            BaseAlign = std::max(BaseAlign,
                                 CGM.getTarget().getTypeAlign(F->getType()));
          }
          CurrentOffset = llvm::alignTo(CurrentOffset, BaseAlign);
          BaseOffsetCache[{RD, BaseCXX}] = CurrentOffset;
          if (!IsEmptyBase) {
            CurrentOffset += BaseSize;
          }
        } else {
          // 空基类且当前已有内容，复用当前偏移（EBO）
          BaseOffsetCache[{RD, BaseCXX}] = CurrentOffset;
        }
      }
    }
  }

  // 3. 按声明顺序排列非静态数据成员
  for (FieldDecl *FD : RD->fields()) {
    uint64_t FieldSize = CGM.getTarget().getTypeSize(FD->getType());
    uint64_t FieldAlign = CGM.getTarget().getTypeAlign(FD->getType());
    CurrentOffset = llvm::alignTo(CurrentOffset, FieldAlign);
    FieldOffsets.push_back(CurrentOffset);
    FieldOffsetCache[FD] = CurrentOffset;
    CurrentOffset += FieldSize;
  }

  // 4. 虚基类子对象（放在末尾，符合 Itanium ABI）
  // 虚基类的偏移量将在 vtable 中记录，运行时通过 vptr 查找
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        // 只处理虚基类
        if (!Base.isVirtual()) continue;
        
        uint64_t BaseSize = GetClassSize(BaseCXX);
        
        // Empty Base Optimization (EBO): 空基类不占用空间
        bool IsEmptyBase = (BaseSize == 1 && BaseCXX->fields().empty() && 
                           !hasVirtualFunctionsInHierarchy(BaseCXX));
        
        if (!IsEmptyBase) {
          uint64_t BaseAlign = 1;
          if (hasVirtualFunctionsInHierarchy(BaseCXX)) {
            BaseAlign = std::max(BaseAlign, CGM.getTarget().getPointerAlign());
          }
          for (FieldDecl *F : BaseCXX->fields()) {
            BaseAlign = std::max(BaseAlign,
                                 CGM.getTarget().getTypeAlign(F->getType()));
          }
          CurrentOffset = llvm::alignTo(CurrentOffset, BaseAlign);
          BaseOffsetCache[{RD, BaseCXX}] = CurrentOffset;
          CurrentOffset += BaseSize;
        } else {
          // 空基类，复用当前偏移（EBO）
          BaseOffsetCache[{RD, BaseCXX}] = CurrentOffset;
        }
      }
    }
  }

  // 缓存类大小（含尾部填充）
  uint64_t OverallAlign = 1;
  if (HasVPtr) {
    OverallAlign = std::max(OverallAlign, CGM.getTarget().getPointerAlign());
  }
  for (FieldDecl *F : RD->fields()) {
    OverallAlign =
        std::max(OverallAlign, CGM.getTarget().getTypeAlign(F->getType()));
  }
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        uint64_t BA = 1;
        if (hasVirtualFunctionsInHierarchy(BaseCXX)) {
          BA = std::max(BA, CGM.getTarget().getPointerAlign());
        }
        for (FieldDecl *F : BaseCXX->fields()) {
          BA = std::max(BA, CGM.getTarget().getTypeAlign(F->getType()));
        }
        OverallAlign = std::max(OverallAlign, BA);
      }
    }
  }

  uint64_t TotalSize = llvm::alignTo(CurrentOffset, OverallAlign);
  ClassSizeCache[RD] = std::max(TotalSize, (uint64_t)1);

  return FieldOffsets;
}

uint64_t CGCXX::GetFieldOffset(FieldDecl *FD) {
  if (!FD) return 0;
  auto It = FieldOffsetCache.find(FD);
  if (It != FieldOffsetCache.end()) return It->second;
  return 0;
}

uint64_t CGCXX::GetClassSize(CXXRecordDecl *RD) {
  if (!RD) return 0;
  auto It = ClassSizeCache.find(RD);
  if (It != ClassSizeCache.end()) return It->second;
  ComputeClassLayout(RD);
  auto It2 = ClassSizeCache.find(RD);
  if (It2 != ClassSizeCache.end()) return It2->second;
  return 1;
}

uint64_t CGCXX::GetBaseOffset(CXXRecordDecl *Derived, CXXRecordDecl *Base) {
  if (!Derived || !Base) return 0;
  if (Derived == Base) return 0;
  auto It = BaseOffsetCache.find({Derived, Base});
  if (It != BaseOffsetCache.end()) return It->second;
  ComputeClassLayout(Derived);
  It = BaseOffsetCache.find({Derived, Base});
  if (It != BaseOffsetCache.end()) return It->second;
  return 0;
}

//===----------------------------------------------------------------------===//
// 构造函数
//===----------------------------------------------------------------------===//

void CGCXX::EmitConstructor(CXXConstructorDecl *Ctor, llvm::Function *Fn) {
  if (!Fn || !Ctor) return;
  if (!Fn->empty()) return;

  CXXRecordDecl *Class = Ctor->getParent();
  if (!Class) {
    auto *Entry =
        llvm::BasicBlock::Create(CGM.getLLVMContext(), "entry", Fn);
    llvm::IRBuilder<> Builder(Entry);
    Builder.CreateRetVoid();
    return;
  }

  // 使用 CodeGenFunction 生成函数体
  CodeGenFunction CGF(CGM);
  llvm::BasicBlock *EntryBB =
      llvm::BasicBlock::Create(CGM.getLLVMContext(), "entry", Fn);
  CGF.getBuilder().SetInsertPoint(EntryBB);
  CGF.setCurrentFunction(Fn);
  
  // 重要：创建 AllocaInsertPt，确保 CreateAlloca 可以正常工作
  // 这是 EmitFunctionBody 中的标准做法，但在 EmitConstructor 中需要手动设置
  CGF.setAllocaInsertPoint(CGF.getBuilder().CreateAlloca(
      llvm::Type::getInt32Ty(CGM.getLLVMContext()), nullptr, "alloca.point"));

  // this 指针是第一个参数
  llvm::Value *This = &*Fn->arg_begin();
  CGF.setThisPointer(This);

  // 为函数参数创建 alloca（跳过 this）
  unsigned ArgIdx = 0;
  for (auto &Arg : Fn->args()) {
    if (ArgIdx == 0) {
      ++ArgIdx;
      continue;
    }
    if (ArgIdx - 1 < Ctor->getNumParams()) {
      ParmVarDecl *PVD = Ctor->getParamDecl(ArgIdx - 1);
      llvm::AllocaInst *Alloca = CGF.CreateAlloca(PVD->getType(), PVD->getName());
      CGF.getBuilder().CreateStore(&Arg, Alloca);
      CGF.setLocalDecl(PVD, Alloca);
    }
    ++ArgIdx;
  }

  bool NeedsVPtr = hasVirtualFunctionsInHierarchy(Class);

  // === Phase 0.5: 检查委托构造函数 ===
  // 委托构造函数（ctor() : other_ctor() {}）将整个初始化委托给另一个构造函数，
  // 因此不需要基类/成员初始化——被委托的构造函数会处理一切。
  bool IsDelegating = false;
  for (CXXCtorInitializer *Init : Ctor->initializers()) {
    if (Init->isDelegatingInitializer()) {
      IsDelegating = true;
      // 查找匹配的被委托构造函数（同类、按参数数量匹配）
      unsigned NumArgs = Init->getArguments().size();
      for (CXXMethodDecl *MD : Class->methods()) {
        if (auto *DelegatedCtor = llvm::dyn_cast<CXXConstructorDecl>(MD)) {
          if (DelegatedCtor != Ctor && DelegatedCtor->getNumParams() == NumArgs) {
            llvm::Function *DelegatedFn = CGM.GetOrCreateFunctionDecl(DelegatedCtor);
            if (DelegatedFn) {
              llvm::SmallVector<llvm::Value *, 4> Args;
              Args.push_back(This);
              for (Expr *Arg : Init->getArguments()) {
                llvm::Value *ArgVal = CGF.EmitExpr(Arg);
                if (ArgVal) Args.push_back(ArgVal);
              }
              CGF.getBuilder().CreateCall(DelegatedFn, Args, "delegated.ctor");
              break;
            }
          }
        }
      }
      break; // 委托构造函数只能有一个委托初始化器
    }
  }

  // 委托构造函数跳过基类/成员初始化（被委托的构造函数已处理）
  if (!IsDelegating) {
  // === Phase 1 + Phase 2 交织: 基类初始化 + 每个基类后更新 vptr ===
  llvm::SmallVector<const CXXRecordDecl *, 4> InitializedBases;

  // 检查是否有虚基类，如果有，需要使用 VTT
  bool HasVirtualBase = false;
  for (const auto &BaseSpec : Class->bases()) {
    if (BaseSpec.isVirtual()) {
      HasVirtualBase = true;
      break;
    }
  }

  // 如果有虚基类，从 VTT 加载构造时的 vtable
  llvm::Value *ConstructionVTable = nullptr;
  if (HasVirtualBase) {
    llvm::GlobalVariable *VTT = EmitVTT(Class);
    if (VTT) {
      auto &Ctx = CGM.getLLVMContext();
      // VTT 布局: [primary vtable ptr, construction vtables...]
      // 对于最派生类，使用 VTT[0]（主 vtable）
      // 对于基类子对象构造，使用对应的 construction vtable
      // 当前简化：总是使用 VTT[0]
      llvm::Value *VTTPtr = CGF.getBuilder().CreateInBoundsGEP(
          VTT->getValueType(), VTT,
          {llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), 0),
           llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 0)},
          "vtt.ptr");
      ConstructionVTable = CGF.getBuilder().CreateLoad(
          llvm::PointerType::get(Ctx, 0), VTTPtr, "construction.vtable");
    }
  }

  // 收集初始化列表中的基类信息（按类型匹配）
  for (CXXCtorInitializer *Init : Ctor->initializers()) {
    if (Init->isBaseInitializer()) {
      QualType InitBaseType = Init->getBaseType();
      for (const auto &BaseSpec : Class->bases()) {
        QualType BT = BaseSpec.getType();
        if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
          if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
            bool Match = false;
            // 优先使用类型匹配（P1-3 修复）
            if (!InitBaseType.isNull()) {
              if (auto *InitRT = llvm::dyn_cast<RecordType>(InitBaseType.getTypePtr())) {
                if (auto *InitRD = llvm::dyn_cast<CXXRecordDecl>(InitRT->getDecl())) {
                  Match = (InitRD == BaseRD);
                }
              }
            } else {
              // 退化为名称匹配
              Match = (BaseRD->getName() == Init->getMemberName());
            }
            if (Match) {
              InitializedBases.push_back(BaseRD);

              llvm::Value *BasePtr = EmitCastToBase(CGF, Class, This, BaseRD);

              if (!Init->getArguments().empty()) {
                // 有参数：调用基类构造函数
                llvm::SmallVector<llvm::Value *, 4> Args;
                Args.push_back(BasePtr);
                for (Expr *Arg : Init->getArguments()) {
                  llvm::Value *ArgVal = CGF.EmitExpr(Arg);
                  if (ArgVal) Args.push_back(ArgVal);
                }
                for (CXXMethodDecl *MD : BaseRD->methods()) {
                  if (auto *CtorDecl =
                          llvm::dyn_cast<CXXConstructorDecl>(MD)) {
                    if (CtorDecl->getNumParams() ==
                        Init->getArguments().size()) {
                      llvm::Function *CtorFn =
                          CGM.GetOrCreateFunctionDecl(CtorDecl);
                      if (CtorFn) {
                        CGF.getBuilder().CreateCall(CtorFn, Args, "base.ctor");
                        break;
                      }
                    }
                  }
                }
              } else {
                // 默认构造：零初始化
                llvm::StructType *BaseTy = CGM.getTypes().GetRecordType(BaseRD);
                llvm::Value *TypedPtr = CGF.getBuilder().CreateBitCast(
                    BasePtr, llvm::PointerType::get(BaseTy, 0), "base.ptr");
                CGF.getBuilder().CreateStore(
                    llvm::Constant::getNullValue(BaseTy), TypedPtr);
              }

              // P1-4 修复：每个基类初始化后立即更新 vptr
              if (NeedsVPtr) {
                InitializeVTablePtr(CGF, This, Class, ConstructionVTable);
              }

              break;
            }
          }
        }
      }
    }
  }

  // 为没有在初始化列表中出现的基类执行默认初始化
  for (const auto &BaseSpec : Class->bases()) {
    QualType BT = BaseSpec.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
      if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        bool AlreadyInit = false;
        for (auto *IB : InitializedBases) {
          if (IB == BaseRD) {
            AlreadyInit = true;
            break;
          }
        }
        if (!AlreadyInit) {
          llvm::Value *BasePtr = EmitCastToBase(CGF, Class, This, BaseRD);
          llvm::StructType *BaseTy = CGM.getTypes().GetRecordType(BaseRD);
          llvm::Value *TypedPtr = CGF.getBuilder().CreateBitCast(
              BasePtr, llvm::PointerType::get(BaseTy, 0), "base.ptr");
          CGF.getBuilder().CreateStore(
              llvm::Constant::getNullValue(BaseTy), TypedPtr);

          // P1-4 修复：每个基类初始化后立即更新 vptr
          if (NeedsVPtr) {
            InitializeVTablePtr(CGF, This, Class, ConstructionVTable);
          }
        }
      }
    }
  }

  // === Phase 3: 成员初始化 ===
  llvm::SmallVector<llvm::StringRef, 8> InitializedMembers;
  for (CXXCtorInitializer *Init : Ctor->initializers()) {
    if (Init->isMemberInitializer()) {
      InitializedMembers.push_back(Init->getMemberName());

      FieldDecl *Field = nullptr;
      for (FieldDecl *FD : Class->fields()) {
        if (FD->getName() == Init->getMemberName()) {
          Field = FD;
          break;
        }
      }
      if (Field) {
        EmitMemberInitializer(CGF, Class, This, Field, Init->getArguments());
      }
    }
  }

  // 默认初始化未出现在初始化列表中的成员
  // 优先使用 in-class initializer（如 int x = 42;），否则零初始化
  for (FieldDecl *FD : Class->fields()) {
    bool AlreadyInit = false;
    for (auto N : InitializedMembers) {
      if (N == FD->getName()) {
        AlreadyInit = true;
        break;
      }
    }
    if (!AlreadyInit) {
      if (FD->hasInClassInitializer()) {
        // 使用 in-class initializer 初始化成员
        llvm::SmallVector<Expr *, 1> InClassArgs;
        InClassArgs.push_back(FD->getInClassInitializer());
        EmitMemberInitializer(CGF, Class, This, FD, InClassArgs);
      } else {
        // 零初始化
        llvm::StructType *StructTy = CGM.getTypes().GetRecordType(Class);
        unsigned FieldIdx = CGM.getTypes().GetFieldIndex(FD);
        llvm::Value *FieldPtr = CGF.getBuilder().CreateStructGEP(
            StructTy, This, FieldIdx, FD->getName());
        llvm::Type *FieldLLVMTy = CGM.getTypes().ConvertType(FD->getType());
        CGF.getBuilder().CreateStore(
            llvm::Constant::getNullValue(FieldLLVMTy), FieldPtr);
      }
    }
  }
  } // !IsDelegating

  // === Phase 4: 构造函数体 ===
  if (Stmt *Body = Ctor->getBody()) {
    CGF.EmitStmt(Body);
  }

  if (CGF.haveInsertPoint()) {
    CGF.getBuilder().CreateRetVoid();
  }
}

//===----------------------------------------------------------------------===//
// 析构函数
//===----------------------------------------------------------------------===//

llvm::Function *CGCXX::GetDestructor(CXXRecordDecl *RD) {
  if (!RD || !RD->hasDestructor())
    return nullptr;

  for (CXXMethodDecl *MD : RD->methods()) {
    if (auto *Dtor = llvm::dyn_cast<CXXDestructorDecl>(MD)) {
      return CGM.GetOrCreateFunctionDecl(Dtor);
    }
  }
  return nullptr;
}

void CGCXX::EmitDestructorCall(CodeGenFunction &CGF, CXXRecordDecl *RD,
                                 llvm::Value *Ptr) {
  llvm::Function *DtorFn = GetDestructor(RD);
  if (DtorFn && Ptr) {
    // 析构函数标记 nounwind（C++ 析构函数不应抛异常）
    CGF.EmitNounwindCall(DtorFn, {Ptr}, "dtor.call");
  }
}

void CGCXX::EmitDestructor(CXXDestructorDecl *Dtor, llvm::Function *Fn) {
  if (!Fn || !Dtor) return;
  if (!Fn->empty()) return;

  CXXRecordDecl *Class = Dtor->getParent();
  if (!Class) {
    auto *Entry =
        llvm::BasicBlock::Create(CGM.getLLVMContext(), "entry", Fn);
    llvm::IRBuilder<> Builder(Entry);
    Builder.CreateRetVoid();
    return;
  }

  CodeGenFunction CGF(CGM);
  llvm::BasicBlock *EntryBB =
      llvm::BasicBlock::Create(CGM.getLLVMContext(), "entry", Fn);
  CGF.getBuilder().SetInsertPoint(EntryBB);
  CGF.setCurrentFunction(Fn);

  llvm::Value *This = &*Fn->arg_begin();
  CGF.setThisPointer(This);

  // Phase 1: 析构函数体
  if (Stmt *Body = Dtor->getBody()) {
    CGF.EmitStmt(Body);
  }

  // Phase 2 & 3: 成员析构 + 基类析构
  EmitDestructorBody(CGF, Class, This);

  if (CGF.haveInsertPoint()) {
    CGF.getBuilder().CreateRetVoid();
  }
}

//===----------------------------------------------------------------------===//
// 虚函数表
//===----------------------------------------------------------------------===//

llvm::GlobalVariable *CGCXX::EmitVTable(CXXRecordDecl *RD) {
  if (!RD) return nullptr;

  auto It = VTables.find(RD);
  if (It != VTables.end()) return It->second;

  llvm::SmallVector<llvm::Constant *, 32> VTableEntries;
  auto *PtrTy = llvm::PointerType::get(CGM.getLLVMContext(), 0);

  // === 辅助 lambda：添加一个虚函数条目（含 D0/D1 处理） ===
  auto addVfnEntry = [&](CXXMethodDecl *MD, CXXRecordDecl *OverridingClass,
                          CXXRecordDecl *BaseGroup = nullptr) {
    // 检查派生类是否覆盖
    CXXMethodDecl *Effective = MD;
    if (OverridingClass) {
      CXXMethodDecl *Ov = findOverride(OverridingClass, MD);
      if (Ov) Effective = Ov;
    }

    // 如果是在非主基类组中，且方法被覆盖，需要生成 thunk
    llvm::Function *Fn = nullptr;
    if (BaseGroup && BaseGroup != getPrimaryBase(RD) && OverridingClass) {
      // 计算 this 指针调整量
      int64_t ThisAdjust = -static_cast<int64_t>(GetBaseOffset(OverridingClass, BaseGroup));
      if (ThisAdjust != 0) {
        // 生成 thunk
        Fn = EmitThunk(Effective, BaseGroup, ThisAdjust);
      }
    }
    
    // 如果没有生成 thunk，使用原函数
    if (!Fn) {
      Fn = CGM.GetOrCreateFunctionDecl(Effective);
    }

    VTableEntries.push_back(
        Fn ? llvm::cast<llvm::Constant>(Fn)
           : llvm::ConstantPointerNull::get(PtrTy));

    // D0 (deleting destructor) — 仅虚析构函数
    if (isVirtualDestructor(MD)) {
      llvm::Function *D0Fn = EmitDeletingDestructor(
          OverridingClass ? OverridingClass : Effective->getParent());
      VTableEntries.push_back(
          D0Fn ? llvm::cast<llvm::Constant>(D0Fn)
                : llvm::ConstantPointerNull::get(PtrTy));
    }
  };

  // === Group 0: 主 vtable 组 ===
  // offset-to-top (0 for primary vtable)
  VTableEntries.push_back(llvm::ConstantExpr::getIntToPtr(
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()), 0),
      PtrTy));

  // RTTI 指针
  llvm::GlobalVariable *TypeInfo = EmitTypeInfo(RD);
  if (TypeInfo) {
    VTableEntries.push_back(
        llvm::ConstantExpr::getBitCast(TypeInfo, PtrTy));
  } else {
    VTableEntries.push_back(llvm::ConstantPointerNull::get(PtrTy));
  }

  // 虚基类偏移表（在 RTTI 之后，虚函数之前）
  // 格式：每个虚基类一个条目 [offset-from-this-to-vbase]
  // 如果没有虚基类，跳过此部分
  for (const auto &Base : RD->bases()) {
    if (!Base.isVirtual()) continue;
    
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        // 计算从 this 到虚基类的偏移
        uint64_t VBaseOffset = GetBaseOffset(RD, BaseCXX);
        VTableEntries.push_back(llvm::ConstantExpr::getIntToPtr(
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()),
                                   VBaseOffset),
            PtrTy));
      }
    }
  }

  // 主基类的虚函数（含覆盖检测）
  CXXRecordDecl *Primary = getPrimaryBase(RD);
  if (Primary) {
    for (CXXMethodDecl *MD : Primary->methods()) {
      if (!MD->isVirtual()) continue;
      addVfnEntry(MD, RD, Primary);
    }
  }

  // 自身新增的虚函数（不在任何基类中的）
  for (CXXMethodDecl *MD : RD->methods()) {
    if (!MD->isVirtual()) continue;
    if (isMethodInAnyBase(MD, RD)) continue;
    // 自身新增的方法，如果自身是虚析构函数也需要 D0
    if (isVirtualDestructor(MD)) {
      addVfnEntry(MD, nullptr);
    } else {
      llvm::Function *Fn = CGM.GetOrCreateFunctionDecl(MD);
      VTableEntries.push_back(
          Fn ? llvm::cast<llvm::Constant>(Fn)
             : llvm::ConstantPointerNull::get(PtrTy));
    }
  }

  // === 次要 vtable 组：其他有虚函数的基类 ===
  bool PastPrimary = false;
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr());
    if (!RT) continue;
    auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
    if (!BaseRD) continue;
    if (!hasVirtualFunctionsInHierarchy(BaseRD)) continue;

    if (BaseRD == Primary) { PastPrimary = true; continue; }
    if (!PastPrimary && Primary) continue;

    // 计算此基类在 Derived 中的偏移量（用于 offset-to-top，取负值）
    int64_t BaseOffset = -static_cast<int64_t>(GetBaseOffset(RD, BaseRD));
    VTableEntries.push_back(llvm::ConstantExpr::getIntToPtr(
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()),
                               BaseOffset, true),
        PtrTy));

    // RTTI 指针
    if (TypeInfo) {
      VTableEntries.push_back(
          llvm::ConstantExpr::getBitCast(TypeInfo, PtrTy));
    } else {
      VTableEntries.push_back(llvm::ConstantPointerNull::get(PtrTy));
    }

    // 此基类的虚函数（含覆盖检测）
    for (CXXMethodDecl *MD : BaseRD->methods()) {
      if (!MD->isVirtual()) continue;
      addVfnEntry(MD, RD, BaseRD);
    }
  }

  // 创建 VTable 全局变量
  auto *VTableTy =
      llvm::ArrayType::get(PtrTy, VTableEntries.size());
  auto *VTableInit = llvm::ConstantArray::get(VTableTy, VTableEntries);

  auto *GV = new llvm::GlobalVariable(
      *CGM.getModule(), VTableTy, true, llvm::GlobalValue::ExternalLinkage,
      VTableInit, CGM.getMangler().getVTableName(RD));

  VTables[RD] = GV;

  // 预计算并缓存组偏移
  computeVTableGroupOffset(RD, Primary);
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr());
    if (!RT) continue;
    auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
    if (!BaseRD) continue;
    if (hasVirtualFunctionsInHierarchy(BaseRD)) {
      computeVTableGroupOffset(RD, BaseRD);
    }
  }

  return GV;
}

llvm::ArrayType *CGCXX::GetVTableType(CXXRecordDecl *RD) {
  if (!RD) return nullptr;
  unsigned NumEntries = 0;

  // === Group 0: 主 vtable 组 ===
  NumEntries += 2; // offset-to-top + RTTI

  // 主基类的虚函数
  CXXRecordDecl *Primary = getPrimaryBase(RD);
  if (Primary) {
    for (CXXMethodDecl *MD : Primary->methods()) {
      if (MD->isVirtual()) NumEntries += vtableEntryCount(MD);
    }
  }

  // 自身新增的虚函数
  for (CXXMethodDecl *MD : RD->methods()) {
    if (!MD->isVirtual()) continue;
    if (isMethodInAnyBase(MD, RD)) continue;
    NumEntries += vtableEntryCount(MD);
  }

  // === 次要 vtable 组 ===
  bool PastPrimary = false;
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr());
    if (!RT) continue;
    auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
    if (!BaseRD) continue;
    if (!hasVirtualFunctionsInHierarchy(BaseRD)) continue;

    if (BaseRD == Primary) { PastPrimary = true; continue; }
    if (!PastPrimary && Primary) continue;

    NumEntries += 2; // offset-to-top + RTTI
    for (CXXMethodDecl *MD : BaseRD->methods()) {
      if (MD->isVirtual()) NumEntries += vtableEntryCount(MD);
    }
  }

  return llvm::ArrayType::get(llvm::PointerType::get(CGM.getLLVMContext(), 0),
                              NumEntries);
}

unsigned CGCXX::GetVTableIndex(CXXMethodDecl *MD) {
  if (!MD) return 2;
  CXXRecordDecl *RD = MD->getParent();
  if (!RD) return 2;

  // 返回 MD 在 RD 的 vtable 组中的相对索引（从 ott+RTTI 后开始 = 偏移 2）
  // Itanium ABI：一个方法在其声明类的 vtable 中的位置在所有派生类中保持不变

  unsigned Idx = 2; // 跳过 offset-to-top + RTTI

  // 先数主基类的虚函数（如果 MD 覆盖了主基类的方法，返回其位置）
  CXXRecordDecl *Primary = getPrimaryBase(RD);
  if (Primary) {
    for (CXXMethodDecl *PMD : Primary->methods()) {
      if (!PMD->isVirtual()) continue;
      if (isMethodOverride(MD, PMD)) return Idx;
      Idx += vtableEntryCount(PMD);
    }
  }

  // RD 自身新增的虚函数（跳过覆盖主基类的——它们已在上面处理）
  for (CXXMethodDecl *M : RD->methods()) {
    if (!M->isVirtual()) continue;
    // 跳过覆盖主基类的方法（已占据主基类位置，不重复计数）
    if (Primary) {
      bool OverridesPrimary = false;
      for (CXXMethodDecl *PMD : Primary->methods()) {
        if (PMD->isVirtual() && isMethodOverride(M, PMD)) {
          OverridesPrimary = true;
          break;
        }
      }
      if (OverridesPrimary) continue;
    }
    if (M == MD) return Idx;
    Idx += vtableEntryCount(M);
  }

  return 2;
}

int CGCXX::GetVPtrIndex(CXXRecordDecl *RD) {
  if (!RD) return -1;

  bool HasVPtr = hasVirtualFunctionsInHierarchy(RD);
  if (!HasVPtr) return -1;

  // 主基类的 vptr 也在 index 0（共享）
  CXXRecordDecl *Primary = getPrimaryBase(RD);
  if (Primary) return 0;

  // 没有带虚函数的基类 — 自身 vptr 在索引 0
  return 0;
}

llvm::Value *CGCXX::EmitVirtualCall(CodeGenFunction &CGF, CXXMethodDecl *MD,
                                      llvm::Value *This,
                                      llvm::ArrayRef<llvm::Value *> Args,
                                      CXXRecordDecl *StaticType) {
  if (!MD || !This) return nullptr;
  CXXRecordDecl *RD = MD->getParent();
  if (!RD) return nullptr;

  // StaticType = this 指针指向的对象的静态类型（用于 MI 场景确定 vptr 位置）
  // 如果未提供，退化为 MD 的声明类
  CXXRecordDecl *VtblRD = StaticType ? StaticType : RD;

  // === 确定加载哪个 vptr ===
  // 方法在其声明类 RD 的 vtable 组中有固定位置
  // 在 StaticType 的对象中，需要找到 RD 对应的 vptr
  unsigned VPtrIdx = 0; // 默认：主 vptr（覆盖主基类或在主组中）

  if (StaticType && StaticType != RD) {
    // 多重继承场景：方法声明在非 StaticType 的类中
    // 检查 RD 是否是 StaticType 的非主基类
    CXXRecordDecl *Primary = getPrimaryBase(StaticType);
    if (Primary && RD == Primary) {
      // RD 就是 StaticType 的主基类 → 主 vptr (index 0)
      VPtrIdx = 0;
    } else if (RD != StaticType) {
      // RD 是 StaticType 的非主基类（或更深层的基类）
      // 需要找到 RD 在 StaticType 中的 vptr 位置
      // 先检查 RD 是否是直接非主基类
      bool FoundAsDirectBase = false;
      bool PastPrimary = false;
      for (const auto &Base : StaticType->bases()) {
        QualType BT = Base.getType();
        auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr());
        if (!RT) continue;
        auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
        if (!BaseRD) continue;
        if (BaseRD == Primary) { PastPrimary = true; continue; }
        if (BaseRD == RD && hasVirtualFunctionsInHierarchy(RD)) {
          VPtrIdx = getBaseVPtrStructIndex(StaticType, RD);
          FoundAsDirectBase = true;
          break;
        }
      }
      // 如果不是直接非主基类（可能是主基类的方法或更深层的方法）
      // 默认使用主 vptr
      if (!FoundAsDirectBase) VPtrIdx = 0;
    }
  }

  // 1. 加载 vptr
  llvm::StructType *ClassTy = CGM.getTypes().GetRecordType(VtblRD);
  llvm::Value *VTablePtrAddr = CGF.getBuilder().CreateStructGEP(
      ClassTy, This, VPtrIdx, "vtable.ptr");
  llvm::Value *VTable = CGF.getBuilder().CreateLoad(
      llvm::PointerType::get(CGM.getLLVMContext(), 0), VTablePtrAddr,
      "vtable");

  // 2. 获取 vtable 索引（相对于 vptr 所指向的组起始位置）
  unsigned VTableIdx = GetVTableIndex(MD);

  // 3. GEP 获取函数指针
  auto *VTableArrTy = llvm::ArrayType::get(
      llvm::PointerType::get(CGM.getLLVMContext(), 0), VTableIdx + 1);
  llvm::Value *FuncPtrPtr = CGF.getBuilder().CreateInBoundsGEP(
      VTableArrTy, VTable,
      {llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()), 0),
       llvm::ConstantInt::get(llvm::Type::getInt32Ty(CGM.getLLVMContext()),
                              VTableIdx)},
      "vfn.ptr");

  // 4. 加载函数指针
  llvm::Value *FuncPtr = CGF.getBuilder().CreateLoad(
      llvm::PointerType::get(CGM.getLLVMContext(), 0), FuncPtrPtr, "vfn");

  // 5. 构建参数列表
  llvm::SmallVector<llvm::Value *, 8> CallArgs;
  CallArgs.push_back(This);
  for (llvm::Value *Arg : Args) {
    CallArgs.push_back(Arg);
  }

  // 6. 间接调用
  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionTypeForDecl(MD);
  return CGF.getBuilder().CreateCall(FnTy, FuncPtr, CallArgs, "vcall");
}

void CGCXX::InitializeVTablePtr(CodeGenFunction &CGF, llvm::Value *This,
                                 CXXRecordDecl *RD,
                                 llvm::Value *ConstructionVTable) {
  if (!This || !RD) return;

  int VPtrIdx = GetVPtrIndex(RD);
  if (VPtrIdx < 0) return; // 此类不需要 vptr（没有虚函数）

  // 优先使用 construction vtable（如果提供）
  llvm::GlobalVariable *VTableGV = nullptr;
  llvm::Value *VTableToUse = ConstructionVTable;
  
  if (!VTableToUse) {
    VTableGV = EmitVTable(RD);
    if (!VTableGV) return;
  }

  llvm::StructType *ClassTy = CGM.getTypes().GetRecordType(RD);
  if (!ClassTy || ClassTy->getNumElements() == 0) return;

  auto &Ctx = CGM.getLLVMContext();
  auto *Zero64 = llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), 0);
  auto *Zero32 = llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 0);

  // === 主 vptr：存储 vtable 主组的地址（vtable[0][0]） ===
  unsigned VPtrUnsigned = static_cast<unsigned>(VPtrIdx);
  llvm::Value *VTablePtrAddr = CGF.getBuilder().CreateStructGEP(
      ClassTy, This, VPtrUnsigned, "vtable.ptr");

  llvm::Value *VTableAddr;
  if (VTableToUse) {
    // 使用 construction vtable
    VTableAddr = VTableToUse;
  } else {
    // 使用常规的 vtable
    VTableAddr = CGF.getBuilder().CreateInBoundsGEP(
        VTableGV->getValueType(), VTableGV, {Zero64, Zero32}, "vtable.addr");
  }

  CGF.getBuilder().CreateStore(VTableAddr, VTablePtrAddr);

  // === 多重继承：初始化次要 vptr ===
  // 遍历有虚函数的非主基类，在每个基类子对象位置存储对应的 vtable 组地址
  CXXRecordDecl *Primary = getPrimaryBase(RD);
  bool PastPrimary = false;
  for (const auto &Base : RD->bases()) {
    QualType BT = Base.getType();
    auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr());
    if (!RT) continue;
    auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
    if (!BaseRD) continue;
    if (!hasVirtualFunctionsInHierarchy(BaseRD)) continue;

    if (BaseRD == Primary) { PastPrimary = true; continue; }
    if (!PastPrimary && Primary) continue;

    // 此基类需要独立的 vptr
    unsigned BaseVPtrIdx = getBaseVPtrStructIndex(RD, BaseRD);
    llvm::Value *BaseVTablePtrAddr = CGF.getBuilder().CreateStructGEP(
        ClassTy, This, BaseVPtrIdx, BaseRD->getName().str() + ".vtable.ptr");

    // 计算 vtable 组偏移
    unsigned GroupOffset = computeVTableGroupOffset(RD, BaseRD);
    auto *GroupOffset32 =
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), GroupOffset);

    llvm::Value *GroupAddr;
    if (VTableToUse) {
      // 使用 construction vtable
      GroupAddr = CGF.getBuilder().CreateInBoundsGEP(
          llvm::ArrayType::get(llvm::PointerType::get(Ctx, 0), GroupOffset + 1),
          VTableToUse,
          {Zero64, GroupOffset32},
          BaseRD->getName().str() + ".vtable.group");
    } else if (VTableGV) {
      // 使用常规的 vtable
      GroupAddr = CGF.getBuilder().CreateInBoundsGEP(
          VTableGV->getValueType(), VTableGV, {Zero64, GroupOffset32},
          BaseRD->getName().str() + ".vtable.group");
    } else {
      continue; // 没有可用的 vtable
    }

    CGF.getBuilder().CreateStore(GroupAddr, BaseVTablePtrAddr);
  }
}

//===----------------------------------------------------------------------===//
// RTTI（运行时类型信息）
//===----------------------------------------------------------------------===//

llvm::GlobalVariable *CGCXX::EmitTypeInfo(CXXRecordDecl *RD) {
  if (!RD) return nullptr;

  // 查缓存
  auto It = TypeInfos.find(RD);
  if (It != TypeInfos.end()) return It->second;

  auto &Ctx = CGM.getLLVMContext();

  // === 1. 生成 typeinfo name (_ZTS<ClassN>) ===
  std::string TINameStr = CGM.getMangler().getTypeinfoName(RD);
  std::string ClassName = RD->getName().str();
  // 内容：类名 + null 终止符
  llvm::Constant *NameInit = llvm::ConstantDataArray::getString(
      Ctx, llvm::StringRef(ClassName), /*AddNull=*/true);
  auto *NameGV = new llvm::GlobalVariable(
      *CGM.getModule(), NameInit->getType(), /*isConstant=*/true,
      llvm::GlobalValue::LinkOnceODRLinkage, NameInit, TINameStr);
  NameGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // === 2. 生成 typeinfo 对象 (_ZTI<ClassN>) ===
  // 结构: [ vptr, name_ptr [, base_info...] ]
  // vptr 指向对应 RTTI 类的 vtable（__class_type_info / __si_class_type_info / __vmi_class_type_info）
  // 这些是 libcxxabi 中的外部符号

  llvm::SmallVector<llvm::Constant *, 8> TIFields;
  llvm::PointerType *PtrTy = llvm::PointerType::get(Ctx, 0);

  // 2a. vptr：指向对应 RTTI 类的 vtable + 16（跳过 offset-to-top + RTTI ptr 两个槽位）
  // 声明外部 vtable 全局变量
  auto *VTableArrTy = llvm::ArrayType::get(PtrTy, 3);
  auto *VTableExtern = CGM.getModule()->getOrInsertGlobal(
      getRTTIClassVTableName(RD), VTableArrTy);
  // vtable + 16 bytes = element [0, 2]（跳过前两个指针大小的槽位）
  llvm::Constant *VTablePtr = llvm::ConstantExpr::getInBoundsGetElementPtr(
      VTableArrTy, VTableExtern,
      llvm::ArrayRef<llvm::Constant *>{
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), 0),
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 2)});
  TIFields.push_back(VTablePtr);

  // 2b. name 指针
  llvm::Constant *NamePtr = llvm::ConstantExpr::getBitCast(NameGV, PtrTy);
  TIFields.push_back(NamePtr);

  // 2c. 根据继承类型追加基类信息
  unsigned NumBases = RD->getNumBases();
  if (NumBases == 0) {
    // __class_type_info: 只有 vptr + name
  } else if (NumBases == 1) {
    // __si_class_type_info: vptr + name + base typeinfo 指针
    auto &Base = RD->bases()[0];
    if (auto *RT = llvm::dyn_cast<RecordType>(Base.getType().getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        llvm::GlobalVariable *BaseTI = EmitTypeInfo(BaseCXX);
        TIFields.push_back(
            BaseTI ? llvm::cast<llvm::Constant>(BaseTI)
                   : llvm::ConstantPointerNull::get(PtrTy));
      }
    }
  } else {
    // __vmi_class_type_info: vptr + name + flags + base_count + {flags, offset}[]
    // flags: 0 = public, non-virtual, non-shadow
    unsigned Flags = 0;
    TIFields.push_back(
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), Flags));
    TIFields.push_back(
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), NumBases));
    for (const auto &Base : RD->bases()) {
      // 每个基类: [flags(32-bit), offset(32-bit)] 或 [flags(64-bit), offset(64-bit)]
      // Itanium ABI: long base_flags, long base_offset
      unsigned BaseFlags = 0;
      uint64_t BaseOffset = 0;
      if (auto *RT =
              llvm::dyn_cast<RecordType>(Base.getType().getTypePtr())) {
        if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          BaseOffset = GetBaseOffset(RD, BaseCXX);
          // 确保基类 typeinfo 先生成
          EmitTypeInfo(BaseCXX);
        }
      }
      TIFields.push_back(
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), BaseFlags));
      TIFields.push_back(
          llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), BaseOffset));
    }
  }

  // 构建 typeinfo 结构体类型
  auto *TIStructTy = llvm::StructType::get(Ctx,
      llvm::SmallVector<llvm::Type *, 8>(TIFields.size(), PtrTy));
  // 对于 __vmi_class_type_info，字段类型混合了 i32/i64，需要重新计算
  if (NumBases > 1) {
    llvm::SmallVector<llvm::Type *, 8> FieldTys;
    FieldTys.push_back(PtrTy); // vptr
    FieldTys.push_back(PtrTy); // name
    FieldTys.push_back(llvm::Type::getInt32Ty(Ctx)); // flags
    FieldTys.push_back(llvm::Type::getInt32Ty(Ctx)); // base_count
    for (unsigned i = 0; i < NumBases; ++i) {
      FieldTys.push_back(llvm::Type::getInt64Ty(Ctx)); // base_flags
      FieldTys.push_back(llvm::Type::getInt64Ty(Ctx)); // base_offset
    }
    TIStructTy = llvm::StructType::get(Ctx, FieldTys);
  } else {
    // 无基类或单继承：所有字段都是 ptr
    TIStructTy = llvm::StructType::get(Ctx,
        llvm::SmallVector<llvm::Type *, 8>(TIFields.size(), PtrTy));
  }

  llvm::Constant *TIInit = llvm::ConstantStruct::get(TIStructTy, TIFields);

  std::string TIName = CGM.getMangler().getRTTIName(RD);
  auto *TIGV = new llvm::GlobalVariable(
      *CGM.getModule(), TIStructTy, /*isConstant=*/true,
      llvm::GlobalValue::LinkOnceODRLinkage, TIInit, TIName);
  TIGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);

  TypeInfos[RD] = TIGV;
  return TIGV;
}

std::string CGCXX::getRTTIClassVTableName(CXXRecordDecl *RD) {
  if (!RD) return "_ZTVN10__cxxabiv117__class_type_infoE";
  unsigned NumBases = RD->getNumBases();
  if (NumBases == 0) {
    // __class_type_info vtable
    return "_ZTVN10__cxxabiv117__class_type_infoE";
  } else if (NumBases == 1) {
    // __si_class_type_info vtable
    return "_ZTVN10__cxxabiv120__si_class_type_infoE";
  } else {
    // __vmi_class_type_info vtable
    return "_ZTVN10__cxxabiv121__vmi_class_type_infoE";
  }
}

llvm::Value *CGCXX::EmitCatchTypeInfo(CodeGenFunction &CGF, QualType CatchType) {
  if (CatchType.isNull()) return nullptr;

  auto &Ctx = CGM.getLLVMContext();
  auto *PtrTy = llvm::PointerType::get(Ctx, 0);

  // Record 类型（class/struct）→ 使用 EmitTypeInfo
  if (CatchType->isRecordType()) {
    if (auto *RT = llvm::dyn_cast<RecordType>(CatchType.getTypePtr())) {
      if (auto *CXXRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (auto *TI = EmitTypeInfo(CXXRD)) {
          return CGF.getBuilder().CreateBitCast(TI, PtrTy, "catch.ti");
        }
      }
    }
    // 非 CXX record → fallback
    return nullptr;
  }

  // 基础类型和指针类型：引用 libcxxabi 的 __fundamental_type_info 全局符号
  // Itanium C++ ABI 命名约定：_ZTI + Itanium 类型 mangling
  //   int → _ZTIi, float → _ZTIf, char → _ZTIc, pointer → _ZTIPv 等

  std::string TIName = "_ZTI";

  if (auto *BT = llvm::dyn_cast<BuiltinType>(CatchType.getTypePtr())) {
    // 根据 BuiltinKind 映射到 Itanium mangling
    switch (BT->getKind()) {
    case BuiltinKind::Void:   TIName += "v"; break;
    case BuiltinKind::Bool:   TIName += "b"; break;
    case BuiltinKind::Char:   TIName += "c"; break;
    case BuiltinKind::SignedChar:  TIName += "a"; break;
    case BuiltinKind::UnsignedChar:  TIName += "h"; break;
    case BuiltinKind::WChar:  TIName += "w"; break;
    case BuiltinKind::Char16: TIName += "Ds"; break;
    case BuiltinKind::Char32: TIName += "Di"; break;
    case BuiltinKind::Char8:  TIName += "Du"; break;
    case BuiltinKind::Short:  TIName += "s"; break;
    case BuiltinKind::Int:    TIName += "i"; break;
    case BuiltinKind::Long:   TIName += "l"; break;
    case BuiltinKind::LongLong: TIName += "x"; break;
    case BuiltinKind::Int128: TIName += "n"; break;
    case BuiltinKind::UnsignedShort: TIName += "t"; break;
    case BuiltinKind::UnsignedInt:   TIName += "j"; break;
    case BuiltinKind::UnsignedLong:  TIName += "m"; break;
    case BuiltinKind::UnsignedLongLong: TIName += "y"; break;
    case BuiltinKind::UnsignedInt128: TIName += "o"; break;
    case BuiltinKind::Float:  TIName += "f"; break;
    case BuiltinKind::Double: TIName += "d"; break;
    case BuiltinKind::LongDouble: TIName += "e"; break;
    case BuiltinKind::Float128: TIName += "g"; break;
    case BuiltinKind::NullPtr: TIName += "Dn"; break;
    default: return nullptr; // 未知基础类型 → fallback
    }
  } else if (CatchType->isPointerType()) {
    // 指针类型 → _ZTIPv (void*) 简化
    TIName += "Pv";
  } else if (CatchType->isEnumType()) {
    // Enum 类型：底层是整数，简化为 int 的 typeinfo
    TIName += "i";
  } else {
    return nullptr;
  }

  // 引用外部 __fundamental_type_info 符号（由 libcxxabi 提供）
  // 结构: [ vptr, name_ptr ] — 与 __class_type_info 布局相同
  auto *TIStructTy = llvm::StructType::get(Ctx, {PtrTy, PtrTy});
  auto *TIGV = CGM.getModule()->getOrInsertGlobal(TIName, TIStructTy);

  return CGF.getBuilder().CreateBitCast(TIGV, PtrTy, "catch.ti");
}

//===----------------------------------------------------------------------===//
// dynamic_cast
//===----------------------------------------------------------------------===//

llvm::Value *CGCXX::EmitDynamicCast(CodeGenFunction &CGF,
                                     CXXDynamicCastExpr *CastExpr) {
  if (!CastExpr) return nullptr;

  auto &Builder = CGF.getBuilder();
  auto &Ctx = CGM.getLLVMContext();

  // 1. 求值子表达式
  llvm::Value *SrcPtr = CGF.EmitExpr(CastExpr->getSubExpr());
  if (!SrcPtr) return nullptr;

  QualType DestType = CastExpr->getDestType();
  if (DestType.isNull()) return SrcPtr;

  // 判断是引用类型还是指针类型
  bool DestIsRef = false;
  QualType DestPointeeType;
  if (auto *RefTy = llvm::dyn_cast<ReferenceType>(DestType.getTypePtr())) {
    DestIsRef = true;
    DestPointeeType = RefTy->getReferencedType();
  } else if (auto *PtrTy = llvm::dyn_cast<PointerType>(DestType.getTypePtr())) {
    DestPointeeType = PtrTy->getPointeeType();
  } else {
    // 非指针/引用目标，fallback bitcast
    auto *DestLLVMTy = CGM.getTypes().ConvertType(DestType);
    if (SrcPtr->getType()->isPointerTy() && DestLLVMTy->isPointerTy()) {
      return Builder.CreateBitCast(SrcPtr, DestLLVMTy, "dyn.cast");
    }
    return SrcPtr;
  }

  // 获取源类型的 RecordDecl
  QualType SrcExprType = CastExpr->getSubExpr()->getType();
  // 如果源是指针，取指向类型
  if (auto *SrcPtrTy = llvm::dyn_cast<PointerType>(SrcExprType.getTypePtr())) {
    SrcExprType = SrcPtrTy->getPointeeType();
  }
  // 如果源是引用，取引用类型
  if (auto *SrcRefTy = llvm::dyn_cast<ReferenceType>(SrcExprType.getTypePtr())) {
    SrcExprType = SrcRefTy->getReferencedType();
  }

  CXXRecordDecl *SrcRD = nullptr;
  if (auto *RT = llvm::dyn_cast<RecordType>(SrcExprType.getTypePtr())) {
    SrcRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
  }
  CXXRecordDecl *DstRD = nullptr;
  if (auto *RT = llvm::dyn_cast<RecordType>(DestPointeeType.getTypePtr())) {
    DstRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
  }

  // 非多态类型或缺少 RD 信息，fallback 为 static bitcast
  if (!SrcRD || !DstRD || !hasVirtualFunctionsInHierarchy(SrcRD)) {
    auto *DestLLVMTy = CGM.getTypes().ConvertType(DestType);
    if (SrcPtr->getType()->isPointerTy() && DestLLVMTy->isPointerTy()) {
      return Builder.CreateBitCast(SrcPtr, DestLLVMTy, "dyn.cast");
    }
    return SrcPtr;
  }

  // 如果 SrcPtr 不是指针类型，需要先取地址（引用情况）
  llvm::PointerType *PtrTy = llvm::PointerType::get(Ctx, 0);
  if (!SrcPtr->getType()->isPointerTy()) {
    SrcPtr = Builder.CreatePtrToInt(SrcPtr, llvm::Type::getInt64Ty(Ctx));
    SrcPtr = Builder.CreateIntToPtr(SrcPtr, PtrTy, "src.ptr");
  }

  // 2. Null check（仅指针类型需要；引用类型失败时抛 bad_cast）
  llvm::Function *CurFn = CGF.getCurrentFunction();
  llvm::BasicBlock *EntryBB = Builder.GetInsertBlock();

  llvm::BasicBlock *CastBB =
      llvm::BasicBlock::Create(Ctx, "dyn.cast", CurFn);
  llvm::BasicBlock *NullBB =
      llvm::BasicBlock::Create(Ctx, "dyn.cast.null", CurFn);
  llvm::BasicBlock *MergeBB =
      llvm::BasicBlock::Create(Ctx, "dyn.cast.merge", CurFn);

  llvm::Value *NullVal = llvm::ConstantPointerNull::get(PtrTy);
  llvm::Value *IsNotNull = Builder.CreateICmpNE(SrcPtr, NullVal, "dyn.cast.notnull");
  Builder.CreateCondBr(IsNotNull, CastBB, NullBB);

  // === CastBB: 执行运行时检查 ===
  Builder.SetInsertPoint(CastBB);

  // 3. 加载 vptr → vtable
  llvm::Value *VTablePtrAddr = Builder.CreateBitCast(
      SrcPtr, llvm::PointerType::get(PtrTy, 0), "vtable.ptr.addr");
  llvm::Value *VTable = Builder.CreateLoad(PtrTy, VTablePtrAddr, "vtable");

  // 4. 从 vtable slot[1] 加载 RTTI 指针
  auto *VTableArrTy = llvm::ArrayType::get(PtrTy, 2);
  llvm::Value *RTTISlotPtr = Builder.CreateInBoundsGEP(
      VTableArrTy, VTable,
      {llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), 0),
       llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 1)},
      "rtti.slot");
  llvm::Value *SrcTypeInfo = Builder.CreateLoad(PtrTy, RTTISlotPtr, "src.ti");

  // 5. 获取目标类型的 typeinfo
  llvm::GlobalVariable *DstTypeInfo = EmitTypeInfo(DstRD);
  llvm::Value *DstTI = DstTypeInfo
      ? llvm::cast<llvm::Value>(Builder.CreateBitCast(DstTypeInfo, PtrTy))
      : NullVal;

  // 6. 计算 src2dst 偏移量
  int64_t SrcToDst = -1; // -1 表示未知，让运行时全路径搜索
  // 如果 SrcRD 派生自 DstRD（upcast），偏移量已知
  if (SrcRD->isDerivedFrom(DstRD)) {
    SrcToDst = static_cast<int64_t>(GetBaseOffset(SrcRD, DstRD));
  }

  // 7. 声明并调用 __dynamic_cast
  // __dynamic_cast(void *src, const __class_type_info *src_type,
  //                const __class_type_info *dst_type, int64_t src2dst_offset)
  llvm::FunctionType *DynCastTy = llvm::FunctionType::get(
      PtrTy,
      {PtrTy, PtrTy, PtrTy, llvm::Type::getInt64Ty(Ctx)},
      false);
  llvm::FunctionCallee DynCastFn =
      CGM.getModule()->getOrInsertFunction("__dynamic_cast", DynCastTy);

  llvm::Value *SrcToDstVal =
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), SrcToDst, true);
  llvm::Value *DynCastResult = Builder.CreateCall(
      DynCastFn, {SrcPtr, SrcTypeInfo, DstTI, SrcToDstVal}, "dyn.cast.result");

  // 8. bitcast 到目标指针类型
  auto *DestLLVMTy = CGM.getTypes().ConvertType(DestType);
  llvm::Type *ResultPtrTy = DestLLVMTy;
  // 对于指针类型目标，直接 bitcast
  // 对于引用类型目标，也使用指针类型
  if (ResultPtrTy->isPointerTy()) {
    DynCastResult = Builder.CreateBitCast(DynCastResult, ResultPtrTy, "dyn.cast.typed");
  }

  llvm::Value *CastResult = DynCastResult;
  Builder.CreateBr(MergeBB);

  // === NullBB: null 输入 ===
  Builder.SetInsertPoint(NullBB);

  if (DestIsRef) {
    // 引用类型 dynamic_cast 失败时调用 __cxa_bad_cast()
    llvm::FunctionType *BadCastTy =
        llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), false);
    llvm::FunctionCallee BadCastFn =
        CGM.getModule()->getOrInsertFunction("__cxa_bad_cast", BadCastTy);
    Builder.CreateCall(BadCastFn);
    Builder.CreateUnreachable();
  } else {
    // 指针类型返回 null
    Builder.CreateBr(MergeBB);
  }

  // === MergeBB: PHI 节点 ===
  Builder.SetInsertPoint(MergeBB);

  if (!DestIsRef) {
    llvm::PHINode *PHI = Builder.CreatePHI(CastResult->getType(), 2, "dyn.cast.phi");
    PHI->addIncoming(CastResult, CastBB);
    // NullBB 可能跳到 MergeBB 或 unreachable
    llvm::BasicBlock *NullSucc = NullBB->getTerminator()
        ? NullBB->getTerminator()->getSuccessor(0) : nullptr;
    if (NullSucc == MergeBB) {
      llvm::Value *NullResult = llvm::ConstantPointerNull::get(
          llvm::cast<llvm::PointerType>(CastResult->getType()));
      PHI->addIncoming(NullResult, NullBB);
    }
    return PHI;
  }

  // 引用类型直接返回 CastResult（bad_cast 已在上面的路径处理）
  return CastResult;
}

llvm::Value *CGCXX::EmitBaseOffset(CodeGenFunction &CGF,
                                     llvm::Value *DerivedPtr,
                                     CXXRecordDecl *Base) {
  (void)CGF;
  (void)DerivedPtr;
  (void)Base;
  return nullptr;
}

llvm::Value *CGCXX::EmitDerivedOffset(CodeGenFunction &CGF,
                                        llvm::Value *BasePtr,
                                        CXXRecordDecl *Derived) {
  (void)CGF;
  (void)BasePtr;
  (void)Derived;
  return nullptr;
}

llvm::Value *CGCXX::EmitCastToBase(CodeGenFunction &CGF, CXXRecordDecl *Derived,
                                    llvm::Value *DerivedPtr, CXXRecordDecl *Base) {
  if (!DerivedPtr || !Base || !Derived) return DerivedPtr;

  // 检查是否为虚继承
  bool IsVirtualInheritance = false;
  unsigned VBaseIndex = 0; // 虚基类在 vtable 中的索引
  for (const auto &B : Derived->bases()) {
    QualType BaseType = B.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (BaseCXX == Base && B.isVirtual()) {
          IsVirtualInheritance = true;
          break;
        }
        if (B.isVirtual()) {
          VBaseIndex++; // 计算当前虚基类的索引
        }
      }
    }
  }

  uint64_t Offset = GetBaseOffset(Derived, Base);
  
  // 对于虚继承，从 vtable 中读取运行时偏移
  if (IsVirtualInheritance) {
    auto &Ctx = CGM.getLLVMContext();
    auto *Builder = &CGF.getBuilder();
    
    // 1. 加载 vptr
    llvm::Value *VTablePtrAddr = Builder->CreateBitCast(
        DerivedPtr, llvm::PointerType::get(llvm::PointerType::get(Ctx, 0), 0),
        "vtable.addr");
    llvm::Value *VTable = Builder->CreateLoad(
        llvm::PointerType::get(Ctx, 0), VTablePtrAddr, "vtable");
    
    // 2. 计算虚基类偏移在 vtable 中的位置
    // vtable 布局: [offset-to-top(0), RTTI(1), vbase-offsets(2..N), methods...]
    unsigned VBaseSlot = 2 + VBaseIndex; // 跳过 ott 和 RTTI
    
    llvm::Value *VBaseOffsetPtr = Builder->CreateInBoundsGEP(
        llvm::PointerType::get(Ctx, 0), VTable,
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), VBaseSlot),
        "vbase.offset.ptr");
    
    // 3. 加载偏移量
    llvm::Value *OffsetVal = Builder->CreateLoad(
        llvm::Type::getInt64Ty(Ctx), VBaseOffsetPtr, "vbase.offset");
    
    // 4. 调整指针
    llvm::Value *IntPtr = Builder->CreatePtrToInt(
        DerivedPtr, llvm::Type::getInt64Ty(Ctx), "ptr.int");
    llvm::Value *AdjustedPtr = Builder->CreateAdd(
        IntPtr, OffsetVal, "vbase.adj");
    return Builder->CreateIntToPtr(
        AdjustedPtr, DerivedPtr->getType(), "vbase.ptr");
  }
  
  if (Offset == 0) return DerivedPtr;

  // 使用 ptrtoint + add + inttoptr 进行字节级指针调整
  llvm::Value *OffsetVal = llvm::ConstantInt::get(
      llvm::Type::getInt64Ty(CGM.getLLVMContext()), Offset);
  llvm::Value *IntPtr = CGF.getBuilder().CreatePtrToInt(
      DerivedPtr, llvm::Type::getInt64Ty(CGM.getLLVMContext()), "ptr.int");
  llvm::Value *AdjustedPtr = CGF.getBuilder().CreateAdd(
      IntPtr, OffsetVal, "base.adj");
  return CGF.getBuilder().CreateIntToPtr(
      AdjustedPtr, DerivedPtr->getType(), "base.ptr");
}

llvm::Value *CGCXX::EmitCastToDerived(CodeGenFunction &CGF, CXXRecordDecl *Derived,
                                        llvm::Value *BasePtr, CXXRecordDecl *Base) {
  if (!BasePtr || !Derived || !Base) return BasePtr;

  uint64_t Offset = GetBaseOffset(Derived, Base);
  if (Offset == 0) return BasePtr;

  // 使用 ptrtoint + sub + inttoptr 进行反向指针调整
  llvm::Value *OffsetVal = llvm::ConstantInt::get(
      llvm::Type::getInt64Ty(CGM.getLLVMContext()), Offset);
  llvm::Value *IntPtr = CGF.getBuilder().CreatePtrToInt(
      BasePtr, llvm::Type::getInt64Ty(CGM.getLLVMContext()), "ptr.int");
  llvm::Value *AdjustedPtr = CGF.getBuilder().CreateSub(
      IntPtr, OffsetVal, "derived.adj");
  return CGF.getBuilder().CreateIntToPtr(
      AdjustedPtr, BasePtr->getType(), "derived.ptr");
}

//===----------------------------------------------------------------------===//
// Thunk 和 VTT 支持
//===----------------------------------------------------------------------===//

llvm::Function *CGCXX::EmitThunk(CXXMethodDecl *MD, CXXRecordDecl *Base,
                                  int64_t ThisAdjustment) {
  if (!MD || !Base) return nullptr;

  auto &Ctx = CGM.getLLVMContext();
  
  // 生成 thunk 名称: _ZThnN_<offset>_<mangled-name>
  std::string MangledName = CGM.getMangler().getMangledName(MD);
  std::string ThunkName = "_ZThn" + std::to_string(ThisAdjustment) + "_" + MangledName;
  
  // 检查是否已存在
  if (auto *Existing = CGM.getModule()->getFunction(ThunkName)) {
    return Existing;
  }

  // 获取目标函数的类型
  llvm::Function *TargetFn = CGM.GetOrCreateFunctionDecl(MD);
  if (!TargetFn) return nullptr;

  // 创建 thunk 函数，签名与目标函数相同
  llvm::FunctionType *ThunkTy = TargetFn->getFunctionType();
  llvm::Function *ThunkFn = llvm::Function::Create(
      ThunkTy, llvm::GlobalValue::LinkOnceODRLinkage, ThunkName, CGM.getModule());
  ThunkFn->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // 生成 thunk 代码
  auto *EntryBB = llvm::BasicBlock::Create(Ctx, "entry", ThunkFn);
  llvm::IRBuilder<> Builder(EntryBB);

  // 调整 this 指针（第一个参数）
  auto Args = ThunkFn->args();
  auto It = Args.begin();
  llvm::Value *ThisPtr = &*It;
  
  if (ThisAdjustment != 0) {
    llvm::Value *OffsetVal = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(Ctx), ThisAdjustment, true /*signed*/);
    llvm::Value *IntPtr = Builder.CreatePtrToInt(
        ThisPtr, llvm::Type::getInt64Ty(Ctx), "thunk.ptr.int");
    llvm::Value *AdjustedPtr = Builder.CreateAdd(
        IntPtr, OffsetVal, "thunk.adj");
    ThisPtr = Builder.CreateIntToPtr(
        AdjustedPtr, ThisPtr->getType(), "thunk.this");
  }

  // 调用目标函数
  llvm::SmallVector<llvm::Value *, 8> CallArgs;
  CallArgs.push_back(ThisPtr);
  ++It;
  for (; It != Args.end(); ++It) {
    CallArgs.push_back(&*It);
  }

  llvm::Value *Result = Builder.CreateCall(TargetFn, CallArgs, "thunk.call");
  
  // 返回结果
  if (ThunkTy->getReturnType()->isVoidTy()) {
    Builder.CreateRetVoid();
  } else {
    Builder.CreateRet(Result);
  }

  return ThunkFn;
}

llvm::GlobalVariable *CGCXX::EmitVTT(CXXRecordDecl *RD) {
  if (!RD) return nullptr;

  // 检查是否有虚基类
  bool HasVirtualBase = false;
  for (const auto &Base : RD->bases()) {
    if (Base.isVirtual()) {
      HasVirtualBase = true;
      break;
    }
  }
  
  if (!HasVirtualBase) return nullptr;

  auto &Ctx = CGM.getLLVMContext();
  llvm::PointerType *PtrTy = llvm::PointerType::get(Ctx, 0);

  // VTT 结构:
  // [primary vtable ptr, construction vtables for each base...]
  // Construction vtables 包含: [vtable ptr, offset-to-vbase, ...]
  
  llvm::SmallVector<llvm::Constant *, 8> VTTFields;
  
  // 1. 主 vtable 指针
  llvm::GlobalVariable *PrimaryVT = CGM.getCXX().EmitVTable(RD);
  if (PrimaryVT) {
    VTTFields.push_back(llvm::ConstantExpr::getBitCast(PrimaryVT, PtrTy));
  }

  // 2. 为每个有虚函数的基类生成构造 vtable
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (hasVirtualFunctionsInHierarchy(BaseCXX)) {
          // 构造时的 vtable（可能不同，因为虚基类偏移未确定）
          llvm::GlobalVariable *BaseVT = CGM.getCXX().EmitVTable(BaseCXX);
          if (BaseVT) {
            VTTFields.push_back(llvm::ConstantExpr::getBitCast(BaseVT, PtrTy));
          }
        }
      }
    }
  }

  if (VTTFields.empty()) return nullptr;

  // 创建 VTT 全局变量
  auto *VTTTy = llvm::ArrayType::get(PtrTy, VTTFields.size());
  llvm::Constant *VTTInit = llvm::ConstantArray::get(VTTTy, VTTFields);
  
  std::string MangledName = CGM.getMangler().getRTTIName(RD);
  std::string VTTName = "_ZTT" + MangledName.substr(4); // 去掉 "_ZTI" 前缀
  auto *VTTGV = new llvm::GlobalVariable(
      *CGM.getModule(), VTTTy, /*isConstant=*/true,
      llvm::GlobalValue::LinkOnceODRLinkage, VTTInit, VTTName);
  VTTGV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  return VTTGV;
}

//===----------------------------------------------------------------------===//
// 成员初始化
//===----------------------------------------------------------------------===//

void CGCXX::EmitBaseInitializer(CodeGenFunction &CGF, CXXRecordDecl *Class,
                                 llvm::Value *This,
                                 CXXRecordDecl::BaseSpecifier *Base,
                                 Expr *Init) {
  if (!Class || !This || !Base) return;

  QualType BaseType = Base->getType();
  auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr());
  if (!RT) return;
  auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
  if (!BaseRD) return;

  llvm::Value *BasePtr = EmitCastToBase(CGF, Class, This, BaseRD);

  if (Init) {
    if (auto *ConstructExpr = llvm::dyn_cast<CXXConstructExpr>(Init)) {
      llvm::SmallVector<llvm::Value *, 4> Args;
      Args.push_back(BasePtr);
      for (Expr *Arg : ConstructExpr->getArgs()) {
        llvm::Value *ArgVal = CGF.EmitExpr(Arg);
        if (ArgVal) Args.push_back(ArgVal);
      }
      for (CXXMethodDecl *MD : BaseRD->methods()) {
        if (auto *Ctor = llvm::dyn_cast<CXXConstructorDecl>(MD)) {
          if (Ctor->getNumParams() == ConstructExpr->getArgs().size()) {
            llvm::Function *CtorFn = CGM.GetOrCreateFunctionDecl(Ctor);
            if (CtorFn) {
              CGF.getBuilder().CreateCall(CtorFn, Args, "base.ctor");
              return;
            }
          }
        }
      }
    }

    llvm::Value *InitVal = CGF.EmitExpr(Init);
    if (InitVal) {
      llvm::StructType *BaseTy = CGM.getTypes().GetRecordType(BaseRD);
      llvm::Value *TypedPtr = CGF.getBuilder().CreateBitCast(
          BasePtr, llvm::PointerType::get(BaseTy, 0), "base.ptr");
      CGF.getBuilder().CreateStore(InitVal, TypedPtr);
    }
  } else {
    llvm::StructType *BaseTy = CGM.getTypes().GetRecordType(BaseRD);
    llvm::Value *TypedPtr = CGF.getBuilder().CreateBitCast(
        BasePtr, llvm::PointerType::get(BaseTy, 0), "base.ptr");
    CGF.getBuilder().CreateStore(
        llvm::Constant::getNullValue(BaseTy), TypedPtr);
  }
}

void CGCXX::EmitMemberInitializer(CodeGenFunction &CGF, CXXRecordDecl *Class,
                                    llvm::Value *This, FieldDecl *Field,
                                    llvm::ArrayRef<Expr *> Args) {
  if (!Class || !This || !Field) return;

  llvm::StructType *StructTy = CGM.getTypes().GetRecordType(Class);
  unsigned FieldIdx = CGM.getTypes().GetFieldIndex(Field);
  llvm::Type *FieldLLVMTy = CGM.getTypes().ConvertType(Field->getType());

  llvm::Value *FieldPtr = CGF.getBuilder().CreateStructGEP(
      StructTy, This, FieldIdx, Field->getName());

  if (!Args.empty()) {
    // 多参数或单参数初始化：优先检测是否为类类型成员的构造函数调用
    if (Args.size() == 1) {
      if (auto *ConstructExpr = llvm::dyn_cast<CXXConstructExpr>(Args[0])) {
        // 单个 CXXConstructExpr 参数 → 调用成员的构造函数
        llvm::SmallVector<llvm::Value *, 4> CallArgs;
        CallArgs.push_back(FieldPtr);
        for (Expr *Arg : ConstructExpr->getArgs()) {
          llvm::Value *ArgVal = CGF.EmitExpr(Arg);
          if (ArgVal) CallArgs.push_back(ArgVal);
        }
        if (auto *FRT =
                llvm::dyn_cast<RecordType>(Field->getType().getTypePtr())) {
          if (auto *FieldCXX =
                  llvm::dyn_cast<CXXRecordDecl>(FRT->getDecl())) {
            for (CXXMethodDecl *MD : FieldCXX->methods()) {
              if (auto *Ctor = llvm::dyn_cast<CXXConstructorDecl>(MD)) {
                if (Ctor->getNumParams() == ConstructExpr->getArgs().size()) {
                  llvm::Function *CtorFn = CGM.GetOrCreateFunctionDecl(Ctor);
                  if (CtorFn) {
                    CGF.getBuilder().CreateCall(CtorFn, CallArgs, "field.ctor");
                    return;
                  }
                }
              }
            }
          }
        }
      }
    }

    // 多参数初始化：如果成员是类类型，尝试查找匹配的构造函数
    if (Args.size() > 1 || (Args.size() == 1 && llvm::isa<CXXConstructExpr>(Args[0]))) {
      if (auto *FRT =
              llvm::dyn_cast<RecordType>(Field->getType().getTypePtr())) {
        if (auto *FieldCXX =
                llvm::dyn_cast<CXXRecordDecl>(FRT->getDecl())) {
          // 如果唯一的参数是 CXXConstructExpr，使用其内部参数计数
          unsigned EffectiveArgCount = Args.size();
          if (Args.size() == 1) {
            if (auto *CE = llvm::dyn_cast<CXXConstructExpr>(Args[0])) {
              EffectiveArgCount = CE->getArgs().size();
            }
          }
          for (CXXMethodDecl *MD : FieldCXX->methods()) {
            if (auto *Ctor = llvm::dyn_cast<CXXConstructorDecl>(MD)) {
              if (Ctor->getNumParams() == EffectiveArgCount) {
                llvm::Function *CtorFn = CGM.GetOrCreateFunctionDecl(Ctor);
                if (CtorFn) {
                  llvm::SmallVector<llvm::Value *, 4> CallArgs;
                  CallArgs.push_back(FieldPtr);
                  for (Expr *Arg : Args) {
                    llvm::Value *ArgVal = CGF.EmitExpr(Arg);
                    if (ArgVal) CallArgs.push_back(ArgVal);
                  }
                  CGF.getBuilder().CreateCall(CtorFn, CallArgs, "field.ctor");
                  return;
                }
              }
            }
          }
        }
      }
    }

    // 单个表达式初始化：求值 + 类型转换 + store
    llvm::Value *InitVal = CGF.EmitExpr(Args[0]);
    if (InitVal && FieldLLVMTy) {
      if (InitVal->getType() != FieldLLVMTy) {
        if (InitVal->getType()->isIntegerTy() && FieldLLVMTy->isIntegerTy()) {
          InitVal = CGF.getBuilder().CreateIntCast(InitVal, FieldLLVMTy, true,
                                                    "init.cast");
        } else if (InitVal->getType()->isFloatingPointTy() &&
                   FieldLLVMTy->isFloatingPointTy()) {
          InitVal = CGF.getBuilder().CreateFPCast(InitVal, FieldLLVMTy,
                                                   "init.cast");
        } else if (InitVal->getType()->isPointerTy() &&
                   FieldLLVMTy->isPointerTy()) {
          InitVal = CGF.getBuilder().CreateBitCast(InitVal, FieldLLVMTy,
                                                    "init.cast");
        }
      }
      CGF.getBuilder().CreateStore(InitVal, FieldPtr);
    }
  } else {
    // 无参数：零初始化
    if (FieldLLVMTy) {
      CGF.getBuilder().CreateStore(
          llvm::Constant::getNullValue(FieldLLVMTy), FieldPtr);
    }
  }
}

void CGCXX::EmitDestructorBody(CodeGenFunction &CGF, CXXRecordDecl *Class,
                                 llvm::Value *This) {
  if (!Class || !This) return;

  // 成员析构（逆序）
  auto Fields = Class->fields();
  for (int i = static_cast<int>(Fields.size()) - 1; i >= 0; --i) {
    FieldDecl *FD = Fields[i];
    if (auto *RT = llvm::dyn_cast<RecordType>(FD->getType().getTypePtr())) {
      if (auto *FieldCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (FieldCXX->hasDestructor()) {
          for (CXXMethodDecl *MD : FieldCXX->methods()) {
            if (auto *Dtor = llvm::dyn_cast<CXXDestructorDecl>(MD)) {
              llvm::Function *DtorFn = CGM.GetOrCreateFunctionDecl(Dtor);
              if (DtorFn) {
                llvm::StructType *StructTy =
                    CGM.getTypes().GetRecordType(Class);
                unsigned FieldIdx = CGM.getTypes().GetFieldIndex(FD);
                llvm::Value *FieldPtr = CGF.getBuilder().CreateStructGEP(
                    StructTy, This, FieldIdx, FD->getName() + ".dtor");
                CGF.getBuilder().CreateCall(DtorFn, {FieldPtr}, "field.dtor");
              }
              break;
            }
          }
        }
      }
    }
  }

  // 基类析构（逆序）
  auto Bases = Class->bases();
  for (int i = static_cast<int>(Bases.size()) - 1; i >= 0; --i) {
    QualType BaseType = Bases[i].getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        if (BaseCXX->hasDestructor()) {
          for (CXXMethodDecl *MD : BaseCXX->methods()) {
            if (auto *Dtor = llvm::dyn_cast<CXXDestructorDecl>(MD)) {
              llvm::Function *DtorFn = CGM.GetOrCreateFunctionDecl(Dtor);
              if (DtorFn) {
                // 在调用基类析构函数之前，将 vptr 恢复为基类的 vtable
                // 这是 Itanium C++ ABI 的要求，确保基类析构函数中的虚函数调用使用正确的 vtable
                llvm::Value *BasePtr = EmitCastToBase(CGF, Class, This, BaseCXX);
                
                // 保存当前的 vptr 值（如果需要的话）
                // 注意：在完整的实现中，应该在析构函数开始时保存所有 vptr
                // 然后在每个基类析构前恢复对应的 vptr
                
                // 将 this 对象的 vptr 设置为基类的 vtable
                InitializeVTablePtr(CGF, This, BaseCXX);
                
                CGF.getBuilder().CreateCall(DtorFn, {BasePtr}, "base.dtor");
                
                // 恢复派生类的 vptr（在下一个基类析构或成员析构之前）
                // 注意：这里简化处理，实际应该在每个基类析构后恢复
                InitializeVTablePtr(CGF, This, Class);
              }
              break;
            }
          }
        }
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// P7.1.1: Deducing this (P0847R7)
//===----------------------------------------------------------------------===//

void CGCXX::EmitExplicitObjectParameterCall(
    CodeGenFunction &CGF, CXXMemberCallExpr *E, llvm::Value *ObjectArg,
    llvm::SmallVectorImpl<llvm::Value *> &CallArgs) {
  // The object argument is already prepended to CallArgs by the caller
  // (EmitCallExpr). For explicit object parameter methods, the function
  // signature includes the explicit object parameter as the first parameter
  // (no implicit this). No additional work needed here — the call is
  // generated by EmitCallExpr's normal path.
  //
  // This method exists for potential future customization (e.g., thunk
  // generation, ABI adjustments).
}

//===----------------------------------------------------------------------===//
// P7.1.3: Static operator (P1169R4, P2589R1)
//===----------------------------------------------------------------------===//

llvm::Value *CGCXX::EmitStaticOperatorCall(CodeGenFunction &CGF,
                                    CXXMethodDecl *MD,
                                    llvm::ArrayRef<llvm::Value *> Args) {
  // Static operators are called without a this pointer.
  // The function is looked up and called as a static method.
  llvm::Function *Fn = CGM.GetOrCreateFunctionDecl(MD);
  if (!Fn) return nullptr;

  return CGF.EmitCallOrInvoke(Fn, Args, "static.op.call");
}
