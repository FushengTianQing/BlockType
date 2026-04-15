//===--- Expr.h - Expression AST Nodes ----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Expr class and all expression AST nodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTNode.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

class ValueDecl; // Forward declaration

//===----------------------------------------------------------------------===//
// Operator Kinds
//===----------------------------------------------------------------------===//

/// BinaryOpKind - Enumeration of all binary operators.
enum class BinaryOpKind {
  // Multiplicative
  Mul, Div, Rem,
  // Additive
  Add, Sub,
  // Shift
  Shl, Shr,
  // Relational
  LT, GT, LE, GE,
  // Equality
  EQ, NE,
  // Bitwise
  And, Or, Xor,
  // Logical
  LAnd, LOr,
  // Assignment
  Assign, MulAssign, DivAssign, RemAssign,
  AddAssign, SubAssign, ShlAssign, ShrAssign,
  AndAssign, OrAssign, XorAssign,
  // Comma
  Comma,
  // Spaceship (C++20)
  Spaceship
};

/// UnaryOpKind - Enumeration of all unary operators.
enum class UnaryOpKind {
  // Prefix
  Plus, Minus, Not, LNot,
  Deref, AddrOf,
  PreInc, PreDec,
  // Postfix
  PostInc, PostDec,
  // C++ specific
  Coawait
};

/// CastKind - Enumeration of different kinds of casts.
enum class CastKind {
  CStyle,
  CXXStatic,
  CXXDynamic,
  CXXConst,
  CXXReinterpret
};

//===----------------------------------------------------------------------===//
// Expr - Base class for all expressions
//===----------------------------------------------------------------------===//

/// Expr - Base class for all expression nodes.
class Expr : public ASTNode {
protected:
  Expr(SourceLocation Loc) : ASTNode(Loc) {}

public:
  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::ExprKind &&
           N->getKind() < NodeKind::NullStmtKind;
  }
};

//===----------------------------------------------------------------------===//
// Literal Expressions
//===----------------------------------------------------------------------===//

/// IntegerLiteral - Integer literal of arbitrary precision.
class IntegerLiteral : public Expr {
  llvm::APInt Value;

public:
  IntegerLiteral(SourceLocation Loc, const llvm::APInt &Val)
      : Expr(Loc), Value(Val) {}

  const llvm::APInt &getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::IntegerLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "IntegerLiteral: " << Value << "\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::IntegerLiteralKind;
  }
};

/// FloatingLiteral - Floating point literal.
class FloatingLiteral : public Expr {
  llvm::APFloat Value;

public:
  FloatingLiteral(SourceLocation Loc, const llvm::APFloat &Val)
      : Expr(Loc), Value(Val) {}

  const llvm::APFloat &getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::FloatingLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    llvm::SmallString<16> Str;
    Value.toString(Str);
    OS << "FloatingLiteral: " << Str << "\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::FloatingLiteralKind;
  }
};

/// StringLiteral - String literal.
class StringLiteral : public Expr {
  llvm::StringRef Value;

public:
  StringLiteral(SourceLocation Loc, llvm::StringRef Val)
      : Expr(Loc), Value(Val) {}

  llvm::StringRef getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::StringLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "StringLiteral: \"" << Value << "\"\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::StringLiteralKind;
  }
};

/// CharacterLiteral - Character literal.
class CharacterLiteral : public Expr {
  uint32_t Value;

public:
  CharacterLiteral(SourceLocation Loc, uint32_t Val)
      : Expr(Loc), Value(Val) {}

  uint32_t getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::CharacterLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CharacterLiteral: '" << static_cast<char>(Value) << "'\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CharacterLiteralKind;
  }
};

/// CXXBoolLiteral - Boolean literal (true/false).
class CXXBoolLiteral : public Expr {
  bool Value;

public:
  CXXBoolLiteral(SourceLocation Loc, bool Val)
      : Expr(Loc), Value(Val) {}

  bool getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::CXXBoolLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CXXBoolLiteral: " << (Value ? "true" : "false") << "\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXBoolLiteralKind;
  }
};

