//===--- Expr.cpp - Expression AST Node Implementation ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/Expr.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

static const char *getBinaryOperatorName(BinaryOpKind Kind) {
  switch (Kind) {
  case BinaryOpKind::Mul: return "*";
  case BinaryOpKind::Div: return "/";
  case BinaryOpKind::Rem: return "%";
  case BinaryOpKind::Add: return "+";
  case BinaryOpKind::Sub: return "-";
  case BinaryOpKind::Shl: return "<<";
  case BinaryOpKind::Shr: return ">>";
  case BinaryOpKind::LT: return "<";
  case BinaryOpKind::GT: return ">";
  case BinaryOpKind::LE: return "<=";
  case BinaryOpKind::GE: return ">=";
  case BinaryOpKind::EQ: return "==";
  case BinaryOpKind::NE: return "!=";
  case BinaryOpKind::And: return "&";
  case BinaryOpKind::Or: return "|";
  case BinaryOpKind::Xor: return "^";
  case BinaryOpKind::LAnd: return "&&";
  case BinaryOpKind::LOr: return "||";
  case BinaryOpKind::Assign: return "=";
  case BinaryOpKind::MulAssign: return "*=";
  case BinaryOpKind::DivAssign: return "/=";
  case BinaryOpKind::RemAssign: return "%=";
  case BinaryOpKind::AddAssign: return "+=";
  case BinaryOpKind::SubAssign: return "-=";
  case BinaryOpKind::ShlAssign: return "<<=";
  case BinaryOpKind::ShrAssign: return ">>=";
  case BinaryOpKind::AndAssign: return "&=";
  case BinaryOpKind::OrAssign: return "|=";
  case BinaryOpKind::XorAssign: return "^=";
  case BinaryOpKind::Comma: return ",";
  case BinaryOpKind::Spaceship: return "<=>";
  }
  llvm_unreachable("Unknown binary operator kind");
}

static const char *getUnaryOperatorName(UnaryOpKind Kind) {
  switch (Kind) {
  case UnaryOpKind::Plus: return "+";
  case UnaryOpKind::Minus: return "-";
  case UnaryOpKind::Not: return "~";
  case UnaryOpKind::LNot: return "!";
  case UnaryOpKind::Deref: return "*";
  case UnaryOpKind::AddrOf: return "&";
  case UnaryOpKind::PreInc: return "++";
  case UnaryOpKind::PreDec: return "--";
  case UnaryOpKind::PostInc: return "++";
  case UnaryOpKind::PostDec: return "--";
  case UnaryOpKind::Coawait: return "co_await";
  }
  llvm_unreachable("Unknown unary operator kind");
}

static const char *getCastKindName(CastKind Kind) {
  switch (Kind) {
  case CastKind::CStyle: return "CStyle";
  case CastKind::CXXStatic: return "static_cast";
  case CastKind::CXXDynamic: return "dynamic_cast";
  case CastKind::CXXConst: return "const_cast";
  case CastKind::CXXReinterpret: return "reinterpret_cast";
  }
  llvm_unreachable("Unknown cast kind");
}

//===----------------------------------------------------------------------===//
// DeclRefExpr
//===----------------------------------------------------------------------===//

void DeclRefExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "DeclRefExpr";
  if (D)
    OS << ": " << D;
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// MemberExpr
//===----------------------------------------------------------------------===//

void MemberExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "MemberExpr: " << (IsArrow ? "->" : ".") << "\n";
  if (Base)
    Base->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// BinaryOperator
//===----------------------------------------------------------------------===//

void BinaryOperator::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "BinaryOperator: " << getBinaryOperatorName(Opcode) << "\n";
  if (LHS)
    LHS->dump(OS, Indent + 1);
  if (RHS)
    RHS->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// UnaryOperator
//===----------------------------------------------------------------------===//

void UnaryOperator::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "UnaryOperator: " << getUnaryOperatorName(Opcode)
     << (isPrefix() ? " (prefix)" : " (postfix)") << "\n";
  if (SubExpr)
    SubExpr->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// ConditionalOperator
//===----------------------------------------------------------------------===//

void ConditionalOperator::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ConditionalOperator: ?:\n";
  if (Cond)
    Cond->dump(OS, Indent + 1);
  if (TrueExpr)
    TrueExpr->dump(OS, Indent + 1);
  if (FalseExpr)
    FalseExpr->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CallExpr
//===----------------------------------------------------------------------===//

void CallExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CallExpr\n";
  if (Callee)
    Callee->dump(OS, Indent + 1);
  for (Expr *Arg : Args) {
    if (Arg)
      Arg->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// CXXMemberCallExpr
//===----------------------------------------------------------------------===//

void CXXMemberCallExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXMemberCallExpr\n";
  if (getCallee())
    getCallee()->dump(OS, Indent + 1);
  for (Expr *Arg : getArgs()) {
    if (Arg)
      Arg->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// CXXConstructExpr
//===----------------------------------------------------------------------===//

void CXXConstructExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXConstructExpr\n";
  for (Expr *Arg : Args) {
    if (Arg)
      Arg->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// CXXTemporaryObjectExpr
//===----------------------------------------------------------------------===//

void CXXTemporaryObjectExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXTemporaryObjectExpr\n";
  for (Expr *Arg : getArgs()) {
    if (Arg)
      Arg->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// CXXNewExpr
//===----------------------------------------------------------------------===//

void CXXNewExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXNewExpr: new\n";
  if (ArraySize)
    ArraySize->dump(OS, Indent + 1);
  if (Initializer)
    Initializer->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CXXDeleteExpr
//===----------------------------------------------------------------------===//

void CXXDeleteExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXDeleteExpr: " << (IsArray ? "delete[]" : "delete") << "\n";
  if (Argument)
    Argument->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CXXThrowExpr
//===----------------------------------------------------------------------===//

void CXXThrowExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXThrowExpr: throw\n";
  if (SubExpr)
    SubExpr->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// Cast Expressions
//===----------------------------------------------------------------------===//

void CXXStaticCastExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXStaticCastExpr: " << getCastKindName(Kind) << "\n";
  if (SubExpr)
    SubExpr->dump(OS, Indent + 1);
}

void CXXDynamicCastExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXDynamicCastExpr: " << getCastKindName(Kind) << "\n";
  if (SubExpr)
    SubExpr->dump(OS, Indent + 1);
}

void CXXConstCastExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXConstCastExpr: " << getCastKindName(Kind) << "\n";
  if (SubExpr)
    SubExpr->dump(OS, Indent + 1);
}

void CXXReinterpretCastExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXReinterpretCastExpr: " << getCastKindName(Kind) << "\n";
  if (SubExpr)
    SubExpr->dump(OS, Indent + 1);
}

void CStyleCastExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CStyleCastExpr: " << getCastKindName(Kind) << "\n";
  if (SubExpr)
    SubExpr->dump(OS, Indent + 1);
}

} // namespace blocktype
