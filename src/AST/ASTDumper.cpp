//===--- ASTDumper.cpp - AST Debugging Implementation -------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ASTDumper class.
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/ASTDumper.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

void ASTDumper::dump(const ASTNode *Node) {
  if (!Node) {
    OS << "<null>\n";
    return;
  }

  if (auto *E = llvm::dyn_cast<Expr>(Node)) {
    dumpExpr(E);
  } else if (auto *S = llvm::dyn_cast<Stmt>(Node)) {
    dumpStmt(S);
  } else {
    OS << "Unknown node\n";
  }
}

void ASTDumper::dump(const Expr *E) {
  if (!E) {
    OS << "<null expr>\n";
    return;
  }
  dumpExpr(E);
}

void ASTDumper::dump(const Stmt *S) {
  if (!S) {
    OS << "<null stmt>\n";
    return;
  }
  dumpStmt(S);
}

void ASTDumper::dump(const Type *T) {
  if (!T) {
    OS << "<null type>\n";
    return;
  }
  dumpType(T);
}

void ASTDumper::dump(QualType QT) {
  if (QT.isNull()) {
    OS << "<null type>\n";
    return;
  }

  if (QT.isConstQualified())
    OS << "const ";
  if (QT.isVolatileQualified())
    OS << "volatile ";

  dumpType(QT.getTypePtr());
}

void ASTDumper::printIndent() {
  for (unsigned i = 0; i < Indent; ++i)
    OS << ' ';
}

void ASTDumper::printChildMarker(bool IsLast) {
  printIndent();
  if (IsLast)
    OS << "`-";
  else
    OS << "|-";
}

void ASTDumper::dumpExpr(const Expr *E) {
  printIndent();

  switch (E->getKind()) {
  case ASTNode::IntegerLiteralKind: {
    auto *Lit = llvm::cast<IntegerLiteral>(E);
    OS << "IntegerLiteral " << Lit->getValue() << "\n";
    break;
  }

  case ASTNode::FloatingLiteralKind: {
    auto *Lit = llvm::cast<FloatingLiteral>(E);
    llvm::SmallString<16> Str;
    Lit->getValue().toString(Str);
    OS << "FloatingLiteral " << Str << "\n";
    break;
  }

  case ASTNode::StringLiteralKind: {
    auto *Lit = llvm::cast<StringLiteral>(E);
    OS << "StringLiteral \"" << Lit->getValue() << "\"\n";
    break;
  }

  case ASTNode::CharacterLiteralKind: {
    auto *Lit = llvm::cast<CharacterLiteral>(E);
    OS << "CharacterLiteral '" << static_cast<char>(Lit->getValue()) << "'\n";
    break;
  }

  case ASTNode::BinaryOperatorKind: {
    auto *BinOp = llvm::cast<BinaryOperator>(E);
    OS << "BinaryOperator '";
    switch (BinOp->getOpcode()) {
    case BinaryOpKind::Add: OS << "+"; break;
    case BinaryOpKind::Sub: OS << "-"; break;
    case BinaryOpKind::Mul: OS << "*"; break;
    case BinaryOpKind::Div: OS << "/"; break;
    case BinaryOpKind::Rem: OS << "%"; break;
    case BinaryOpKind::Shl: OS << "<<"; break;
    case BinaryOpKind::Shr: OS << ">>"; break;
    case BinaryOpKind::LT: OS << "<"; break;
    case BinaryOpKind::GT: OS << ">"; break;
    case BinaryOpKind::LE: OS << "<="; break;
    case BinaryOpKind::GE: OS << ">="; break;
    case BinaryOpKind::EQ: OS << "=="; break;
    case BinaryOpKind::NE: OS << "!="; break;
    case BinaryOpKind::And: OS << "&"; break;
    case BinaryOpKind::Or: OS << "|"; break;
    case BinaryOpKind::Xor: OS << "^"; break;
    case BinaryOpKind::LAnd: OS << "&&"; break;
    case BinaryOpKind::LOr: OS << "||"; break;
    case BinaryOpKind::Assign: OS << "="; break;
    case BinaryOpKind::Comma: OS << ","; break;
    default: OS << "???"; break;
    }
    OS << "'\n";

    indent();
    printChildMarker(false);
    dumpExpr(BinOp->getLHS());
    printChildMarker(true);
    dumpExpr(BinOp->getRHS());
    dedent();
    break;
  }

  case ASTNode::UnaryOperatorKind: {
    auto *UnOp = llvm::cast<UnaryOperator>(E);
    OS << "UnaryOperator '";
    switch (UnOp->getOpcode()) {
    case UnaryOpKind::Plus: OS << "+"; break;
    case UnaryOpKind::Minus: OS << "-"; break;
    case UnaryOpKind::Not: OS << "!"; break;
    case UnaryOpKind::LNot: OS << "~"; break;
    case UnaryOpKind::Deref: OS << "*"; break;
    case UnaryOpKind::AddrOf: OS << "&"; break;
    case UnaryOpKind::PreInc: OS << "++"; break;
    case UnaryOpKind::PreDec: OS << "--"; break;
    default: OS << "???"; break;
    }
    OS << "'\n";

    indent();
    printChildMarker(true);
    dumpExpr(UnOp->getSubExpr());
    dedent();
    break;
  }

  case ASTNode::CallExprKind: {
    auto *Call = llvm::cast<CallExpr>(E);
    OS << "CallExpr\n";

    indent();
    printChildMarker(Call->getNumArgs() == 0);
    dumpExpr(Call->getCallee());

    for (size_t i = 0; i < Call->getNumArgs(); ++i) {
      printChildMarker(i == Call->getNumArgs() - 1);
      dumpExpr(Call->getArgs()[i]);
    }
    dedent();
    break;
  }

  case ASTNode::ConditionalOperatorKind: {
    auto *CondOp = llvm::cast<ConditionalOperator>(E);
    OS << "ConditionalOperator\n";

    indent();
    printChildMarker(false);
    dumpExpr(CondOp->getCond());
    printChildMarker(false);
    dumpExpr(CondOp->getTrueExpr());
    printChildMarker(true);
    dumpExpr(CondOp->getFalseExpr());
    dedent();
    break;
  }

  default:
    OS << "Expr\n";
    break;
  }
}