/// CXXNullPtrLiteral - Null pointer literal (nullptr).
class CXXNullPtrLiteral : public Expr {
public:
  CXXNullPtrLiteral(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::CXXNullPtrLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CXXNullPtrLiteral: nullptr\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXNullPtrLiteralKind;
  }
};

//===----------------------------------------------------------------------===//
// Reference Expressions
//===----------------------------------------------------------------------===//

/// DeclRefExpr - Reference to a declared value (variable, function, etc.).
class DeclRefExpr : public Expr {
  ValueDecl *D;

public:
  DeclRefExpr(SourceLocation Loc, ValueDecl *D)
      : Expr(Loc), D(D) {}

  ValueDecl *getDecl() const { return D; }

  NodeKind getKind() const override { return NodeKind::DeclRefExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::DeclRefExprKind;
  }
};

/// MemberExpr - Member access expression (x.member or p->member).
class MemberExpr : public Expr {
  Expr *Base;
  ValueDecl *Member;
  bool IsArrow;

public:
  MemberExpr(SourceLocation Loc, Expr *Base, ValueDecl *Member, bool IsArrow)
      : Expr(Loc), Base(Base), Member(Member), IsArrow(IsArrow) {}

  Expr *getBase() const { return Base; }
  ValueDecl *getMemberDecl() const { return Member; }
  bool isArrow() const { return IsArrow; }

  NodeKind getKind() const override { return NodeKind::MemberExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::MemberExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Operator Expressions
//===----------------------------------------------------------------------===//

/// BinaryOperator - Binary operator expression.
class BinaryOperator : public Expr {
  Expr *LHS, *RHS;
  BinaryOpKind Opcode;

public:
  BinaryOperator(SourceLocation Loc, Expr *LHS, Expr *RHS,
                 BinaryOpKind Opcode)
      : Expr(Loc), LHS(LHS), RHS(RHS), Opcode(Opcode) {}

  Expr *getLHS() const { return LHS; }
  Expr *getRHS() const { return RHS; }
  BinaryOpKind getOpcode() const { return Opcode; }

  NodeKind getKind() const override { return NodeKind::BinaryOperatorKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::BinaryOperatorKind;
  }
};

/// UnaryOperator - Unary operator expression.
class UnaryOperator : public Expr {
  Expr *SubExpr;
  UnaryOpKind Opcode;

public:
  UnaryOperator(SourceLocation Loc, Expr *SubExpr, UnaryOpKind Opcode)
      : Expr(Loc), SubExpr(SubExpr), Opcode(Opcode) {}

  Expr *getSubExpr() const { return SubExpr; }
  UnaryOpKind getOpcode() const { return Opcode; }

  bool isPrefix() const {
    return Opcode != UnaryOpKind::PostInc &&
           Opcode != UnaryOpKind::PostDec;
  }

  NodeKind getKind() const override { return NodeKind::UnaryOperatorKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::UnaryOperatorKind;
  }
};

/// ConditionalOperator - Ternary conditional operator (?:).
class ConditionalOperator : public Expr {
  Expr *Cond, *TrueExpr, *FalseExpr;

public:
  ConditionalOperator(SourceLocation Loc, Expr *Cond, Expr *TrueExpr,
                      Expr *FalseExpr)
      : Expr(Loc), Cond(Cond), TrueExpr(TrueExpr), FalseExpr(FalseExpr) {}

  Expr *getCond() const { return Cond; }
  Expr *getTrueExpr() const { return TrueExpr; }
  Expr *getFalseExpr() const { return FalseExpr; }

  NodeKind getKind() const override { return NodeKind::ConditionalOperatorKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ConditionalOperatorKind;
  }
};

//===----------------------------------------------------------------------===//
// Call Expressions
//===----------------------------------------------------------------------===//

/// CallExpr - Function call expression.
class CallExpr : public Expr {
  Expr *Callee;
  llvm::SmallVector<Expr *, 4> Args;

public:
  CallExpr(SourceLocation Loc, Expr *Callee, llvm::ArrayRef<Expr *> Args)
      : Expr(Loc), Callee(Callee), Args(Args.begin(), Args.end()) {}

  Expr *getCallee() const { return Callee; }
  llvm::ArrayRef<Expr *> getArgs() const { return Args; }
  unsigned getNumArgs() const { return Args.size(); }

  NodeKind getKind() const override { return NodeKind::CallExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::CallExprKind &&
           N->getKind() <= NodeKind::CXXTemporaryObjectExprKind;
  }
};

/// CXXMemberCallExpr - Member function call expression.
class CXXMemberCallExpr : public CallExpr {
public:
  CXXMemberCallExpr(SourceLocation Loc, Expr *Callee,
                    llvm::ArrayRef<Expr *> Args)
      : CallExpr(Loc, Callee, Args) {}

  NodeKind getKind() const override { return NodeKind::CXXMemberCallExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXMemberCallExprKind;
  }
};

/// CXXConstructExpr - Constructor call expression.
class CXXConstructExpr : public Expr {
  llvm::SmallVector<Expr *, 4> Args;

public:
  CXXConstructExpr(SourceLocation Loc, llvm::ArrayRef<Expr *> Args)
      : Expr(Loc), Args(Args.begin(), Args.end()) {}

  llvm::ArrayRef<Expr *> getArgs() const { return Args; }

  NodeKind getKind() const override { return NodeKind::CXXConstructExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::CXXConstructExprKind &&
           N->getKind() <= NodeKind::CXXTemporaryObjectExprKind;
  }
};

/// CXXTemporaryObjectExpr - Temporary object creation expression.
class CXXTemporaryObjectExpr : public CXXConstructExpr {
public:
  CXXTemporaryObjectExpr(SourceLocation Loc, llvm::ArrayRef<Expr *> Args)
      : CXXConstructExpr(Loc, Args) {}

  NodeKind getKind() const override {
    return NodeKind::CXXTemporaryObjectExprKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXTemporaryObjectExprKind;
  }
};

//===----------------------------------------------------------------------===//
// C++ Specific Expressions
//===----------------------------------------------------------------------===//

/// CXXThisExpr - The 'this' pointer.
class CXXThisExpr : public Expr {
public:
  CXXThisExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::CXXThisExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CXXThisExpr: this\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXThisExprKind;
  }
};

/// CXXNewExpr - New expression.
class CXXNewExpr : public Expr {
  Expr *ArraySize;
  Expr *Initializer;

public:
  CXXNewExpr(SourceLocation Loc, Expr *ArraySize, Expr *Initializer)
      : Expr(Loc), ArraySize(ArraySize), Initializer(Initializer) {}

