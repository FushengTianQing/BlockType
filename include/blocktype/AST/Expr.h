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
#include "blocktype/AST/Decl.h"  // For ValueDecl, ParmVarDecl
#include "blocktype/AST/Type.h"  // For QualType
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

// Forward declarations
class NestedNameSpecifier;

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
  CXXReinterpret,
  // 隐式转换（由 Sema 插入）
  LValueToRValue,      ///< glvalue → prvalue (加载)
  IntegralCast,        ///< 整数类型之间转换（含提升）
  FloatingCast,        ///< 浮点类型之间转换
  IntegralToFloating,  ///< 整数 → 浮点
  FloatingToIntegral,  ///< 浮点 → 整数
  PointerToIntegral,   ///< 指针 → 整数
  IntegralToPointer,   ///< 整数 → 指针
  BitCast,             ///< 指针之间 bitcast
  DerivedToBase,       ///< 派生类指针/引用 → 基类指针/引用
  BaseToDerived,       ///< 基类指针/引用 → 派生类指针/引用
  NoOp                 ///< 无操作（类型相同）
};

//===----------------------------------------------------------------------===//
// Value Kind (expression value category)
//===----------------------------------------------------------------------===//

/// ExprValueKind - The categorization of expression values per C++ [basic.lval].
enum class ExprValueKind {
  VK_PRValue, ///< A prvalue (pure rvalue): temporary, literal, etc.
  VK_LValue,  ///< An lvalue: refers to an object or function
  VK_XValue   ///< An xvalue: expiring glvalue (e.g., move result)
};

//===----------------------------------------------------------------------===//
// Expr - Base class for all expressions
//===----------------------------------------------------------------------===//

/// Expr - Base class for all expression nodes.
class Expr : public ASTNode {
protected:
  QualType ExprTy;
  ExprValueKind ValueKind = ExprValueKind::VK_PRValue;

  Expr(SourceLocation Loc, QualType T = QualType(),
       ExprValueKind VK = ExprValueKind::VK_PRValue)
      : ASTNode(Loc), ExprTy(T), ValueKind(VK) {}

public:
  /// getType - Returns the type of this expression.
  virtual QualType getType() const { return ExprTy; }

  /// Set the type of this expression (used by Sema during type checking).
  void setType(QualType T) { ExprTy = T; }

  /// getValueKind - Returns the value category of this expression.
  ExprValueKind getValueKind() const { return ValueKind; }

  /// setValueKind - Sets the value category of this expression.
  void setValueKind(ExprValueKind VK) { ValueKind = VK; }

  /// isLValue - Returns true if this expression is an lvalue.
  bool isLValue() const { return ValueKind == ExprValueKind::VK_LValue; }

  /// isXValue - Returns true if this expression is an xvalue.
  bool isXValue() const { return ValueKind == ExprValueKind::VK_XValue; }

  /// isPRValue - Returns true if this expression is a prvalue.
  bool isPRValue() const { return ValueKind == ExprValueKind::VK_PRValue; }

  /// isGLValue - Returns true if this expression is a glvalue (lvalue or xvalue).
  bool isGLValue() const { return isLValue() || isXValue(); }

  /// isRValue - Returns true if this expression is an rvalue (prvalue or xvalue).
  bool isRValue() const { return isPRValue() || isXValue(); }

  /// isTypeDependent - Determine whether this expression is type-dependent.
  /// An expression is type-dependent if its type depends on a template parameter.
  virtual bool isTypeDependent() const;

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
  IntegerLiteral(SourceLocation Loc, const llvm::APInt &Val,
                 QualType T = QualType())
      : Expr(Loc, T), Value(Val) {}

  const llvm::APInt &getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::IntegerLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "IntegerLiteral: " << Value << "\n";
  }

  bool isTypeDependent() const override { return false; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::IntegerLiteralKind;
  }
};

/// FloatingLiteral - Floating point literal.
class FloatingLiteral : public Expr {
  llvm::APFloat Value;

public:
  FloatingLiteral(SourceLocation Loc, const llvm::APFloat &Val,
                  QualType T = QualType())
      : Expr(Loc, T), Value(Val) {}

  const llvm::APFloat &getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::FloatingLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    llvm::SmallString<16> Str;
    Value.toString(Str);
    OS << "FloatingLiteral: " << Str << "\n";
  }

  bool isTypeDependent() const override { return false; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::FloatingLiteralKind;
  }
};

/// StringLiteral - String literal.
class StringLiteral : public Expr {
  llvm::StringRef Value;

public:
  StringLiteral(SourceLocation Loc, llvm::StringRef Val,
                QualType T = QualType())
      : Expr(Loc, T), Value(Val) {}

  llvm::StringRef getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::StringLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "StringLiteral: \"" << Value << "\"\n";
  }

  bool isTypeDependent() const override { return false; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::StringLiteralKind;
  }
};

/// CharacterLiteral - Character literal.
class CharacterLiteral : public Expr {
  uint32_t Value;

public:
  CharacterLiteral(SourceLocation Loc, uint32_t Val,
                    QualType T = QualType())
      : Expr(Loc, T), Value(Val) {}

  uint32_t getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::CharacterLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CharacterLiteral: '" << static_cast<char>(Value) << "'\n";
  }

  bool isTypeDependent() const override { return false; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CharacterLiteralKind;
  }
};

/// CXXBoolLiteral - Boolean literal (true/false).
class CXXBoolLiteral : public Expr {
  bool Value;

public:
  CXXBoolLiteral(SourceLocation Loc, bool Val, QualType T = QualType())
      : Expr(Loc, T), Value(Val) {}

