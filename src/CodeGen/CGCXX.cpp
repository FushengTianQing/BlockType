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
// 类布局
//===----------------------------------------------------------------------===//

llvm::SmallVector<uint64_t, 16> CGCXX::ComputeClassLayout(CXXRecordDecl *RD) {
  llvm::SmallVector<uint64_t, 16> FieldOffsets;
  if (!RD) return FieldOffsets;

  uint64_t CurrentOffset = 0;

  // 1. 基类子对象（按声明顺序排列）
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        uint64_t BaseSize = GetClassSize(BaseCXX);
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
      }
    }
  }

  // 2. vptr 指针（如果有虚函数，且没有带虚函数的基类时放在此处）
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

  // 如果没有带 vptr 的基类，且自身需要 vptr，则在此处放置
  if (HasVPtr && !HasVirtualBase) {
    uint64_t PtrSize = CGM.getTarget().getPointerSize();
    uint64_t PtrAlign = CGM.getTarget().getPointerAlign();
    CurrentOffset = llvm::alignTo(CurrentOffset, PtrAlign);
    CurrentOffset += PtrSize;
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

  // === Phase 1 + Phase 2 交织: 基类初始化 + 每个基类后更新 vptr ===
  llvm::SmallVector<const CXXRecordDecl *, 4> InitializedBases;

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
                InitializeVTablePtr(CGF, This, Class);
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
            InitializeVTablePtr(CGF, This, Class);
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
        EmitMemberInitializer(CGF, Class, This, Field,
                              Init->getArguments().empty()
                                  ? nullptr
                                  : Init->getArguments()[0]);
      }
    }
  }

  // 默认初始化未出现在初始化列表中的成员
  for (FieldDecl *FD : Class->fields()) {
    bool AlreadyInit = false;
    for (auto N : InitializedMembers) {
      if (N == FD->getName()) {
        AlreadyInit = true;
        break;
      }
    }
    if (!AlreadyInit) {
      llvm::StructType *StructTy = CGM.getTypes().GetRecordType(Class);
      unsigned FieldIdx = CGM.getTypes().GetFieldIndex(FD);
      llvm::Value *FieldPtr = CGF.getBuilder().CreateStructGEP(
          StructTy, This, FieldIdx, FD->getName());
      llvm::Type *FieldLLVMTy = CGM.getTypes().ConvertType(FD->getType());
      CGF.getBuilder().CreateStore(
          llvm::Constant::getNullValue(FieldLLVMTy), FieldPtr);
    }
  }

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
    CGF.getBuilder().CreateCall(DtorFn, {Ptr}, "dtor.call");
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

  // VTable 布局：
  // [offset-to-top] [RTTI pointer] [base class vfuncs...] [own new vfuncs...]

  llvm::SmallVector<llvm::Constant *, 16> VTableEntries;

  // offset-to-top (0 for primary vtable)
  VTableEntries.push_back(llvm::ConstantExpr::getIntToPtr(
      llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()), 0),
      llvm::PointerType::get(CGM.getLLVMContext(), 0)));

  // RTTI 指针：指向 typeinfo 全局变量
  llvm::GlobalVariable *TypeInfo = EmitTypeInfo(RD);
  if (TypeInfo) {
    VTableEntries.push_back(llvm::ConstantExpr::getBitCast(
        TypeInfo, llvm::PointerType::get(CGM.getLLVMContext(), 0)));
  } else {
    VTableEntries.push_back(llvm::ConstantPointerNull::get(
        llvm::PointerType::get(CGM.getLLVMContext(), 0)));
  }

  // 收集基类的虚函数（检查覆盖）
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        for (CXXMethodDecl *MD : BaseCXX->methods()) {
          if (!MD->isVirtual()) continue;
          // 检查派生类是否覆盖
          CXXMethodDecl *Overridden = nullptr;
          for (CXXMethodDecl *DerivedMD : RD->methods()) {
            if (DerivedMD->isVirtual() &&
                DerivedMD->getName() == MD->getName() &&
                DerivedMD->getNumParams() == MD->getNumParams()) {
              Overridden = DerivedMD;
              break;
            }
          }
          if (Overridden) {
            llvm::Function *Fn = CGM.GetOrCreateFunctionDecl(Overridden);
            VTableEntries.push_back(
                Fn ? llvm::cast<llvm::Constant>(Fn)
                   : llvm::ConstantPointerNull::get(
                         llvm::PointerType::get(CGM.getLLVMContext(), 0)));
          } else {
            llvm::Function *Fn = CGM.GetOrCreateFunctionDecl(MD);
            VTableEntries.push_back(
                Fn ? llvm::cast<llvm::Constant>(Fn)
                   : llvm::ConstantPointerNull::get(
                         llvm::PointerType::get(CGM.getLLVMContext(), 0)));
          }
        }
      }
    }
  }

  // 自身新增的虚函数（不在基类中的）
  for (CXXMethodDecl *MD : RD->methods()) {
    if (!MD->isVirtual()) continue;
    bool AlreadyInVTable = false;
    for (const auto &Base : RD->bases()) {
      QualType BaseType = Base.getType();
      if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
        if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          for (CXXMethodDecl *BaseMD : BaseCXX->methods()) {
            if (BaseMD->isVirtual() && BaseMD->getName() == MD->getName()) {
              AlreadyInVTable = true;
              break;
            }
          }
        }
      }
      if (AlreadyInVTable) break;
    }
    if (AlreadyInVTable) continue;

    llvm::Function *Fn = CGM.GetOrCreateFunctionDecl(MD);
    VTableEntries.push_back(
        Fn ? llvm::cast<llvm::Constant>(Fn)
           : llvm::ConstantPointerNull::get(
                 llvm::PointerType::get(CGM.getLLVMContext(), 0)));
  }

  auto *VTableTy = llvm::ArrayType::get(
      llvm::PointerType::get(CGM.getLLVMContext(), 0), VTableEntries.size());

  auto *VTableInit = llvm::ConstantArray::get(VTableTy, VTableEntries);

  auto *GV = new llvm::GlobalVariable(
      *CGM.getModule(), VTableTy, true, llvm::GlobalValue::ExternalLinkage,
      VTableInit, CGM.getMangler().getVTableName(RD));

  VTables[RD] = GV;
  return GV;
}

