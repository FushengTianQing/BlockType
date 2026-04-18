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
#include <optional>

namespace blocktype {

class CodeGenTypes;
class CodeGenConstant;
class CodeGenFunction;
class CGCXX;
class CGDebugInfo;
class Mangler;
class TargetInfo;

/// CodeGenModule — 模块级代码生成引擎。
///
/// 职责（参照 Clang CodeGenModule）：
/// 1. 管理 LLVM Module（全局变量、函数声明、元数据）
/// 2. 协调所有代码生成子组件（Types、Constant、Function、CXX）
/// 3. 维护 Decl → llvm::GlobalValue 的映射表
/// 4. 处理全局初始化（全局构造/析构函数列表）
/// 5. 管理目标平台信息（TargetInfo）
/// 6. 处理函数/变量属性（visibility, weak, dllimport/dllexport）
/// 7. 区分常量初始化和动态初始化

//===----------------------------------------------------------------------===//
// GlobalDeclAttributes — 全局符号属性（参照 Clang GlobalDecl/CodeGenModule）
//===----------------------------------------------------------------------===//

/// GlobalDeclAttributes — 描述全局符号的附加属性。
struct GlobalDeclAttributes {
  bool IsWeak = false;           ///< __attribute__((weak))
  bool IsDLLImport = false;      ///< __attribute__((dllimport))
  bool IsDLLExport = false;      ///< __attribute__((dllexport))
  bool IsUsed = false;           ///< __attribute__((used))
  bool IsDeprecated = false;     ///< [[deprecated]]
  bool IsHiddenVisibility = false; ///< __attribute__((visibility("hidden")))
  bool IsDefaultVisibility = true; ///< __attribute__((visibility("default")))
};

//===----------------------------------------------------------------------===//
// InitKind — 全局变量初始化分类
//===----------------------------------------------------------------------===//

/// InitKind - Distinguishes constant vs dynamic initialization.
enum class InitKind {
  ZeroInitialization,     ///< Zero-init (no initializer or = 0)
  ConstantInitialization, ///< Constant init (constexpr or constant expr)
  DynamicInitialization   ///< Dynamic init (runtime evaluation required)
};

class CodeGenModule {
  ASTContext &Context;
  llvm::LLVMContext &LLVMCtx;
  std::unique_ptr<llvm::Module> TheModule;
  std::unique_ptr<CodeGenTypes> Types;
  std::unique_ptr<CodeGenConstant> Constants;
  std::unique_ptr<CGCXX> CXX;
  std::unique_ptr<CGDebugInfo> DebugInfo;
  std::unique_ptr<Mangler> Mangle;
  std::unique_ptr<TargetInfo> Target;

  /// Decl → llvm::GlobalValue 映射
  llvm::DenseMap<const Decl *, llvm::GlobalValue *> GlobalValues;

  /// 全局变量延迟发射队列
  llvm::SmallVector<VarDecl *, 16> DeferredGlobalVars;

  /// 需要动态初始化的全局变量（常量初始化的变量直接在 EmitGlobalVar 中完成）
  llvm::SmallVector<VarDecl *, 16> DynamicInitVars;

  /// 全局构造/析构函数（通过 FunctionDecl）
  llvm::SmallVector<std::pair<FunctionDecl *, int>, 4> GlobalCtors;
  llvm::SmallVector<std::pair<FunctionDecl *, int>, 4> GlobalDtors;

  /// 直接使用 llvm::Function 的全局构造函数（用于动态初始化）
  llvm::SmallVector<llvm::Function *, 8> GlobalCtorsDirect;

  /// 需要生成 vtable 的 CXXRecordDecl 集合
  llvm::SmallVector<CXXRecordDecl *, 8> VTableClasses;

  /// 字符串字面量池（StringRef → GlobalVariable*），用于合并相同的字符串字面量。
  /// Clang 也有类似的 StringLiteralPool 机制。
  llvm::DenseMap<llvm::StringRef, llvm::GlobalVariable *> StringLiteralPool;

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

  /// Sema 后处理：遍历 AST，为 CXXNewExpr/CXXDeleteExpr 设置 ExprTy。
  /// Parser 直接创建 new/delete 节点时不经过 Sema，需要在此补设类型。
  void SemaPostProcessAST(TranslationUnitDecl *TU);

  /// Sema 后处理辅助：递归遍历 Stmt。
  void SemaVisitStmt(Stmt *S);

  /// Sema 后处理辅助：递归遍历 Expr。
  void SemaVisitExpr(Expr *E);

  //===------------------------------------------------------------------===//
  // 属性处理（参照 Clang CodeGenModule::getGlobalValueAttributes）
  //===------------------------------------------------------------------===//

  /// 收集全局声明上的属性（从 AST 或 Decl 属性）。
  GlobalDeclAttributes GetGlobalDeclAttributes(const Decl *D);

  /// 将属性应用到 llvm::GlobalValue。
  void ApplyGlobalValueAttributes(llvm::GlobalValue *GV,
                                  const GlobalDeclAttributes &Attrs);

  //===------------------------------------------------------------------===//
  // Linkage / Visibility
  //===------------------------------------------------------------------===//

  /// 计算函数的正确 Linkage。
  llvm::GlobalValue::LinkageTypes GetFunctionLinkage(const FunctionDecl *FD);

  /// 计算全局变量的正确 Linkage。
  llvm::GlobalValue::LinkageTypes GetVariableLinkage(const VarDecl *VD);

  /// 计算全局符号的 Visibility。
  llvm::GlobalValue::VisibilityTypes GetVisibility(const GlobalDeclAttributes &Attrs);

  //===------------------------------------------------------------------===//
  // 初始化分类
  //===------------------------------------------------------------------===//

  /// 判断全局变量的初始化类型。
  InitKind ClassifyGlobalInit(const VarDecl *VD);

  /// 发射动态初始化的全局变量。
  void EmitDynamicGlobalInit(VarDecl *VD);

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
  CGDebugInfo &getDebugInfo() const { return *DebugInfo; }
  Mangler &getMangler() const { return *Mangle; }
  TargetInfo &getTarget() const { return *Target; }
  ASTContext &getASTContext() const { return Context; }
  llvm::LLVMContext &getLLVMContext() const { return LLVMCtx; }

  /// 获取模块的数据布局。
  const llvm::DataLayout &getDataLayout() const;

  /// 获取字符串字面量池（用于 CodeGenConstant 合并相同字符串）。
  llvm::DenseMap<llvm::StringRef, llvm::GlobalVariable *> &getStringLiteralPool() {
    return StringLiteralPool;
  }

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
