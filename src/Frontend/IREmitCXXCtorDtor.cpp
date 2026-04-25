//===--- IREmitCXXCtorDtor.cpp - Constructor/Destructor Emission -*- C++ -*-===//

#include "blocktype/Frontend/IREmitCXX.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IREmitExpr.h"
#include "blocktype/Frontend/IREmitStmt.h"
#include "blocktype/Frontend/IRTypeMapper.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRConstant.h"

namespace blocktype {
namespace frontend {

IREmitCXXCtorDtor::IREmitCXXCtorDtor(ASTToIRConverter& C) : Converter_(C) {}

//===----------------------------------------------------------------------===//
// EmitConstructor
//===----------------------------------------------------------------------===//

void IREmitCXXCtorDtor::EmitConstructor(const CXXConstructorDecl* Ctor,
                                         ir::IRFunction* IRFn) {
  if (!IRFn || !Ctor) return;

  auto& Builder = Converter_.getBuilder();
  auto* Class = Ctor->getParent();

  // Create entry basic block
  auto* EntryBB = IRFn->addBasicBlock("entry");
  Builder.setInsertPoint(EntryBB);

  // this pointer = first parameter
  ir::IRValue* This = IRFn->getArg(0);

  // 1. Initialize vptr if class has virtual functions
  if (Class && IREmitCXXLayout::hasVirtualFunctionsInHierarchy(Class)) {
    Converter_.getCxxEmitter()
        ->getVTableEmitter()
        .InitializeVTablePtr(This, Class);
  }

  // 2. Base class initializers
  for (const CXXCtorInitializer* Init : Ctor->initializers()) {
    if (Init->isBaseInitializer()) {
      EmitBaseInitializer(This, Init);
    }
  }

  // 3. Member initializers
  for (const CXXCtorInitializer* Init : Ctor->initializers()) {
    if (Init->isMemberInitializer()) {
      EmitMemberInitializer(This, Init);
    }
  }

  // 4. Delegating constructors
  for (const CXXCtorInitializer* Init : Ctor->initializers()) {
    if (Init->isDelegatingInitializer()) {
      EmitDelegatingConstructor(This, Init);
    }
  }

  // 5. Constructor body
  if (Ctor->getBody()) {
    if (auto* CS = llvm::dyn_cast<CompoundStmt>(Ctor->getBody())) {
      for (Stmt* S : CS->getBody()) {
        Converter_.getStmtEmitter()->Emit(S);
      }
    }
  }

  // 6. Return void
  Builder.createRetVoid();
}

//===----------------------------------------------------------------------===//
// EmitBaseInitializer
//===----------------------------------------------------------------------===//

void IREmitCXXCtorDtor::EmitBaseInitializer(ir::IRValue* This,
                                             const CXXCtorInitializer* Init) {
  if (!This || !Init) return;

  auto& Builder = Converter_.getBuilder();
  auto& TypeCtx = Converter_.getTypeContext();

  // Resolve base type to CXXRecordDecl
  QualType BaseType = Init->getBaseType();
  const CXXRecordDecl* BaseRD = nullptr;
  if (auto* RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
    BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl());
  }
  if (!BaseRD) return;

  // Compute base offset
  // We need the derived class — use the constructor's parent
  // (passed indirectly via the current context)
  // For now, evaluate initializer arguments
  auto Args = Init->getArguments();
  for (Expr* Arg : Args) {
    Converter_.getExprEmitter()->Emit(Arg);
  }
}

//===----------------------------------------------------------------------===//
// EmitMemberInitializer
//===----------------------------------------------------------------------===//

void IREmitCXXCtorDtor::EmitMemberInitializer(ir::IRValue* This,
                                               const CXXCtorInitializer* Init) {
  if (!This || !Init) return;

  // Evaluate initializer arguments
  auto Args = Init->getArguments();
  if (!Args.empty()) {
    for (Expr* Arg : Args) {
      Converter_.getExprEmitter()->Emit(Arg);
    }
  }
}

//===----------------------------------------------------------------------===//
// EmitDelegatingConstructor
//===----------------------------------------------------------------------===//

void IREmitCXXCtorDtor::EmitDelegatingConstructor(
    ir::IRValue* This, const CXXCtorInitializer* Init) {
  if (!This || !Init) return;

  // Evaluate arguments
  auto Args = Init->getArguments();
  for (Expr* Arg : Args) {
    Converter_.getExprEmitter()->Emit(Arg);
  }
}

//===----------------------------------------------------------------------===//
// EmitDestructor
//===----------------------------------------------------------------------===//

void IREmitCXXCtorDtor::EmitDestructor(const CXXDestructorDecl* Dtor,
                                         ir::IRFunction* IRFn) {
  if (!IRFn || !Dtor) return;

  auto& Builder = Converter_.getBuilder();

  // Create entry basic block
  auto* EntryBB = IRFn->addBasicBlock("entry");
  Builder.setInsertPoint(EntryBB);

  // this pointer = first parameter
  ir::IRValue* This = IRFn->getArg(0);

  // Phase 1: Destructor body
  if (Dtor->getBody()) {
    if (auto* CS = llvm::dyn_cast<CompoundStmt>(Dtor->getBody())) {
      for (Stmt* S : CS->getBody()) {
        Converter_.getStmtEmitter()->Emit(S);
      }
    }
  }

  // Phase 2 & 3: Member destruction + Base destruction
  EmitDestructorBody(This, Dtor);

  // Return void
  Builder.createRetVoid();
}

//===----------------------------------------------------------------------===//
// EmitDestructorBody
//===----------------------------------------------------------------------===//

void IREmitCXXCtorDtor::EmitDestructorBody(ir::IRValue* This,
                                            const CXXDestructorDecl* Dtor) {
  if (!This || !Dtor) return;

  auto* Class = Dtor->getParent();
  if (!Class) return;

  auto& Builder = Converter_.getBuilder();
  auto& Layout = Converter_.getCxxEmitter()->getLayoutEmitter();

  // Destroy own fields (reverse order)
  auto Fields = Class->fields();
  for (int i = static_cast<int>(Fields.size()) - 1; i >= 0; --i) {
    // TODO: Emit destructor call for each field that has a non-trivial
    // destructor. For now, fields with trivial destructors are no-ops.
  }

  // Destroy base classes (reverse order)
  auto Bases = Class->bases();
  for (int i = static_cast<int>(Bases.size()) - 1; i >= 0; --i) {
    const auto& Base = Bases[i];
    QualType BaseType = Base.getType();
    if (auto* RT = llvm::dyn_cast<RecordType>(BaseType.getTypePtr())) {
      if (auto* BaseRD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
        // Skip virtual bases (handled separately)
        if (Base.isVirtual()) continue;

        // TODO: Emit base destructor call
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// EmitDestructorCall
//===----------------------------------------------------------------------===//

void IREmitCXXCtorDtor::EmitDestructorCall(const CXXDestructorDecl* Dtor,
                                            ir::IRValue* Object) {
  if (!Dtor || !Object) return;

  auto& Builder = Converter_.getBuilder();

  // TODO: Look up or create the destructor IRFunction and emit a call.
  // For now, this is a placeholder.
}

} // namespace frontend
} // namespace blocktype