llvm::ArrayType *CGCXX::GetVTableType(CXXRecordDecl *RD) {
  if (!RD) return nullptr;
  unsigned NumEntries = 2; // offset-to-top + RTTI

  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        for (CXXMethodDecl *MD : BaseCXX->methods()) {
          if (MD->isVirtual()) ++NumEntries;
        }
      }
    }
  }
  for (CXXMethodDecl *MD : RD->methods()) {
    if (!MD->isVirtual()) continue;
    bool InBase = false;
    for (const auto &Base : RD->bases()) {
      QualType BaseType = Base.getType();
      if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
        if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          for (CXXMethodDecl *BaseMD : BaseCXX->methods()) {
            if (BaseMD->isVirtual() && BaseMD->getName() == MD->getName()) {
              InBase = true;
              break;
            }
          }
        }
      }
      if (InBase) break;
    }
    if (!InBase) ++NumEntries;
  }

  return llvm::ArrayType::get(llvm::PointerType::get(CGM.getLLVMContext(), 0),
                              NumEntries);
}

unsigned CGCXX::GetVTableIndex(CXXMethodDecl *MD) {
  if (!MD) return 0;
  CXXRecordDecl *RD = MD->getParent();
  if (!RD) return 0;

  unsigned Idx = 2; // 跳过 offset-to-top + RTTI

  // 先数基类虚函数
  for (const auto &Base : RD->bases()) {
    QualType BaseType = Base.getType();
    if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        for (CXXMethodDecl *BaseMD : BaseCXX->methods()) {
          if (!BaseMD->isVirtual()) continue;
          if (MD->getName() == BaseMD->getName()) return Idx;
          ++Idx;
        }
      }
    }
  }

  // 自身虚函数
  for (CXXMethodDecl *M : RD->methods()) {
    if (!M->isVirtual()) continue;
    bool InBase = false;
    for (const auto &Base : RD->bases()) {
      QualType BaseType = Base.getType();
      if (auto *RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
        if (auto *BaseCXX = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          for (CXXMethodDecl *BaseMD : BaseCXX->methods()) {
            if (BaseMD->isVirtual() && BaseMD->getName() == M->getName()) {
              InBase = true;
              break;
            }
          }
        }
      }
      if (InBase) break;
    }
    if (InBase) continue;
    if (M == MD) return Idx;
    ++Idx;
  }

  return 2;
}

