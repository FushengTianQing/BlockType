//===--- ASTToIRConverter.cpp - AST to IR Conversion Framework -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/ASTToIRConverter.h"

#include "blocktype/Frontend/IREmitExpr.h"
#include "blocktype/Frontend/IREmitStmt.h"
#include "blocktype/Frontend/IREmitCXX.h"
#include "blocktype/Frontend/IRConstantEmitter.h"

#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRModule.h"

namespace blocktype {
namespace frontend {

//===----------------------------------------------------------------------===//
// Construction / Destruction
//===----------------------------------------------------------------------===//

ASTToIRConverter::ASTToIRConverter(ir::IRContext& IRCtx,
                                   ir::IRTypeContext& TypeCtx,
                                   const ir::TargetLayout& Layout,
                                   DiagnosticsEngine& Diags)
  : IRCtx_(IRCtx), TypeCtx_(TypeCtx), Layout_(Layout), Diags_(Diags),
    TypeMapper_(std::make_unique<IRTypeMapper>(TypeCtx, Layout, Diags)) {}

ASTToIRConverter::~ASTToIRConverter() {
  delete ExprEmitter_;
  delete StmtEmitter_;
  delete CXXEmitter_;
  delete ConstEmitter_;
}

void ASTToIRConverter::initializeEmitters() {
  // Sub-emitters are stubs for now. B.5-B.9 will provide real implementations.
  ExprEmitter_ = new IREmitExpr(*this);
  StmtEmitter_ = new IREmitStmt(*this);
  CXXEmitter_ = new IREmitCXX(*this);
  ConstEmitter_ = new IRConstantEmitter(*this);
}

//===----------------------------------------------------------------------===//
// convert() - Main entry point
//===----------------------------------------------------------------------===//

ir::IRConversionResult ASTToIRConverter::convert(TranslationUnitDecl* TU) {
  // Step 1: Create IRModule
  TheModule_ = std::make_unique<ir::IRModule>(
    "input",    // Module name (source file name in real usage)
    TypeCtx_,
    Layout_.getTriple(),
    ""          // DataLayout string (empty = use TargetLayout)
  );

  // Step 2: Create IRBuilder
  Builder_ = std::make_unique<ir::IRBuilder>(IRCtx_);

  // Step 3: Initialize sub-emitters
  initializeEmitters();

  // Step 4: Iterate over top-level declarations
  for (Decl* D : TU->decls()) {
    if (auto* FD = llvm::dyn_cast<FunctionDecl>(D)) {
      emitFunction(FD);
    } else if (auto* VD = llvm::dyn_cast<VarDecl>(D)) {
      if (VD->hasGlobalStorage()) {
        emitGlobalVar(VD);
      }
    } else if (auto* RD = llvm::dyn_cast<CXXRecordDecl>(D)) {
      if (RD->isCompleteDefinition() && RD->hasVTable()) {
        // Delegate to IREmitCXX for vtable generation (B.9)
        // For now, CXXEmitter_->EmitVTable(RD) is a no-op stub
        CXXEmitter_->EmitVTable(RD);
      }
    }
    // Other decl types (TypedefDecl, EnumDecl, etc.) are ignored at IR level.
  }

  // Step 5: Transfer module to result
  ir::IRConversionResult Result(std::move(TheModule_));
  return Result;
}

//===----------------------------------------------------------------------===//
// emitFunction
//===----------------------------------------------------------------------===//

ir::IRFunction* ASTToIRConverter::emitFunction(FunctionDecl* FD) {
  // Check if already emitted
  if (auto* Existing = getFunction(FD))
    return Existing;

  // Map return type
  ir::IRType* RetTy = TypeMapper_->mapType(FD->getType());

  // Build parameter types
  ir::SmallVector<ir::IRType*, 8> ParamTypes;
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    ParmVarDecl* PVD = FD->getParamDecl(i);
    ir::IRType* ParamTy = TypeMapper_->mapType(PVD->getType());
    ParamTypes.push_back(ParamTy);
  }

  // Create function type
  ir::IRFunctionType* FnTy = TypeCtx_.getFunctionType(RetTy, std::move(ParamTypes));

  // Create or get the function in the module
  auto FDName = FD->getName();
  ir::IRFunction* IRFn = TheModule_->getOrInsertFunction(
    ir::StringRef(FDName.data(), FDName.size()), FnTy);

  // Record in Functions map
  Functions_[FD] = IRFn;

  // If the function has a body, convert it
  if (Stmt* Body = FD->getBody()) {
    // Create entry basic block
    ir::IRBasicBlock* EntryBB = IRFn->addBasicBlock("entry");
    Builder_->setInsertPoint(EntryBB);

    // Clear local scope for new function
    clearLocalScope();

    // Emit parameters as allocas
    // IRArgument now inherits IRValue (B.5), so createStore works.
    // Map each ParmVarDecl to an alloca, and store the argument value into it.
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      ParmVarDecl* PVD = FD->getParamDecl(i);
      ir::IRType* ParamTy = TypeMapper_->mapType(PVD->getType());
      auto PName = PVD->getName();
      ir::IRValue* Alloca = Builder_->createAlloca(ParamTy,
        ir::StringRef(PName.data(), PName.size()));
      setDeclValue(PVD, Alloca);

      // Store the argument into the alloca
      ir::IRArgument* Arg = IRFn->getArg(i);
      Builder_->createStore(Arg, Alloca);
    }

