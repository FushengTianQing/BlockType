//===--- CodeGenModule.cpp - Module-level CodeGen -------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/CodeGen/CodeGenConstant.h"
#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/CodeGen/CGCXX.h"
#include "blocktype/CodeGen/TargetInfo.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/Compiler.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// 构造 / 析构
//===----------------------------------------------------------------------===//

CodeGenModule::CodeGenModule(ASTContext &Ctx, llvm::LLVMContext &LLVMCtx,
                             llvm::StringRef ModuleName,
                             llvm::StringRef TargetTriple)
    : Context(Ctx), LLVMCtx(LLVMCtx),
      TheModule(std::make_unique<llvm::Module>(ModuleName, LLVMCtx)) {
  // 设置目标三元组和数据布局
  TheModule->setTargetTriple(TargetTriple);

  // 初始化子组件（顺序重要：TargetInfo 先于 Types）
  Target = std::make_unique<class TargetInfo>(TargetTriple);
  TheModule->setDataLayout(Target->getDataLayout());
  Types = std::make_unique<CodeGenTypes>(*this);
  Constants = std::make_unique<CodeGenConstant>(*this);
  CXX = std::make_unique<CGCXX>(*this);
}

CodeGenModule::~CodeGenModule() = default;

const llvm::DataLayout &CodeGenModule::getDataLayout() const {
  return TheModule->getDataLayout();
}

//===----------------------------------------------------------------------===//
// 代码生成入口
//===----------------------------------------------------------------------===//

void CodeGenModule::EmitTranslationUnit(TranslationUnitDecl *TU) {
  if (!TU) return;

  // 第一遍：生成所有声明（前向引用）
  for (Decl *D : TU->decls()) {
    if (auto *FD = llvm::dyn_cast<FunctionDecl>(D)) {
      // 创建函数声明（不生成函数体）
      GetOrCreateFunctionDecl(FD);
    } else if (auto *VD = llvm::dyn_cast<VarDecl>(D)) {
      // 全局变量加入延迟队列
      DeferredGlobalVars.push_back(VD);
    } else if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(D)) {
      // 前向声明 struct 类型 + 计算类布局
      getTypes().GetRecordType(RD);
      EmitClassLayout(RD);
    } else if (auto *RD = llvm::dyn_cast<RecordDecl>(D)) {
      getTypes().GetRecordType(RD);
    } else if (auto *ED = llvm::dyn_cast<EnumDecl>(D)) {
      // 枚举类型 — ConvertType 会处理
      (void)ED;
    }
  }

  // 发射延迟定义（全局变量定义）
  EmitDeferred();

  // 发射所有需要的虚函数表
  EmitVTables();

  // 第二遍：生成函数体
  for (Decl *D : TU->decls()) {
    if (auto *FD = llvm::dyn_cast<FunctionDecl>(D)) {
      if (FD->getBody()) {
        EmitFunction(FD);
      }
    }
  }

  // 发射全局构造/析构
  EmitGlobalCtorDtors();
}

void CodeGenModule::EmitDeferred() {
  // 发射全局变量定义
  for (VarDecl *VD : DeferredGlobalVars) {
    EmitGlobalVar(VD);
  }
  DeferredGlobalVars.clear();
}

//===----------------------------------------------------------------------===//
// 全局变量生成
//===----------------------------------------------------------------------===//

llvm::GlobalVariable *CodeGenModule::EmitGlobalVar(VarDecl *VD) {
  if (!VD) return nullptr;

  // 检查是否已生成
  if (auto *Existing = GetGlobalVar(VD))
    return Existing;

  llvm::Type *Ty = getTypes().ConvertType(VD->getType());
  if (!Ty) return nullptr;

  // 计算初始值
  llvm::Constant *Init = nullptr;
  if (Expr *InitExpr = VD->getInit()) {
    Init = getConstants().EmitConstantForType(InitExpr, VD->getType());
  }
  if (!Init) {
    Init = getConstants().EmitZeroValue(VD->getType());
  }

  // 创建全局变量
  // static 变量使用 InternalLinkage，其他使用 ExternalLinkage
  auto Linkage = VD->isStatic()
                     ? llvm::GlobalValue::InternalLinkage
                     : llvm::GlobalValue::ExternalLinkage;
  bool IsConstant = VD->isConstexpr() ||
                    VD->getType().isConstQualified();

  auto *GV = new llvm::GlobalVariable(
      *TheModule, Ty, IsConstant, Linkage, Init,
      VD->getName());

  // 设置对齐
  GV->setAlignment(llvm::Align(getTarget().getTypeAlign(VD->getType())));

  // 注册映射
  GlobalValues[VD] = GV;

  return GV;
}

