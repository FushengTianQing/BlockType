//===--- IREmitCXX.cpp - C++ IR Emission Top-Level Dispatcher -*- C++ -*-===//

#include "blocktype/Frontend/IREmitCXX.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/AST/Decl.h"

namespace blocktype {
namespace frontend {

//===----------------------------------------------------------------------===//
// Static helpers (forward to Layout)
//===----------------------------------------------------------------------===//

bool IREmitCXX::hasVirtualFunctions(const CXXRecordDecl* RD) {
  return IREmitCXXLayout::hasVirtualFunctions(RD);
}

bool IREmitCXX::hasVirtualFunctionsInHierarchy(const CXXRecordDecl* RD) {
  return IREmitCXXLayout::hasVirtualFunctionsInHierarchy(RD);
}

//===----------------------------------------------------------------------===//
// Constructor / Destructor
//===----------------------------------------------------------------------===//

IREmitCXX::IREmitCXX(ASTToIRConverter& C) : Converter_(C) {
  LayoutEmitter_ = std::make_unique<IREmitCXXLayout>(C);
  CtorDtorEmitter_ = std::make_unique<IREmitCXXCtorDtor>(C);
  VTableEmitter_ = std::make_unique<IREmitCXXVTable>(C);
  InheritEmitter_ = std::make_unique<IREmitCXXInherit>(C);
}

IREmitCXX::~IREmitCXX() = default;

//===----------------------------------------------------------------------===//
// Top-level dispatch
//===----------------------------------------------------------------------===//

void IREmitCXX::EmitCXXConstructor(CXXConstructorDecl* Ctor,
                                    ir::IRFunction* IRFn) {
  CtorDtorEmitter_->EmitConstructor(Ctor, IRFn);
}

void IREmitCXX::EmitCXXDestructor(CXXDestructorDecl* Dtor,
                                   ir::IRFunction* IRFn) {
  CtorDtorEmitter_->EmitDestructor(Dtor, IRFn);
}

void IREmitCXX::EmitVTable(const CXXRecordDecl* RD) {
  VTableEmitter_->EmitVTable(RD);
}

void IREmitCXX::EmitRTTI(const CXXRecordDecl* RD) {
  VTableEmitter_->EmitRTTI(RD);
}

void IREmitCXX::EmitThunk(const CXXMethodDecl* MD) {
  InheritEmitter_->EmitThunk(MD);
}

} // namespace frontend
} // namespace blocktype