  bool getValue() const { return Value; }

  NodeKind getKind() const override { return NodeKind::CXXBoolLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CXXBoolLiteral: " << (Value ? "true" : "false") << "\n";
  }

  bool isTypeDependent() const override { return false; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXBoolLiteralKind;
  }
};

/// CXXNullPtrLiteral - Null pointer literal (nullptr).
class CXXNullPtrLiteral : public Expr {
public:
  CXXNullPtrLiteral(SourceLocation Loc, QualType T = QualType())
      : Expr(Loc, T) {}

  NodeKind getKind() const override { return NodeKind::CXXNullPtrLiteralKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "CXXNullPtrLiteral: nullptr\n";
  }

  bool isTypeDependent() const override { return false; }

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
  llvm::StringRef Name;  // Store name for template lookup when D is nullptr

public:
  DeclRefExpr(SourceLocation Loc, ValueDecl *D, llvm::StringRef Name = "")
      : Expr(Loc, D ? D->getType() : QualType(), ExprValueKind::VK_LValue),
        D(D), Name(Name) {}

  ValueDecl *getDecl() const { return D; }
  llvm::StringRef getName() const { 
    // Prefer stored name, fall back to declaration name
    if (!Name.empty()) return Name;
    if (D) return D->getName();
    return "";
  }

  NodeKind getKind() const override { return NodeKind::DeclRefExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A declaration reference is type-dependent if the referenced declaration has a dependent type
  bool isTypeDependent() const override {
    return D && D->getType()->isDependentType();
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::DeclRefExprKind;
  }
};

/// TemplateSpecializationExpr - Template specialization expression (e.g., Integral<T>).
///
/// This represents a template-id with explicit template arguments,
/// such as `std::vector<int>` or `MyTemplate<T, 42>`.
class TemplateSpecializationExpr : public Expr {
  llvm::StringRef TemplateName;
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
  ValueDecl *TemplateDecl;  // The template declaration (may be nullptr if not found)

public:
  TemplateSpecializationExpr(SourceLocation Loc, llvm::StringRef TemplateName,
                              llvm::ArrayRef<TemplateArgument> Args,
                              ValueDecl *TD = nullptr)
      : Expr(Loc), TemplateName(TemplateName), 
        TemplateArgs(Args.begin(), Args.end()), TemplateDecl(TD) {}

  llvm::StringRef getTemplateName() const { return TemplateName; }
  
  llvm::ArrayRef<TemplateArgument> getTemplateArgs() const { return TemplateArgs; }
  
  unsigned getNumTemplateArgs() const { return TemplateArgs.size(); }
  
  const TemplateArgument &getTemplateArg(unsigned Idx) const { 
    return TemplateArgs[Idx]; 
  }
  
  ValueDecl *getTemplateDecl() const { return TemplateDecl; }

  NodeKind getKind() const override { 
    return NodeKind::TemplateSpecializationExprKind; 
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A template specialization is type-dependent if any of its template arguments is dependent
  bool isTypeDependent() const override {
    for (const auto &Arg : TemplateArgs) {
      if (Arg.isType()) {
        if (Arg.getAsType()->isDependentType()) {
          return true;
        }
      } else if (Arg.isNonType()) {
        if (Arg.getAsExpr()->isTypeDependent()) {
          return true;
        }
      }
      // Template template parameters are also potentially dependent
    }
    return false;
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TemplateSpecializationExprKind;
  }
};

/// MemberExpr - Member access expression (x.member or p->member).
class MemberExpr : public Expr {
  Expr *Base;
  ValueDecl *Member;
  bool IsArrow;

public:
  MemberExpr(SourceLocation Loc, Expr *Base, ValueDecl *Member, bool IsArrow)
      : Expr(Loc, Member ? Member->getType() : QualType(),
             ExprValueKind::VK_LValue),
        Base(Base), Member(Member), IsArrow(IsArrow) {}

  Expr *getBase() const { return Base; }
  ValueDecl *getMemberDecl() const { return Member; }
  bool isArrow() const { return IsArrow; }

  NodeKind getKind() const override { return NodeKind::MemberExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A member access expression is type-dependent if the base or the member type is dependent
  bool isTypeDependent() const override {
    return Base->isTypeDependent() || (Member && Member->getType()->isDependentType());
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::MemberExprKind;
  }
};

/// ArraySubscriptExpr - Array subscript expression (base[index]).
/// C++23: supports multi-dimensional subscript: base[i, j, k]
class ArraySubscriptExpr : public Expr {
  Expr *Base;
  Expr *Index;                       // First index (backward compat)
  llvm::SmallVector<Expr *, 2> Indices; // C++23: all indices

public:
  ArraySubscriptExpr(SourceLocation Loc, Expr *Base, Expr *Index)
      : Expr(Loc, QualType(), ExprValueKind::VK_LValue), Base(Base), Index(Index) {
    if (Index)
      Indices.push_back(Index);
  }

  /// C++23 multi-index constructor
  ArraySubscriptExpr(SourceLocation Loc, Expr *Base,
                     llvm::ArrayRef<Expr *> Indices)
      : Expr(Loc, QualType(), ExprValueKind::VK_LValue), Base(Base),
        Index(Indices.empty() ? nullptr : Indices[0]),
        Indices(Indices.begin(), Indices.end()) {}

  Expr *getBase() const { return Base; }
  Expr *getIndex() const { return Index; }
  llvm::ArrayRef<Expr *> getIndices() const { return Indices; }
  unsigned getNumIndices() const { return Indices.size(); }
  bool isMultiDimensional() const { return Indices.size() > 1; }

