//===--- ASTToIRConverter.h - AST to IR Conversion Framework ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ASTToIRConverter class, which drives the conversion
// from AST (TranslationUnitDecl) to IR (IRModule). It orchestrates the
// sub-emitters (IREmitExpr, IREmitStmt, IREmitCXX, IRConstantEmitter) which
// are implemented in subsequent tasks (B.5-B.9).
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_ASTTOIRCONVERTER_H
#define BLOCKTYPE_FRONTEND_ASTTOIRCONVERTER_H

#include <memory>

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/IRTypeMapper.h"
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRConversionResult.h"
#include "blocktype/IR/TargetLayout.h"

namespace blocktype {
namespace frontend {

// Forward declarations for sub-emitters (B.5-B.9)
class IREmitExpr;
class IREmitStmt;
class IREmitCXX;
class IRConstantEmitter;
class IRMangler;

/// ASTToIRConverter - Drives the top-level conversion from AST to IR.
///
/// Responsibilities:
/// 1. Creates an IRModule for the translation unit
/// 2. Iterates over top-level declarations (functions, globals, vtables)
/// 3. Delegates expression/statement/C++ specific conversion to sub-emitters
/// 4. Implements error recovery: on failure, emits a placeholder and continues
///
/// Lifecycle:
/// - Construct once per translation unit
/// - Call convert() to produce an IRConversionResult
/// - After convert(), the converter should not be reused
///
/// Thread safety: Not thread-safe. One instance per compilation thread.
class ASTToIRConverter {
  ir::IRContext& IRCtx_;
  ir::IRTypeContext& TypeCtx_;
  const ir::TargetLayout& Layout_;
  DiagnosticsEngine& Diags_;

  std::unique_ptr<ir::IRModule> TheModule_;
  std::unique_ptr<ir::IRBuilder> Builder_;
  std::unique_ptr<IRTypeMapper> TypeMapper_;

  /// Maps AST declarations to their corresponding IR values.
  ir::DenseMap<const Decl*, ir::IRValue*> DeclValues_;

  /// Maps VarDecls at global scope to IR global variables.
  ir::DenseMap<const VarDecl*, ir::IRGlobalVariable*> GlobalVars_;

  /// Maps FunctionDecls to IR functions.
  ir::DenseMap<const FunctionDecl*, ir::IRFunction*> Functions_;

  /// Maps VarDecls at local scope to IR values (alloca/load).
  ir::DenseMap<const VarDecl*, ir::IRValue*> LocalDecls_;

  // Sub-emitters (allocated in initializeEmitters(), B.5-B.9 fill in impl).
  // Using raw pointers; ownership managed via manual delete in destructor.
  // Will be converted to unique_ptr when sub-emitter headers are available.
  IREmitExpr* ExprEmitter_ = nullptr;
  IREmitStmt* StmtEmitter_ = nullptr;
  IREmitCXX* CXXEmitter_ = nullptr;
  IRConstantEmitter* ConstEmitter_ = nullptr;
  IRMangler* Mangler_ = nullptr;

public:
  /// Construct an ASTToIRConverter.
  ///
  /// \param IRCtx   IR context for allocating IR nodes.
  /// \param TypeCtx IR type context for creating types.
  /// \param Layout  Target layout for platform-dependent decisions.
  /// \param Diags   Diagnostics engine for error reporting.
  ASTToIRConverter(ir::IRContext& IRCtx,
                   ir::IRTypeContext& TypeCtx,
                   const ir::TargetLayout& Layout,
                   DiagnosticsEngine& Diags);

  /// Destructor. Cleans up sub-emitter objects.
  ~ASTToIRConverter();

  // Non-copyable, non-movable (references + DenseMap).
  ASTToIRConverter(const ASTToIRConverter&) = delete;
  ASTToIRConverter& operator=(const ASTToIRConverter&) = delete;
  ASTToIRConverter(ASTToIRConverter&&) = delete;
  ASTToIRConverter& operator=(ASTToIRConverter&&) = delete;

  /// Convert a translation unit AST into an IR module.
  ///
  /// \param TU The translation unit to convert.
  /// \returns An IRConversionResult. On success, isUsable() == true.
  ///          On error, contains error count and possibly a partial module.
  ir::IRConversionResult convert(TranslationUnitDecl* TU);

  // === Top-level declaration emitters ===

  /// Emit an IR function for the given FunctionDecl.
  /// Creates the function signature and body.
  ir::IRFunction* emitFunction(FunctionDecl* FD);

  /// Emit an IR global variable for the given VarDecl.
  ir::IRGlobalVariable* emitGlobalVar(VarDecl* VD);

  // === C++ specific converters (delegates to IREmitCXX, B.9) ===

  /// Convert a C++ constructor.
  void convertCXXConstructor(CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);

  /// Convert a C++ destructor.
  void convertCXXDestructor(CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);

  /// Convert a CXXConstructExpr.
  ir::IRValue* convertCXXConstructExpr(CXXConstructExpr* CCE);

  /// Convert a virtual method call.
  ir::IRValue* convertVirtualCall(CXXMemberCallExpr* MCE);

  // === Error recovery ===

  /// Emit a placeholder value for a failed expression conversion.
  /// Returns IRConstantUndef of the given type.
  ir::IRValue* emitErrorPlaceholder(ir::IRType* T);

  /// Emit an error type (IROpaqueType("error")) for failed type mapping.
  ir::IRType* emitErrorType();

  // === Accessors for sub-emitters ===

  IREmitExpr* getExprEmitter() { return ExprEmitter_; }
  IREmitStmt* getStmtEmitter() { return StmtEmitter_; }
  IREmitCXX* getCxxEmitter() { return CXXEmitter_; }
  IRMangler* getMangler() const { return Mangler_; }

  ir::IRBuilder& getBuilder() { return *Builder_; }
  ir::IRModule* getModule() const { return TheModule_.get(); }
  IRTypeMapper& getTypeMapper() { return *TypeMapper_; }
  ir::IRContext& getIRContext() { return IRCtx_; }
  ir::IRTypeContext& getTypeContext() { return TypeCtx_; }
  const ir::TargetLayout& getTargetLayout() const { return Layout_; }
  DiagnosticsEngine& getDiagnostics() { return Diags_; }

  // === Declaration value tracking ===

  ir::IRValue* getDeclValue(const Decl* D) const;
  void setDeclValue(const Decl* D, ir::IRValue* V);
  ir::IRFunction* getFunction(const FunctionDecl* FD) const;
  ir::IRGlobalVariable* getGlobalVar(const VarDecl* VD) const;
  void setLocalDecl(const VarDecl* VD, ir::IRValue* V);
  ir::IRValue* getLocalDecl(const VarDecl* VD) const;

  /// Clear local scope state (called between function bodies).
  void clearLocalScope();

private:
  /// Initialize sub-emitters. Called at the start of convert().
  void initializeEmitters();

  /// Emit diagnostics for IR conversion errors.
  void emitConversionError(DiagID ID, SourceLocation Loc, ir::StringRef Msg);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_ASTTOIRCONVERTER_H