  Expr *getArraySize() const { return ArraySize; }
  Expr *getInitializer() const { return Initializer; }

  NodeKind getKind() const override { return NodeKind::CXXNewExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXNewExprKind;
  }
};

/// CXXDeleteExpr - Delete expression.
class CXXDeleteExpr : public Expr {
  Expr *Argument;
  bool IsArray;

public:
  CXXDeleteExpr(SourceLocation Loc, Expr *Argument, bool IsArray)
      : Expr(Loc), Argument(Argument), IsArray(IsArray) {}

  Expr *getArgument() const { return Argument; }
  bool isArrayForm() const { return IsArray; }

  NodeKind getKind() const override { return NodeKind::CXXDeleteExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXDeleteExprKind;
  }
};

/// CXXThrowExpr - Throw expression.
class CXXThrowExpr : public Expr {
  Expr *SubExpr;

public:
  CXXThrowExpr(SourceLocation Loc, Expr *SubExpr)
      : Expr(Loc), SubExpr(SubExpr) {}

  Expr *getSubExpr() const { return SubExpr; }

  NodeKind getKind() const override { return NodeKind::CXXThrowExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXThrowExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Cast Expressions
//===----------------------------------------------------------------------===//

/// CastExpr - Base class for all cast expressions.
class CastExpr : public Expr {
protected:
  Expr *SubExpr;
  CastKind Kind;

  CastExpr(SourceLocation Loc, Expr *SubExpr, CastKind K)
      : Expr(Loc), SubExpr(SubExpr), Kind(K) {}

public:
  Expr *getSubExpr() const { return SubExpr; }
  CastKind getCastKind() const { return Kind; }

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::CastExprKind &&
           N->getKind() <= NodeKind::CStyleCastExprKind;
  }
};

/// CXXStaticCastExpr - static_cast expression.
class CXXStaticCastExpr : public CastExpr {
public:
  CXXStaticCastExpr(SourceLocation Loc, Expr *SubExpr)
      : CastExpr(Loc, SubExpr, CastKind::CXXStatic) {}