  NodeKind getKind() const override { return NodeKind::ArraySubscriptExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// An array subscript is type-dependent if the base or index is type-dependent
  bool isTypeDependent() const override {
    return Base->isTypeDependent() || Index->isTypeDependent();
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ArraySubscriptExprKind;
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
      : Expr(Loc, QualType(),
             (Opcode == BinaryOpKind::Assign ||
              Opcode == BinaryOpKind::MulAssign ||
              Opcode == BinaryOpKind::DivAssign ||
              Opcode == BinaryOpKind::RemAssign ||
              Opcode == BinaryOpKind::AddAssign ||
              Opcode == BinaryOpKind::SubAssign ||
              Opcode == BinaryOpKind::ShlAssign ||
              Opcode == BinaryOpKind::ShrAssign ||
              Opcode == BinaryOpKind::AndAssign ||
              Opcode == BinaryOpKind::OrAssign ||
              Opcode == BinaryOpKind::XorAssign ||
              Opcode == BinaryOpKind::Comma)
                 ? ExprValueKind::VK_LValue
                 : ExprValueKind::VK_PRValue),
        LHS(LHS), RHS(RHS), Opcode(Opcode) {}

  Expr *getLHS() const { return LHS; }
  Expr *getRHS() const { return RHS; }
  BinaryOpKind getOpcode() const { return Opcode; }

  NodeKind getKind() const override { return NodeKind::BinaryOperatorKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A binary operator is type-dependent if either operand is type-dependent
  bool isTypeDependent() const override {
    return LHS->isTypeDependent() || RHS->isTypeDependent();
  }

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
      : Expr(Loc, QualType(),
             (Opcode == UnaryOpKind::Deref || Opcode == UnaryOpKind::PreInc ||
              Opcode == UnaryOpKind::PreDec)
                 ? ExprValueKind::VK_LValue
                 : ExprValueKind::VK_PRValue),
        SubExpr(SubExpr), Opcode(Opcode) {}

  Expr *getSubExpr() const { return SubExpr; }
  UnaryOpKind getOpcode() const { return Opcode; }

  bool isPrefix() const {
    return Opcode != UnaryOpKind::PostInc &&
           Opcode != UnaryOpKind::PostDec;
  }

  NodeKind getKind() const override { return NodeKind::UnaryOperatorKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A unary operator is type-dependent if its subexpression is type-dependent
  bool isTypeDependent() const override {
    return SubExpr->isTypeDependent();
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::UnaryOperatorKind;
  }
};

//===----------------------------------------------------------------------===//
// UnaryExprOrTypeTraitExpr (sizeof/alignof)
//===----------------------------------------------------------------------===//

/// UnaryExprOrTypeTraitKind — The kind of unary expression or type trait.
enum class UnaryExprOrTypeTrait {
  SizeOf,    ///< sizeof
  AlignOf,   ///< alignof
};

/// UnaryExprOrTypeTraitExpr — Represents sizeof/alignof expressions.
///
/// Two forms:
///   1. sizeof(type)  — applies to a type
///   2. sizeof expr   — applies to an expression (without parentheses)
///
/// This matches Clang's UnaryExprOrTypeTraitExpr design.
class UnaryExprOrTypeTraitExpr : public Expr {
  UnaryExprOrTypeTrait Kind;

  /// Type argument (active when IsArgumentType is true).
  QualType ArgType;

  /// Expression argument (active when IsArgumentType is false).
  Expr *ArgExpr = nullptr;

  bool IsArgumentType;

public:
  /// Construct for sizeof(type) / alignof(type).
  UnaryExprOrTypeTraitExpr(SourceLocation Loc, UnaryExprOrTypeTrait K,
                            QualType T)
      : Expr(Loc), Kind(K), ArgType(T), ArgExpr(nullptr),
        IsArgumentType(true) {}

  /// Construct for sizeof expr / alignof expr (applied to expression).
  UnaryExprOrTypeTraitExpr(SourceLocation Loc, UnaryExprOrTypeTrait K,
                            Expr *E)
      : Expr(Loc), Kind(K), ArgExpr(E), IsArgumentType(false) {}

  /// Get the kind of trait (sizeof or alignof).
  UnaryExprOrTypeTrait getTraitKind() const { return Kind; }

  /// True if the argument is a type (sizeof(type)).
  bool isArgumentType() const { return IsArgumentType; }

  /// Get the type argument (only valid when isArgumentType() is true).
  QualType getArgumentType() const {
    assert(IsArgumentType && "Argument is not a type");
    return ArgType;
  }

  /// Get the expression argument (only valid when isArgumentType() is false).
  Expr *getArgumentExpr() const {
    assert(!IsArgumentType && "Argument is not an expression");
    return ArgExpr;
  }

  /// Get the type of the argument (works for both forms).
  /// For type form, returns the type directly.
  /// For expr form, returns the expression's type.
  QualType getTypeOfArgument() const {
    if (IsArgumentType)
      return ArgType;
    return ArgExpr ? ArgExpr->getType() : QualType();
  }

  NodeKind getKind() const override {
    return NodeKind::UnaryExprOrTypeTraitExprKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// sizeof/alignof is type-dependent if the argument type is dependent
  bool isTypeDependent() const override {
    if (IsArgumentType)
      return ArgType.isNull() ? false :
             ArgType->isDependentType();
    return ArgExpr ? ArgExpr->isTypeDependent() : false;
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::UnaryExprOrTypeTraitExprKind;
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

  /// A conditional operator is type-dependent if any of its operands is type-dependent
  bool isTypeDependent() const override {
    return Cond->isTypeDependent() || TrueExpr->isTypeDependent() ||
           FalseExpr->isTypeDependent();
  }

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

  /// A call expression is type-dependent if the callee or any argument is type-dependent
  bool isTypeDependent() const override {
    if (Callee->isTypeDependent())
      return true;
    for (const auto *Arg : Args) {
      if (Arg->isTypeDependent())
        return true;
    }
    return false;
  }

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

  // Inherits isTypeDependent() from CallExpr

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXMemberCallExprKind;
  }
};

/// CXXConstructExpr - Constructor call expression.
class CXXConstructExpr : public Expr {
  CXXConstructorDecl *Constructor;
  llvm::SmallVector<Expr *, 4> Args;

public:
  CXXConstructExpr(SourceLocation Loc, llvm::ArrayRef<Expr *> Args,
                   CXXConstructorDecl *Ctor = nullptr)
      : Expr(Loc), Constructor(Ctor), Args(Args.begin(), Args.end()) {}

  llvm::ArrayRef<Expr *> getArgs() const { return Args; }
  unsigned getNumArgs() const { return Args.size(); }

  /// 获取被调用的构造函数声明（可能为 nullptr）
  CXXConstructorDecl *getConstructor() const { return Constructor; }
  void setConstructor(CXXConstructorDecl *Ctor) { Constructor = Ctor; }

  NodeKind getKind() const override { return NodeKind::CXXConstructExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A constructor call is type-dependent if any argument is type-dependent
  bool isTypeDependent() const override {
    for (const auto *Arg : Args) {
      if (Arg->isTypeDependent())
        return true;
    }
    return false;
  }

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

  // Inherits isTypeDependent() from CXXConstructExpr

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXTemporaryObjectExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Initialization Expressions
//===----------------------------------------------------------------------===//

/// InitListExpr - Represents brace-enclosed initializer list.
/// Example: int arr[] = {1, 2, 3}; or Point p = {1.0, 2.0};
class InitListExpr : public Expr {
  llvm::SmallVector<Expr *, 8> Inits;
  SourceLocation LBraceLoc;
  SourceLocation RBraceLoc;

public:
  InitListExpr(SourceLocation LBraceLoc, llvm::ArrayRef<Expr *> Inits,
               SourceLocation RBraceLoc)
      : Expr(LBraceLoc), Inits(Inits.begin(), Inits.end()),
        LBraceLoc(LBraceLoc), RBraceLoc(RBraceLoc) {}

  llvm::ArrayRef<Expr *> getInits() const { return Inits; }
  unsigned getNumInits() const { return Inits.size(); }
  SourceLocation getLBraceLoc() const { return LBraceLoc; }
  SourceLocation getRBraceLoc() const { return RBraceLoc; }

  NodeKind getKind() const override { return NodeKind::InitListExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// An init list is type-dependent if any of its initializers is type-dependent
  bool isTypeDependent() const override {
    for (const auto *Init : Inits) {
      if (Init->isTypeDependent())
        return true;
    }
    return false;
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::InitListExprKind;
  }
};

/// DesignatedInitExpr - Represents a designated initializer (C++20).
/// Example: Point p = {.x = 1, .y = 2};
class DesignatedInitExpr : public Expr {
public:
  /// Designator - Represents a single designator.
  class Designator {
  public:
    enum DesignatorKind {
      FieldDesignator, // .field_name
      ArrayDesignator  // [index]
    };

  private:
    DesignatorKind Kind;
    llvm::StringRef FieldName; // For field designator
    Expr *IndexExpr;           // For array designator
    SourceLocation Loc;

  public:
    Designator(llvm::StringRef FieldName, SourceLocation Loc)
        : Kind(FieldDesignator), FieldName(FieldName), IndexExpr(nullptr),
          Loc(Loc) {}

    Designator(Expr *IndexExpr, SourceLocation Loc)
        : Kind(ArrayDesignator), IndexExpr(IndexExpr), Loc(Loc) {}

    DesignatorKind getKind() const { return Kind; }
    bool isFieldDesignator() const { return Kind == FieldDesignator; }
    bool isArrayDesignator() const { return Kind == ArrayDesignator; }

    llvm::StringRef getFieldName() const { return FieldName; }
    Expr *getIndexExpr() const { return IndexExpr; }
    SourceLocation getLocation() const { return Loc; }
  };

private:
  llvm::SmallVector<Designator, 4> Designators;
  Expr *Init; // The initializer expression

public:
  DesignatedInitExpr(SourceLocation Loc, llvm::ArrayRef<Designator> Desigs,
                     Expr *Init)
      : Expr(Loc), Designators(Desigs.begin(), Desigs.end()), Init(Init) {}

  llvm::ArrayRef<Designator> getDesignators() const { return Designators; }
  unsigned getNumDesignators() const { return Designators.size(); }
  Expr *getInit() const { return Init; }

  NodeKind getKind() const override { return NodeKind::DesignatedInitExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A designated init is type-dependent if the init expression is type-dependent
  bool isTypeDependent() const override {
    return Init->isTypeDependent();
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::DesignatedInitExprKind;
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

  /// 'this' is never type-dependent
  bool isTypeDependent() const override { return false; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXThisExprKind;
  }
};

/// CXXNewExpr - New expression.
class CXXNewExpr : public Expr {
  Expr *ArraySize;
  Expr *Initializer;
  QualType AllocatedType; ///< 被分配的类型 T（非指针）

public:
  CXXNewExpr(SourceLocation Loc, Expr *ArraySize, Expr *Initializer,
             QualType AllocatedType = QualType())
      : Expr(Loc), ArraySize(ArraySize), Initializer(Initializer),
        AllocatedType(AllocatedType) {}

  Expr *getArraySize() const { return ArraySize; }
  Expr *getInitializer() const { return Initializer; }
  QualType getAllocatedType() const { return AllocatedType; }
  bool isArray() const { return ArraySize != nullptr; }

  NodeKind getKind() const override { return NodeKind::CXXNewExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A new expression is type-dependent if array size or initializer is type-dependent
  bool isTypeDependent() const override {
    return (ArraySize && ArraySize->isTypeDependent()) ||
           (Initializer && Initializer->isTypeDependent());
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXNewExprKind;
  }
};

/// CXXDeleteExpr - Delete expression.
class CXXDeleteExpr : public Expr {
  Expr *Argument;
  bool IsArray;
  QualType AllocatedType; ///< 被删除的元素类型 T（非指针）

public:
  CXXDeleteExpr(SourceLocation Loc, Expr *Argument, bool IsArray,
                QualType AllocatedType = QualType())
      : Expr(Loc), Argument(Argument), IsArray(IsArray),
        AllocatedType(AllocatedType) {}

  Expr *getArgument() const { return Argument; }
  bool isArrayForm() const { return IsArray; }
  QualType getAllocatedType() const { return AllocatedType; }

  NodeKind getKind() const override { return NodeKind::CXXDeleteExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A delete expression is type-dependent if its argument is type-dependent
  bool isTypeDependent() const override {
    return Argument->isTypeDependent();
  }

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

  /// A throw expression is type-dependent if its subexpression is type-dependent
  bool isTypeDependent() const override {
    return SubExpr->isTypeDependent();
  }

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

  /// A cast expression is type-dependent if its subexpression is type-dependent
  bool isTypeDependent() const override {
    return SubExpr->isTypeDependent();
  }

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

  // Inherits isTypeDependent() from CastExpr

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXStaticCastExprKind;
  }
};

/// CXXDynamicCastExpr - dynamic_cast expression.
class CXXDynamicCastExpr : public CastExpr {
  QualType DestType;

public:
  CXXDynamicCastExpr(SourceLocation Loc, Expr *SubExpr, QualType DestType)
      : CastExpr(Loc, SubExpr, CastKind::CXXDynamic), DestType(DestType) {}

  QualType getDestType() const { return DestType; }

  NodeKind getKind() const override { return NodeKind::CXXDynamicCastExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  // Inherits isTypeDependent() from CastExpr

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

  // Inherits isTypeDependent() from CastExpr

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

  // Inherits isTypeDependent() from CastExpr

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

  // Inherits isTypeDependent() from CastExpr

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CStyleCastExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Modern C++ Expressions
//===----------------------------------------------------------------------===//

/// LambdaCapture - Represents a lambda capture.
struct LambdaCapture {
  enum CaptureKind {
    ByCopy,    // [=]
    ByRef,     // [&]
    InitCopy   // [x = expr]
  };
  
  CaptureKind Kind = ByCopy;
  StringRef Name;
  class Expr *InitExpr = nullptr;  // For init captures
  SourceLocation Loc;
  class NamedDecl *CapturedDecl = nullptr;  // P7.1.5: The captured variable declaration
  
  LambdaCapture() = default;
  LambdaCapture(CaptureKind K, StringRef N, class Expr *I, SourceLocation L)
      : Kind(K), Name(N), InitExpr(I), Loc(L) {}
};

/// LambdaExpr - Lambda expression.
class LambdaExpr : public Expr {
  CXXRecordDecl *ClosureClass;  // P7.1.5: The closure type
  llvm::SmallVector<LambdaCapture, 4> Captures;
  llvm::SmallVector<ParmVarDecl *, 4> Params;
  Stmt *Body;  // CompoundStmt
  bool IsMutable = false;
  QualType ReturnType;
  SourceLocation LBraceLoc;
  SourceLocation RBraceLoc;
  TemplateParameterList *TemplateParams = nullptr; // C++20: lambda-template
  class AttributeListDecl *Attrs = nullptr;         // C++23: lambda attributes
  
  // P7.1.5: Map from captured VarDecl to field index in closure
  llvm::DenseMap<const class VarDecl *, unsigned> CapturedVarsMap;

public:
  LambdaExpr(SourceLocation Loc, CXXRecordDecl *Closure,
             llvm::ArrayRef<LambdaCapture> Captures,
             llvm::ArrayRef<ParmVarDecl *> Params, Stmt *Body,
             bool IsMutable = false, QualType ReturnType = QualType(),
             SourceLocation LBraceLoc = SourceLocation(),
             SourceLocation RBraceLoc = SourceLocation(),
             TemplateParameterList *TemplateParams = nullptr,
             class AttributeListDecl *Attrs = nullptr)
      : Expr(Loc), ClosureClass(Closure),
        Captures(Captures.begin(), Captures.end()),
        Params(Params.begin(), Params.end()), Body(Body),
        IsMutable(IsMutable), ReturnType(ReturnType),
        LBraceLoc(LBraceLoc), RBraceLoc(RBraceLoc),
        TemplateParams(TemplateParams), Attrs(Attrs) {}

  llvm::ArrayRef<LambdaCapture> getCaptures() const { return Captures; }
  llvm::ArrayRef<ParmVarDecl *> getParams() const { return Params; }
  Stmt *getBody() const { return Body; }
  bool isMutable() const { return IsMutable; }
  QualType getReturnType() const { return ReturnType; }
  TemplateParameterList *getTemplateParameters() const { return TemplateParams; }
  class AttributeListDecl *getAttributes() const { return Attrs; }
  
  // P7.1.5: Lambda closure class access
  CXXRecordDecl *getClosureClass() const { return ClosureClass; }

  NodeKind getKind() const override { return NodeKind::LambdaExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A lambda expression is never type-dependent (it has a unique closure type)
  bool isTypeDependent() const override { return false; }
  
  // P7.1.5: Get captured variable to field index mapping
  const llvm::DenseMap<const class VarDecl *, unsigned> &getCapturedVarsMap() const {
    return CapturedVarsMap;
  }
  
  // P7.1.5: Set captured variable mapping (called by Sema)
  void setCapturedVar(const class VarDecl *VD, unsigned FieldIndex) {
    CapturedVarsMap[VD] = FieldIndex;
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::LambdaExprKind;
  }
};

/// Requirement - Base class for requires expression requirements.
class Requirement {
public:
  enum RequirementKind {
    Type,        // typename T
    SimpleExpr,  // expression
    Nested,      // { requires ... }
    Compound     // { statement-seq }
  };
  
private:
  RequirementKind Kind;
  SourceLocation Loc;
  
public:
  Requirement(RequirementKind K, SourceLocation Loc) : Kind(K), Loc(Loc) {}
  virtual ~Requirement() = default;
  
  RequirementKind getKind() const { return Kind; }
  SourceLocation getLocation() const { return Loc; }
  virtual void dump(raw_ostream &OS, unsigned Indent = 0) const = 0;
};

/// TypeRequirement - Requires clause type requirement.
class TypeRequirement : public Requirement {
  QualType Type;
  
public:
  TypeRequirement(QualType T, SourceLocation Loc)
      : Requirement(RequirementKind::Type, Loc), Type(T) {}
  
  QualType getType() const { return Type; }
  
  void dump(raw_ostream &OS, unsigned Indent = 0) const override;
};

/// ExprRequirement - Requires clause expression requirement.
class ExprRequirement : public Requirement {
  class Expr *Expression;
  bool IsNoexcept = false;

public:
  ExprRequirement(class Expr *E, bool Noexcept, SourceLocation Loc)
      : Requirement(RequirementKind::SimpleExpr, Loc), Expression(E),
        IsNoexcept(Noexcept) {}

  class Expr *getExpression() const { return Expression; }
  bool isNoexcept() const { return IsNoexcept; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;
};

/// CompoundRequirement - Requires clause compound requirement.
/// Example: { expression } noexcept? return-type-requirement?
class CompoundRequirement : public Requirement {
  class Expr *Expression;
  class Stmt *Body;             // Compound statement body
  bool IsNoexcept = false;
  QualType ReturnType;          // Optional return type requirement

public:
  CompoundRequirement(class Expr *E, class Stmt *Body, bool Noexcept,
                      QualType RetType, SourceLocation Loc)
      : Requirement(RequirementKind::Compound, Loc), Expression(E),
        Body(Body), IsNoexcept(Noexcept), ReturnType(RetType) {}

  class Expr *getExpression() const { return Expression; }
  class Stmt *getBody() const { return Body; }
  bool isNoexcept() const { return IsNoexcept; }
  QualType getReturnType() const { return ReturnType; }
  bool hasReturnTypeRequirement() const { return !ReturnType.isNull(); }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;
};

/// NestedRequirement - Requires clause nested requirement.
/// Example: requires constraint-expression
class NestedRequirement : public Requirement {
  class Expr *Constraint;  // The constraint expression

public:
  NestedRequirement(class Expr *Constraint, SourceLocation Loc)
      : Requirement(RequirementKind::Nested, Loc), Constraint(Constraint) {}

  class Expr *getConstraint() const { return Constraint; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;
};

//===----------------------------------------------------------------------===//
// Requirement type casting helpers
//===----------------------------------------------------------------------===//

/// dyn_cast for Requirement hierarchy
template <typename T>
inline T *dyn_cast(Requirement *Req) {
  if (!Req) return nullptr;
  // Check if the requirement kind matches the target type
  bool matches = false;
  if constexpr (std::is_same_v<T, TypeRequirement>) {
    matches = Req->getKind() == Requirement::RequirementKind::Type;
  } else if constexpr (std::is_same_v<T, ExprRequirement>) {
    matches = Req->getKind() == Requirement::RequirementKind::SimpleExpr;
  } else if constexpr (std::is_same_v<T, CompoundRequirement>) {
    matches = Req->getKind() == Requirement::RequirementKind::Compound;
  } else if constexpr (std::is_same_v<T, NestedRequirement>) {
    matches = Req->getKind() == Requirement::RequirementKind::Nested;
  }
  return matches ? static_cast<T *>(Req) : nullptr;
}

template <typename T>
inline const T *dyn_cast(const Requirement *Req) {
  if (!Req) return nullptr;
  bool matches = false;
  if constexpr (std::is_same_v<T, TypeRequirement>) {
    matches = Req->getKind() == Requirement::RequirementKind::Type;
  } else if constexpr (std::is_same_v<T, ExprRequirement>) {
    matches = Req->getKind() == Requirement::RequirementKind::SimpleExpr;
  } else if constexpr (std::is_same_v<T, CompoundRequirement>) {
    matches = Req->getKind() == Requirement::RequirementKind::Compound;
  } else if constexpr (std::is_same_v<T, NestedRequirement>) {
    matches = Req->getKind() == Requirement::RequirementKind::Nested;
  }
  return matches ? static_cast<const T *>(Req) : nullptr;
}

/// RequiresExpr - C++20 requires expression.
class RequiresExpr : public Expr {
  llvm::SmallVector<Requirement *, 4> Requirements;
  SourceLocation RequiresLoc;
  SourceLocation RBraceLoc;

public:
  RequiresExpr(SourceLocation Loc, llvm::ArrayRef<Requirement *> Reqs,
               SourceLocation RequiresLoc = SourceLocation(),
               SourceLocation RBraceLoc = SourceLocation())
      : Expr(Loc), Requirements(Reqs.begin(), Reqs.end()),
        RequiresLoc(RequiresLoc), RBraceLoc(RBraceLoc) {}

  llvm::ArrayRef<Requirement *> getRequirements() const { return Requirements; }

  NodeKind getKind() const override { return NodeKind::RequiresExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A requires expression is type-dependent if any of its requirements contains type-dependent expressions
  bool isTypeDependent() const override {
    for (const auto *Req : Requirements) {
      if (auto *ExprReq = dyn_cast<ExprRequirement>(Req)) {
        if (ExprReq->getExpression()->isTypeDependent())
          return true;
      } else if (auto *CompoundReq = dyn_cast<CompoundRequirement>(Req)) {
        if (CompoundReq->getExpression()->isTypeDependent())
          return true;
      } else if (auto *NestedReq = dyn_cast<NestedRequirement>(Req)) {
        if (NestedReq->getConstraint()->isTypeDependent())
          return true;
      }
    }
    return false;
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::RequiresExprKind;
  }
};

/// CXXFoldExpr - C++17 fold expression.
class CXXFoldExpr : public Expr {
  Expr *LHS = nullptr;      // Left operand (nullptr for unary left fold)
  Expr *RHS = nullptr;      // Right operand (nullptr for unary right fold)
  Expr *Pattern;            // The pack expansion pattern
  BinaryOpKind Op;          // The operator (+, -, *, /, etc.)
  bool IsRightFold;         // True if right fold, false if left fold

public:
  CXXFoldExpr(SourceLocation Loc, Expr *LHS, Expr *RHS, Expr *Pattern,
              BinaryOpKind Op, bool IsRightFold)
      : Expr(Loc), LHS(LHS), RHS(RHS), Pattern(Pattern), Op(Op),
        IsRightFold(IsRightFold) {}

  Expr *getLHS() const { return LHS; }
  Expr *getRHS() const { return RHS; }
  Expr *getPattern() const { return Pattern; }
  BinaryOpKind getOperator() const { return Op; }
  bool isRightFold() const { return IsRightFold; }
  bool isLeftFold() const { return !IsRightFold; }

  NodeKind getKind() const override { return NodeKind::CXXFoldExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A fold expression is type-dependent if any of its operands is type-dependent
  bool isTypeDependent() const override {
    return (LHS && LHS->isTypeDependent()) ||
           (RHS && RHS->isTypeDependent()) ||
           Pattern->isTypeDependent();
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXFoldExprKind;
  }
};

/// PackIndexingExpr - C++26 pack indexing expression.
class PackIndexingExpr : public Expr {
  Expr *Pack;           // The pack expression
  Expr *Index;          // The index expression (constant or runtime)

public:
  PackIndexingExpr(SourceLocation Loc, Expr *Pack, Expr *Index)
      : Expr(Loc), Pack(Pack), Index(Index) {}

  Expr *getPack() const { return Pack; }
  Expr *getIndex() const { return Index; }

  NodeKind getKind() const override { return NodeKind::PackIndexingExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A pack indexing expression is type-dependent if the pack or index is type-dependent
  bool isTypeDependent() const override {
    return Pack->isTypeDependent() || Index->isTypeDependent();
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::PackIndexingExprKind;
  }
};

/// ReflexprExpr - C++26 reflexpr expression.
///
/// Represents `reflexpr(type-id)` or `reflexpr(expression)`.
/// The result type (via getType()) is MetaInfoType (std::meta::info).
///
/// **Clang reference**: Clang's reflection TS uses a similar approach in
/// clang::Sema::ActOnReflectionTraitExpr.
class ReflexprExpr : public Expr {
public:
  /// Discriminator for the kind of operand
  enum OperandKind {
    OK_Type,       ///< Reflecting a type: reflexpr(type-id)
    OK_Expression  ///< Reflecting an expression: reflexpr(expr)
  };

private:
  /// The kind of operand being reflected
  OperandKind OpKind;

  /// The reflected type (valid when OpKind == OK_Type)
  QualType ReflectedType;

  /// The reflected expression (valid when OpKind == OK_Expression)
  Expr *Argument;

public:
  /// Construct for reflexpr(type-id)
  ReflexprExpr(SourceLocation Loc, QualType T,
               QualType ResultTy = QualType())
      : Expr(Loc, ResultTy), OpKind(OK_Type), ReflectedType(T),
        Argument(nullptr) {}

  /// Construct for reflexpr(expression)
  ReflexprExpr(SourceLocation Loc, Expr *Arg,
               QualType ResultTy = QualType())
      : Expr(Loc, ResultTy), OpKind(OK_Expression), ReflectedType(),
        Argument(Arg) {}

  /// Operand kind query
  OperandKind getOperandKind() const { return OpKind; }
  bool reflectsType() const { return OpKind == OK_Type; }
  bool reflectsExpression() const { return OpKind == OK_Expression; }

  /// Accessors
  /// @{
  QualType getReflectedType() const { return ReflectedType; }
  Expr *getArgument() const { return Argument; }
  /// getResultType is an alias for getType() (the MetaInfoType).
  QualType getResultType() const { return getType(); }
  /// @}

  NodeKind getKind() const override { return NodeKind::ReflexprExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A reflexpr expression is type-dependent if its argument is type-dependent
  bool isTypeDependent() const override {
    if (reflectsExpression())
      return Argument && Argument->isTypeDependent();
    return ReflectedType.getTypePtr() && ReflectedType.getTypePtr()->isDependentType();
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
  Expr *Operand;

public:
  CoawaitExpr(SourceLocation Loc, Expr *Operand)
      : Expr(Loc), Operand(Operand) {}

  Expr *getOperand() const { return Operand; }

  NodeKind getKind() const override { return NodeKind::CoawaitExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// A co_await expression is type-dependent if its operand is type-dependent
  bool isTypeDependent() const override {
    return Operand->isTypeDependent();
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CoawaitExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Dependent Name Resolution (Two-Phase Lookup)
//===----------------------------------------------------------------------===//

/// CXXDependentScopeMemberExpr - Represents a member access expression where
/// the base type is dependent (e.g., x.member where x has type T).
///
/// Created during template definition parsing when the base expression's type
/// depends on a template parameter. Resolved during template instantiation by
/// SubstituteExpr which replaces it with a concrete MemberExpr.
///
/// Examples:
///   t.foo()       where t is of type T
///   T::value      where T is a template type parameter
///   this->member  inside a template class
class CXXDependentScopeMemberExpr : public Expr {
  Expr *Base;                    // Base expression (may be null for T::foo)
  QualType BaseType;             // Type of the base (used when Base is null)
  bool IsArrow;                  // true if -> (false if .)
  llvm::StringRef MemberName;    // Name of the member being accessed

public:
  CXXDependentScopeMemberExpr(SourceLocation Loc, Expr *Base,
                               QualType BaseType, bool IsArrow,
                               llvm::StringRef MemberName)
      : Expr(Loc), Base(Base), BaseType(BaseType), IsArrow(IsArrow),
        MemberName(MemberName) {}

  Expr *getBase() const { return Base; }
  QualType getBaseType() const { return BaseType; }
  bool isArrow() const { return IsArrow; }
  llvm::StringRef getMemberName() const { return MemberName; }

  NodeKind getKind() const override {
    return NodeKind::CXXDependentScopeMemberExprKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// Always type-dependent — the member type is unknown until instantiation.
  bool isTypeDependent() const override { return true; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXDependentScopeMemberExprKind;
  }
};

/// DependentScopeDeclRefExpr - Represents a reference to a name in a dependent
/// scope, e.g., T::type or typename T::value_type.
///
/// Created during template definition parsing when a qualified name refers to
/// a dependent type's member. Resolved during instantiation by SubstituteExpr
/// which replaces it with a concrete DeclRefExpr or MemberExpr.
///
/// Examples:
///   T::iterator          where T is a template type parameter
///   Container::value_type where Container is dependent
class DependentScopeDeclRefExpr : public Expr {
  NestedNameSpecifier *Qualifier;  // The qualifying scope (e.g., T::)
  llvm::StringRef DeclName;        // The name being referenced (e.g., type)

public:
  DependentScopeDeclRefExpr(SourceLocation Loc,
                              NestedNameSpecifier *Qualifier,
                              llvm::StringRef DeclName)
      : Expr(Loc), Qualifier(Qualifier), DeclName(DeclName) {}

  NestedNameSpecifier *getQualifier() const { return Qualifier; }
  llvm::StringRef getDeclName() const { return DeclName; }

  NodeKind getKind() const override {
    return NodeKind::DependentScopeDeclRefExprKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  /// Always type-dependent — the referenced type is unknown until instantiation.
  bool isTypeDependent() const override { return true; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::DependentScopeDeclRefExprKind;
  }
};

//===----------------------------------------------------------------------===//
// Error Recovery
//===----------------------------------------------------------------------===//

/// DecayCopyExpr - Decay-copy expression auto(expr) or auto{expr} (P0849R8).
///
/// Represents a C++23 decay-copy expression that performs decay on the
/// subexpression (removing references, top-level cv-qualifiers, converting
/// arrays/functions to pointers) and creates a temporary prvalue.
///
/// Example:
///   auto x = auto(expr);   // copy-initialization from decay-copy
///   auto y = auto{expr};   // direct-initialization from decay-copy
class DecayCopyExpr : public Expr {
  Expr *SubExpr;
  bool IsDirectInit;  // true = auto{expr}, false = auto(expr)

public:
  DecayCopyExpr(SourceLocation Loc, Expr *Sub, bool DirectInit)
      : Expr(Loc, QualType(), ExprValueKind::VK_PRValue),
        SubExpr(Sub), IsDirectInit(DirectInit) {}

  Expr *getSubExpr() const { return SubExpr; }
  bool isDirectInit() const { return IsDirectInit; }

  NodeKind getKind() const override { return NodeKind::DecayCopyExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  bool isTypeDependent() const override {
    return SubExpr && SubExpr->isTypeDependent();
  }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::DecayCopyExprKind;
  }
};

/// RecoveryExpr - Error recovery expression.
class RecoveryExpr : public Expr {
public:
  RecoveryExpr(SourceLocation Loc) : Expr(Loc) {}

  NodeKind getKind() const override { return NodeKind::RecoveryExprKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "RecoveryExpr\n";
  }

  /// A recovery expression is never type-dependent (it's a placeholder)
  bool isTypeDependent() const override { return false; }

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::RecoveryExprKind;
  }
};

} // namespace blocktype