llvm::GlobalVariable *CodeGenModule::GetGlobalVar(VarDecl *VD) {
  auto It = GlobalValues.find(VD);
  if (It != GlobalValues.end())
    return llvm::dyn_cast_or_null<llvm::GlobalVariable>(It->second);
  return nullptr;
}

//===----------------------------------------------------------------------===//
// 函数生成
//===----------------------------------------------------------------------===//

llvm::Function *CodeGenModule::EmitFunction(FunctionDecl *FD) {
  if (!FD) return nullptr;

  llvm::Function *Fn = GetOrCreateFunctionDecl(FD);
  if (!Fn) return nullptr;

  // 如果没有函数体，只生成声明
  if (!FD->getBody()) return Fn;

  // 如果函数体已经生成过（检查是否有基本块）
  if (!Fn->empty()) return Fn;

  // 构造函数分派到 CGCXX::EmitConstructor
  if (auto *Ctor = llvm::dyn_cast<CXXConstructorDecl>(FD)) {
    getCXX().EmitConstructor(Ctor, Fn);
    return Fn;
  }

  // 析构函数分派到 CGCXX::EmitDestructor
  if (auto *Dtor = llvm::dyn_cast<CXXDestructorDecl>(FD)) {
    getCXX().EmitDestructor(Dtor, Fn);
    return Fn;
  }

  // 使用 CodeGenFunction 生成函数体
  CodeGenFunction CGF(*this);
  CGF.EmitFunctionBody(FD, Fn);

  return Fn;
}

llvm::Function *CodeGenModule::GetFunction(FunctionDecl *FD) {
  auto It = GlobalValues.find(FD);
  if (It != GlobalValues.end())
    return llvm::dyn_cast_or_null<llvm::Function>(It->second);
  return nullptr;
}

llvm::Function *CodeGenModule::GetOrCreateFunctionDecl(FunctionDecl *FD) {
  if (!FD) return nullptr;

  // 检查缓存
  if (auto *Existing = GetFunction(FD))
    return Existing;

  // 获取函数类型
  llvm::FunctionType *FTy = getTypes().GetFunctionTypeForDecl(FD);
  if (!FTy) return nullptr;

  // 创建 LLVM 函数
  // static 函数使用 InternalLinkage，其他使用 ExternalLinkage
  auto Linkage = llvm::Function::ExternalLinkage;
  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
    // 成员函数不使用 static linkage 语义
  } else if (FD->isInline()) {
    // inline 函数使用 LinkOnceODRLinkage（允许跨 TU 合并）
    Linkage = llvm::Function::LinkOnceODRLinkage;
  }
  // Note: 普通函数的 static 判断需要 Sema 的 StorageClass 信息

  llvm::Function *Fn = llvm::Function::Create(
      FTy, Linkage, FD->getName(), TheModule.get());

  // 设置参数名
  unsigned Idx = 0;
  for (auto &Arg : Fn->args()) {
    if (Idx < FD->getNumParams()) {
      ParmVarDecl *PVD = FD->getParamDecl(Idx);
      Arg.setName(PVD->getName());
    }
    ++Idx;
  }

  // 设置函数属性
  if (FD->isInline()) {
    Fn->addFnAttr(llvm::Attribute::AlwaysInline);
  }
  if (FD->hasNoexceptSpec() && FD->getNoexceptValue()) {
    Fn->setDoesNotThrow();
  }

  // 注册映射
  GlobalValues[FD] = Fn;

  return Fn;
}