llvm::Value *CGCXX::EmitVirtualCall(CodeGenFunction &CGF, CXXMethodDecl *MD,
                                      llvm::Value *This,
                                      llvm::ArrayRef<llvm::Value *> Args) {
  if (!MD || !This) return nullptr;
  CXXRecordDecl *RD = MD->getParent();
  if (!RD) return nullptr;

  // 1. 从对象头部加载 vptr
  llvm::StructType *ClassTy = CGM.getTypes().GetRecordType(RD);
  llvm::Value *VTablePtrAddr = CGF.getBuilder().CreateStructGEP(
      ClassTy, This, 0, "vtable.ptr");
  llvm::Value *VTable = CGF.getBuilder().CreateLoad(
      llvm::PointerType::get(CGM.getLLVMContext(), 0), VTablePtrAddr,
      "vtable");

  // 2. 计算索引
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
                                 CXXRecordDecl *RD) {
  if (!This || !RD) return;

  llvm::GlobalVariable *VTable = EmitVTable(RD);
  if (!VTable) return;

  llvm::StructType *ClassTy = CGM.getTypes().GetRecordType(RD);
  if (!ClassTy || ClassTy->getNumElements() == 0) return;

  // vptr 是结构体的第一个元素
  llvm::Value *VTablePtrAddr = CGF.getBuilder().CreateStructGEP(
      ClassTy, This, 0, "vtable.ptr");

  // vtable 全局变量的地址
  llvm::Value *VTableAddr = CGF.getBuilder().CreateInBoundsGEP(
      VTable->getValueType(), VTable,
      {llvm::ConstantInt::get(llvm::Type::getInt64Ty(CGM.getLLVMContext()), 0),
       llvm::ConstantInt::get(llvm::Type::getInt32Ty(CGM.getLLVMContext()), 0)},
      "vtable.addr");

  CGF.getBuilder().CreateStore(VTableAddr, VTablePtrAddr);
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

  uint64_t Offset = GetBaseOffset(Derived, Base);
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
                                    Expr *Init) {
  if (!Class || !This || !Field) return;

  llvm::StructType *StructTy = CGM.getTypes().GetRecordType(Class);
  unsigned FieldIdx = CGM.getTypes().GetFieldIndex(Field);
  llvm::Type *FieldLLVMTy = CGM.getTypes().ConvertType(Field->getType());

  llvm::Value *FieldPtr = CGF.getBuilder().CreateStructGEP(
      StructTy, This, FieldIdx, Field->getName());

  if (Init) {
    if (auto *ConstructExpr = llvm::dyn_cast<CXXConstructExpr>(Init)) {
      llvm::SmallVector<llvm::Value *, 4> Args;
      Args.push_back(FieldPtr);
      for (Expr *Arg : ConstructExpr->getArgs()) {
        llvm::Value *ArgVal = CGF.EmitExpr(Arg);
        if (ArgVal) Args.push_back(ArgVal);
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
                  CGF.getBuilder().CreateCall(CtorFn, Args, "field.ctor");
                  return;
                }
              }
            }
          }
        }
      }
    }

    llvm::Value *InitVal = CGF.EmitExpr(Init);
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
                llvm::Value *BasePtr = EmitCastToBase(CGF, Class, This, BaseCXX);
                CGF.getBuilder().CreateCall(DtorFn, {BasePtr}, "base.dtor");
              }
              break;
            }
          }
        }
      }
    }
  }
}