void ASTDumper::dumpStmt(const Stmt *S) {
  printIndent();

  switch (S->getKind()) {
  case ASTNode::NullStmtKind: {
    OS << "NullStmt\n";
    break;
  }

  case ASTNode::CompoundStmtKind: {
    auto *CS = llvm::cast<CompoundStmt>(S);
    OS << "CompoundStmt\n";

    indent();
    auto Body = CS->getBody();
    for (size_t i = 0; i < Body.size(); ++i) {
      printChildMarker(i == Body.size() - 1);
      dumpStmt(Body[i]);
    }
    dedent();
    break;
  }

  case ASTNode::ReturnStmtKind: {
    auto *RS = llvm::cast<ReturnStmt>(S);
    OS << "ReturnStmt\n";

    if (RS->getRetValue()) {
      indent();
      printChildMarker(true);
      dumpExpr(RS->getRetValue());
      dedent();
    }
    break;
  }

  case ASTNode::IfStmtKind: {
    auto *IS = llvm::cast<IfStmt>(S);
    OS << "IfStmt\n";

    indent();
    printChildMarker(false);
    dumpExpr(IS->getCond());
    printChildMarker(IS->getElse() == nullptr);
    dumpStmt(IS->getThen());

    if (IS->getElse()) {
      printChildMarker(true);
      dumpStmt(IS->getElse());
    }
    dedent();
    break;
  }

  case ASTNode::WhileStmtKind: {
    OS << "WhileStmt\n";
    // TODO: Implement when WhileStmt is available
    break;
  }

  case ASTNode::ForStmtKind: {
    OS << "ForStmt\n";
    // TODO: Implement when ForStmt is available
    break;
  }

  default:
    OS << "Stmt\n";
    break;
  }
}

void ASTDumper::dumpType(const Type *T) {
  switch (T->getTypeClass()) {
  case TypeClass::Builtin: {
    auto *BT = llvm::cast<BuiltinType>(T);
    OS << BT->getName() << "\n";
    break;
  }

  case TypeClass::Pointer: {
    auto *PT = llvm::cast<PointerType>(T);
    OS << "Pointer\n";
    indent();
    printChildMarker(true);
    dumpType(PT->getPointeeType());
    dedent();
    break;
  }

  case TypeClass::LValueReference: {
    auto *RT = llvm::cast<LValueReferenceType>(T);
    OS << "LValueReference\n";
    indent();
    printChildMarker(true);
    dumpType(RT->getReferencedType());
    dedent();
    break;
  }

  case TypeClass::RValueReference: {
    auto *RT = llvm::cast<RValueReferenceType>(T);
    OS << "RValueReference\n";
    indent();
    printChildMarker(true);
    dumpType(RT->getReferencedType());
    dedent();
    break;
  }

  case TypeClass::ConstantArray: {
    auto *AT = llvm::cast<ArrayType>(T);
    OS << "Array\n";
    indent();
    printChildMarker(true);
    dumpType(AT->getElementType());
    dedent();
    break;
  }

  case TypeClass::Function: {
    auto *FT = llvm::cast<FunctionType>(T);
    OS << "Function\n";
    indent();
    printChildMarker(FT->getParamTypes().empty());
    dumpType(FT->getReturnType());

    for (const auto &Param : FT->getParamTypes()) {
      printChildMarker(true);
      dumpType(Param);
    }
    dedent();
    break;
  }

  default:
    OS << "Type\n";
    break;
  }
}

} // namespace blocktype