//===----------------------------------------------------------------------===//
// C++ 特有生成
//===----------------------------------------------------------------------===//

void CodeGenModule::EmitVTable(CXXRecordDecl *RD) {
  if (!RD) return;
  getCXX().EmitVTable(RD);
}

void CodeGenModule::EmitClassLayout(CXXRecordDecl *RD) {
  if (!RD) return;
  getCXX().ComputeClassLayout(RD);

  // 如果类有虚函数，记录需要生成 vtable
  bool HasVirtual = false;
  for (CXXMethodDecl *MD : RD->methods()) {
    if (MD->isVirtual()) { HasVirtual = true; break; }
  }
  if (HasVirtual) {
    VTableClasses.push_back(RD);
  }
}

void CodeGenModule::EmitVTables() {
  for (CXXRecordDecl *RD : VTableClasses) {
    getCXX().EmitVTable(RD);
  }
  VTableClasses.clear();
}

//===----------------------------------------------------------------------===//
// 全局构造/析构
//===----------------------------------------------------------------------===//

void CodeGenModule::AddGlobalCtor(FunctionDecl *FD, int Priority) {
  GlobalCtors.emplace_back(FD, Priority);
}

void CodeGenModule::AddGlobalDtor(FunctionDecl *FD, int Priority) {
  GlobalDtors.emplace_back(FD, Priority);
}

void CodeGenModule::EmitGlobalCtorDtors() {
  // 生成 llvm.global_ctors
  if (!GlobalCtors.empty()) {
    llvm::SmallVector<llvm::Constant *, 8> Ctors;
    llvm::StructType *EntryTy = llvm::StructType::get(
        LLVMCtx,
        {llvm::Type::getInt32Ty(LLVMCtx),
         llvm::PointerType::get(LLVMCtx, 0),
         llvm::PointerType::get(LLVMCtx, 0)});

    for (auto &[FD, Priority] : GlobalCtors) {
      llvm::Function *Fn = GetOrCreateFunctionDecl(FD);
      if (!Fn) continue;

      Ctors.push_back(llvm::ConstantStruct::get(
          EntryTy,
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(LLVMCtx), Priority),
           Fn,
           llvm::ConstantPointerNull::get(llvm::PointerType::get(LLVMCtx, 0))}));
    }

    if (!Ctors.empty()) {
      auto *AT = llvm::ArrayType::get(EntryTy, Ctors.size());
      new llvm::GlobalVariable(*TheModule, AT, true,
                               llvm::GlobalValue::AppendingLinkage,
                               llvm::ConstantArray::get(AT, Ctors),
                               "llvm.global_ctors");
    }
  }

  // 生成 llvm.global_dtors
  if (!GlobalDtors.empty()) {
    llvm::SmallVector<llvm::Constant *, 8> Dtors;
    llvm::StructType *EntryTy = llvm::StructType::get(
        LLVMCtx,
        {llvm::Type::getInt32Ty(LLVMCtx),
         llvm::PointerType::get(LLVMCtx, 0),
         llvm::PointerType::get(LLVMCtx, 0)});

    for (auto &[FD, Priority] : GlobalDtors) {
      llvm::Function *Fn = GetOrCreateFunctionDecl(FD);
      if (!Fn) continue;

      Dtors.push_back(llvm::ConstantStruct::get(
          EntryTy,
          {llvm::ConstantInt::get(llvm::Type::getInt32Ty(LLVMCtx), Priority),
           Fn,
           llvm::ConstantPointerNull::get(llvm::PointerType::get(LLVMCtx, 0))}));
    }

    if (!Dtors.empty()) {
      auto *AT = llvm::ArrayType::get(EntryTy, Dtors.size());
      new llvm::GlobalVariable(*TheModule, AT, true,
                               llvm::GlobalValue::AppendingLinkage,
                               llvm::ConstantArray::get(AT, Dtors),
                               "llvm.global_dtors");
    }
  }
}
