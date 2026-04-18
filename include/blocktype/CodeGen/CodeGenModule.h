//===--- CodeGenModule.h - Module-level CodeGen --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the CodeGenModule class for managing LLVM IR generation
// at the translation unit level.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include <memory>

namespace blocktype {

class CodeGenTypes;
class CodeGenConstant;
class CodeGenFunction;
class CGCXX;
class TargetInfo;

/// CodeGenModule — 模块级代码生成引擎。
///
/// 职责（参照 Clang CodeGenModule）：
/// 1. 管理 LLVM Module（全局变量、函数声明、元数据）
/// 2. 协调所有代码生成子组件（Types、Constant、Function、CXX）
/// 3. 维护 Decl → llvm::GlobalValue 的映射表
/// 4. 处理全局初始化（全局构造/析构函数列表）
/// 5. 管理目标平台信息（TargetInfo）
class CodeGenModule {
  ASTContext &Context;
  llvm::LLVMContext &LLVMCtx;
  std::unique_ptr<llvm::Module> TheModule;
  std::unique_ptr<CodeGenTypes> Types;
  std::unique_ptr<CodeGenConstant> Constants;
  std::unique_ptr<CGCXX> CXX;
  std::unique_ptr<TargetInfo> Target;

  /// Decl → llvm::GlobalValue 映射
  llvm::DenseMap<const Decl *, llvm::GlobalValue *> GlobalValues;

  /// 全局变量延迟发射队列
  llvm::SmallVector<VarDecl *, 16> DeferredGlobalVars;

  /// 全局构造/析构函数
  llvm::SmallVector<std::pair<FunctionDecl *, int>, 4> GlobalCtors;
  llvm::SmallVector<std::pair<FunctionDecl *, int>, 4> GlobalDtors;

  /// 需要生成 vtable 的 CXXRecordDecl 集合
  llvm::SmallVector<CXXRecordDecl *, 8> VTableClasses;

public:
  CodeGenModule(ASTContext &Ctx, llvm::LLVMContext &LLVMCtx,
                llvm::StringRef ModuleName, llvm::StringRef TargetTriple);
  ~CodeGenModule();

  // Non-copyable
  CodeGenModule(const CodeGenModule &) = delete;
  CodeGenModule &operator=(const CodeGenModule &) = delete;

  //===------------------------------------------------------------------===//
  // 代码生成入口
  //===------------------------------------------------------------------===//

  /// 生成整个翻译单元的 LLVM IR。
  void EmitTranslationUnit(TranslationUnitDecl *TU);

  /// 发射所有延迟定义。
  void EmitDeferred();

  //===------------------------------------------------------------------===//
  // 全局变量生成
  //===------------------------------------------------------------------===//

  /// 生成全局变量的 LLVM IR。
  llvm::GlobalVariable *EmitGlobalVar(VarDecl *VD);

  /// 获取已生成的全局变量（或 nullptr）。
  llvm::GlobalVariable *GetGlobalVar(VarDecl *VD);

  //===------------------------------------------------------------------===//
  // 函数生成
  //===------------------------------------------------------------------===//

  /// 生成函数的完整 LLVM IR。
  llvm::Function *EmitFunction(FunctionDecl *FD);

  /// 获取函数的 llvm::Function（已生成或仅声明）。
  llvm::Function *GetFunction(FunctionDecl *FD);

  /// 获取或创建函数声明（不生成函数体）。
  llvm::Function *GetOrCreateFunctionDecl(FunctionDecl *FD);

  //===------------------------------------------------------------------===//
  // C++ 特有生成
  //===------------------------------------------------------------------===//

  /// 生成类的虚函数表。
  void EmitVTable(CXXRecordDecl *RD);

  /// 生成所有需要的虚函数表。
  void EmitVTables();

  /// 生成类的布局信息。
  void EmitClassLayout(CXXRecordDecl *RD);

  //===------------------------------------------------------------------===//
  // 访问器
  //===------------------------------------------------------------------===//

  llvm::Module *getModule() const { return TheModule.get(); }
  CodeGenTypes &getTypes() const { return *Types; }
  CodeGenConstant &getConstants() const { return *Constants; }
  CGCXX &getCXX() const { return *CXX; }
  TargetInfo &getTarget() const { return *Target; }
  ASTContext &getASTContext() const { return Context; }
  llvm::LLVMContext &getLLVMContext() const { return LLVMCtx; }

  /// 获取模块的数据布局。
  const llvm::DataLayout &getDataLayout() const;

  //===------------------------------------------------------------------===//
  // 全局构造/析构
  //===------------------------------------------------------------------===//

  /// 注册全局构造函数。
  void AddGlobalCtor(FunctionDecl *FD, int Priority = 65535);

  /// 注册全局析构函数。
  void AddGlobalDtor(FunctionDecl *FD, int Priority = 65535);

  /// 生成 llvm.global_ctors 和 llvm.global_dtors。
  void EmitGlobalCtorDtors();
};

} // namespace blocktype
