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

  // === Phase 1: 基类初始化 ===
  // 先处理初始化列表中的基类初始化
  llvm::SmallVector<const CXXRecordDecl *, 4> InitializedBases;

  for (CXXCtorInitializer *Init : Ctor->initializers()) {
    if (Init->isBaseInitializer()) {
      llvm::StringRef BaseName = Init->getMemberName();
      for (const auto &BaseSpec : Class->bases()) {
        QualType BT = BaseSpec.getType();
        if (auto *RT = llvm::dyn_cast<RecordType>(BT.getTypePtr())) {
          if (auto *BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
            if (BaseRD->getName() == BaseName || BaseName.empty()) {
              InitializedBases.push_back(BaseRD);

              llvm::Value *BasePtr = EmitCastToBase(CGF, This, BaseRD);

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
          llvm::Value *BasePtr = EmitCastToBase(CGF, This, BaseRD);
          llvm::StructType *BaseTy = CGM.getTypes().GetRecordType(BaseRD);
          llvm::Value *TypedPtr = CGF.getBuilder().CreateBitCast(
              BasePtr, llvm::PointerType::get(BaseTy, 0), "base.ptr");
          CGF.getBuilder().CreateStore(
              llvm::Constant::getNullValue(BaseTy), TypedPtr);
        }
      }
    }
  }

  // === Phase 2: vptr 初始化 ===
  if (hasVirtualFunctionsInHierarchy(Class)) {
    InitializeVTablePtr(CGF, This, Class);
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

  // RTTI 指针占位
  VTableEntries.push_back(llvm::ConstantPointerNull::get(
      llvm::PointerType::get(CGM.getLLVMContext(), 0)));

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
      VTableInit, "_ZTV" + RD->getName());

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
// 继承
//===----------------------------------------------------------------------===//

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

llvm::Value *CGCXX::EmitCastToBase(CodeGenFunction &CGF, llvm::Value *DerivedPtr,
                                    CXXRecordDecl *Base) {
  if (!DerivedPtr || !Base) return DerivedPtr;

  // 单继承：偏移为 0，无需调整
  // 多继承：需要根据 BaseOffsetCache 计算偏移
  // 简化实现：偏移为 0
  return DerivedPtr;
}

llvm::Value *CGCXX::EmitCastToDerived(CodeGenFunction &CGF,
                                        llvm::Value *BasePtr,
                                        CXXRecordDecl *Derived) {
  if (!BasePtr || !Derived) return BasePtr;
  return BasePtr;
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

  llvm::Value *BasePtr = EmitCastToBase(CGF, This, BaseRD);

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
                llvm::Value *BasePtr = EmitCastToBase(CGF, This, BaseCXX);
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