  NodeKind getKind() const override { return NodeKind::CXXStaticCastExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXStaticCastExprKind;
  }
};

/// CXXDynamicCastExpr - dynamic_cast expression.
class CXXDynamicCastExpr : public CastExpr {
public:
  CXXDynamicCastExpr(SourceLocation Loc, Expr *SubExpr)
      : CastExpr(Loc, SubExpr, CastKind::CXXDynamic) {}

  NodeKind getKind() const override { return NodeKind::CXXDynamicCastExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXDynamicCastExprKind;
  }
};

/// CXXConstCastExpr - const_cast expression.
class CXXConstCastExpr : public CastExpr {
public:
  CXXConstCastExpr(SourceLocation Loc, Expr *SubExpr)
      : CastExpr(Loc, SubExpr, CastKind::CXXConst) {}

  NodeKind getKind() const override { return NodeKind::CXXConstCastExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXConstCastExprKind;
  }
};

/// CXXReinterpretCastExpr - reinterpret_cast expression.
class CXXReinterpretCastExpr : public CastExpr {
public:
  CXXReinterpretCastExpr(SourceLocation Loc, Expr *SubExpr)
      : CastExpr(Loc, SubExpr, CastKind::CXXReinterpret) {}

  NodeKind getKind() const override {
    return NodeKind::CXXReinterpretCastExprKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXReinterpretCastExprKind;
  }
};

/// CStyleCastExpr - C-style cast expression.
class CStyleCastExpr : public CastExpr {
public:
  CStyleCastExpr(SourceLocation Loc, Expr *SubExpr)
      : CastExpr(Loc, SubExpr, CastKind::CStyle) {}

  NodeKind getKind() const override { return NodeKind::CStyleCastExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CStyleCastExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Modern C++ Expressions
//===----------------------------------------------------------------------===//

/// LambdaExpr - Lambda expression.
class LambdaExpr : public Expr {
public:
  LambdaExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::LambdaExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "LambdaExpr\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::LambdaExprKind;
  }
};

/// RequiresExpr - C++20 requires expression.
class RequiresExpr : public Expr {
public:
  RequiresExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::RequiresExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "RequiresExpr\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::RequiresExprKind;
  }
};

/// CXXFoldExpr - C++17 fold expression.
class CXXFoldExpr : public Expr {
public:
  CXXFoldExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::CXXFoldExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CXXFoldExpr\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXFoldExprKind;
  }
};

/// PackIndexingExpr - C++26 pack indexing expression.
class PackIndexingExpr : public Expr {
public:
  PackIndexingExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::PackIndexingExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "PackIndexingExpr\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::PackIndexingExprKind;
  }
};

/// ReflexprExpr - C++26 reflexpr expression.
class ReflexprExpr : public Expr {
public:
  ReflexprExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::ReflexprExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "ReflexprExpr\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ReflexprExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Coroutine Expressions
//===----------------------------------------------------------------------===//

/// CoawaitExpr - Co_await expression.
class CoawaitExpr : public Expr {
public:
  CoawaitExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::CoawaitExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CoawaitExpr\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CoawaitExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Error Recovery
//===----------------------------------------------------------------------===//

/// RecoveryExpr - Error recovery expression.
class RecoveryExpr : public Expr {
public:
  RecoveryExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::RecoveryExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "RecoveryExpr\n";
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::RecoveryExprKind;
  }
};

} // namespace blocktype