    // Delegate body emission to IREmitStmt (B.6)
    if (auto* CS = llvm::dyn_cast<CompoundStmt>(Body)) {
      for (Stmt* S : CS->getBody()) {
        StmtEmitter_->Emit(S);
      }
    }

    // Ensure terminator: check the current insert block (not necessarily the last BB)
    ir::IRBasicBlock* CurBB = Builder_->getInsertBlock();
    ir::IRInstruction* Term = CurBB ? CurBB->getTerminator() : nullptr;
    if (!Term) {
      if (RetTy->isVoid()) {
        Builder_->createRetVoid();
      } else {
        // Error recovery: return undef for non-void function without explicit return
        Builder_->createRet(ir::IRConstantUndef::get(IRCtx_, RetTy));
      }
    }
  }

  return IRFn;
}

//===----------------------------------------------------------------------===//
// emitGlobalVar
//===----------------------------------------------------------------------===//

ir::IRGlobalVariable* ASTToIRConverter::emitGlobalVar(VarDecl* VD) {
  // Check if already emitted
  if (auto* Existing = getGlobalVar(VD))
    return Existing;

  // Map type
  ir::IRType* Ty = TypeMapper_->mapType(VD->getType());

  // Create global variable
  auto VDName = VD->getName();
  ir::IRGlobalVariable* GV = TheModule_->getOrInsertGlobal(
    ir::StringRef(VDName.data(), VDName.size()), Ty);

  // Handle initializer
  if (Expr* Init = VD->getInit()) {
    // TODO: Delegate to ConstEmitter_->emit(Init) in B.8
    // For now, set undef as initializer as error recovery
    GV->setInitializer(ir::IRConstantUndef::get(IRCtx_, Ty));
  }

  // Record in GlobalVars map
  GlobalVars_[VD] = GV;
  // Note: DeclValues_ map uses IRValue*, and IRGlobalVariable is not an IRValue.
  // This will be addressed when the IR hierarchy is unified in a later phase.

  return GV;
}

//===----------------------------------------------------------------------===//
// C++ converters (stubs for B.9)
//===----------------------------------------------------------------------===//

void ASTToIRConverter::convertCXXConstructor(CXXConstructorDecl* Ctor,
                                              ir::IRFunction* IRFn) {
  // TODO: B.9 (IREmitCXX) implementation
  (void)Ctor;
  (void)IRFn;
}

void ASTToIRConverter::convertCXXDestructor(CXXDestructorDecl* Dtor,
                                             ir::IRFunction* IRFn) {
  // TODO: B.9 (IREmitCXX) implementation
  (void)Dtor;
  (void)IRFn;
}

ir::IRValue* ASTToIRConverter::convertCXXConstructExpr(CXXConstructExpr* CCE) {
  // TODO: B.9 (IREmitCXX) implementation
  (void)CCE;
  return nullptr;
}

ir::IRValue* ASTToIRConverter::convertVirtualCall(CXXMemberCallExpr* MCE) {
  // TODO: B.9 (IREmitCXX) implementation
  (void)MCE;
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Error recovery
//===----------------------------------------------------------------------===//

ir::IRValue* ASTToIRConverter::emitErrorPlaceholder(ir::IRType* T) {
  return ir::IRConstantUndef::get(IRCtx_, T);
}

ir::IRType* ASTToIRConverter::emitErrorType() {
  return TypeCtx_.getOpaqueType("error");
}

void ASTToIRConverter::emitConversionError(DiagID ID, SourceLocation Loc,
                                            ir::StringRef Msg) {
  Diags_.report(Loc, ID, std::string(Msg).c_str());
}

//===----------------------------------------------------------------------===//
// Declaration value tracking
//===----------------------------------------------------------------------===//

ir::IRValue* ASTToIRConverter::getDeclValue(const Decl* D) const {
  auto It = DeclValues_.find(D);
  if (It != DeclValues_.end()) {
    return (*It).second;
  }
  return nullptr;
}

void ASTToIRConverter::setDeclValue(const Decl* D, ir::IRValue* V) {
  DeclValues_[D] = V;
}

ir::IRFunction* ASTToIRConverter::getFunction(const FunctionDecl* FD) const {
  auto It = Functions_.find(FD);
  if (It != Functions_.end()) {
    return (*It).second;
  }
  return nullptr;
}

ir::IRGlobalVariable* ASTToIRConverter::getGlobalVar(const VarDecl* VD) const {
  auto It = GlobalVars_.find(VD);
  if (It != GlobalVars_.end()) {
    return (*It).second;
  }
  return nullptr;
}

void ASTToIRConverter::setLocalDecl(const VarDecl* VD, ir::IRValue* V) {
  LocalDecls_[VD] = V;
}

ir::IRValue* ASTToIRConverter::getLocalDecl(const VarDecl* VD) const {
  auto It = LocalDecls_.find(VD);
  if (It != LocalDecls_.end()) {
    return (*It).second;
  }
  return nullptr;
}

void ASTToIRConverter::clearLocalScope() {
  LocalDecls_.clear();
}

} // namespace frontend
} // namespace blocktype
