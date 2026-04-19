//===--- Decl.h - Declaration AST Nodes --------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Decl class and all declaration AST nodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/DeclContext.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/AST/Type.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

class Expr;
class Stmt;
class CompoundStmt;
class ParmVarDecl; // Forward declaration
class CXXMethodDecl; // Forward declaration
class CXXRecordDecl; // Forward declaration
class AccessSpecDecl; // Forward declaration
class ClassTemplateSpecializationDecl; // Forward declaration
class ClassTemplatePartialSpecializationDecl; // Forward declaration
class VarTemplateSpecializationDecl; // Forward declaration

//===----------------------------------------------------------------------===//
// AccessSpecifier - Access control enumeration
//===----------------------------------------------------------------------===//

/// AccessSpecifier - Access control for class members.
enum class AccessSpecifier {
  AS_none,       ///< Not a class member (sentinel value)
  AS_public,
  AS_protected,
  AS_private
};

//===----------------------------------------------------------------------===//
// Decl - Base class for all declarations
//===----------------------------------------------------------------------===//

/// Decl - Base class for all declaration nodes.
class Decl : public ASTNode {
protected:
  bool Used = false;
  Decl(SourceLocation Loc) : ASTNode(Loc) {}

public:
  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::NamedDeclKind &&
           N->getKind() < NodeKind::NumNodeKinds;
  }

  bool isUsed() const { return Used; }
  void setUsed(bool U = true) { Used = U; }
};

//===----------------------------------------------------------------------===//
// NamedDecl - Base class for declarations with names
//===----------------------------------------------------------------------===//

/// NamedDecl - Base class for declarations that have a name.
class NamedDecl : public Decl {
  llvm::StringRef Name;

protected:
  NamedDecl(SourceLocation Loc, llvm::StringRef Name)
      : Decl(Loc), Name(Name) {}

public:
  llvm::StringRef getName() const { return Name; }

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::NamedDeclKind &&
           N->getKind() < NodeKind::TranslationUnitDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// ValueDecl - Base class for declarations with types
//===----------------------------------------------------------------------===//

/// ValueDecl - Base class for declarations that have a type.
class ValueDecl : public NamedDecl {
protected:
  QualType T;

  ValueDecl(SourceLocation Loc, llvm::StringRef Name, QualType T)
      : NamedDecl(Loc, Name), T(T) {}

public:
  QualType getType() const { return T; }
  void setType(QualType NewType) { T = NewType; }

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::ValueDeclKind &&
           N->getKind() < NodeKind::TypeDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// VarDecl - Variable declaration
//===----------------------------------------------------------------------===//

/// VarDecl - Variable declaration.
class VarDecl : public ValueDecl {
protected:
  Expr *Init;
  bool IsStatic;
  bool IsConstexpr;

public:
  VarDecl(SourceLocation Loc, llvm::StringRef Name, QualType T, 
          Expr *Init = nullptr, bool IsStatic = false, bool IsConstexpr = false)
      : ValueDecl(Loc, Name, T), Init(Init), IsStatic(IsStatic),
        IsConstexpr(IsConstexpr) {}

  Expr *getInit() const { return Init; }
  void setInit(Expr *I) { Init = I; }
  bool isStatic() const { return IsStatic; }
  void setStatic(bool S) { IsStatic = S; }
  bool isConstexpr() const { return IsConstexpr; }
  void setConstexpr(bool C = true) { IsConstexpr = C; }

  NodeKind getKind() const override { return NodeKind::VarDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::VarDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// FunctionDecl - Function declaration
//===----------------------------------------------------------------------===//

/// FunctionDecl - Function declaration.
class FunctionDecl : public ValueDecl {
  llvm::SmallVector<ParmVarDecl *, 8> Params;
  Stmt *Body; // CompoundStmt or nullptr
  bool IsInline;
  bool IsConstexpr;
  bool IsConsteval;
  bool HasNoexceptSpec;
  bool NoexceptValue; // true if noexcept(true), false if noexcept(false)
  Expr *NoexceptExpr; // noexcept(expression)
  class AttributeListDecl *Attrs = nullptr; // [[noreturn]], [[nodiscard]] etc.

  // P7.1.1: Deducing this (P0847R7) - Explicit object parameter
  ParmVarDecl *ExplicitObjectParam = nullptr;
  bool HasExplicitObjectParam = false;

public:
  FunctionDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
               llvm::ArrayRef<ParmVarDecl *> Params, Stmt *Body = nullptr,
               bool IsInline = false, bool IsConstexpr = false,
               bool IsConsteval = false,
               bool HasNoexceptSpec = false, bool NoexceptValue = false,
               Expr *NoexceptExpr = nullptr,
               class AttributeListDecl *Attrs = nullptr)
      : ValueDecl(Loc, Name, T), Params(Params.begin(), Params.end()),
        Body(Body), IsInline(IsInline), IsConstexpr(IsConstexpr),
        IsConsteval(IsConsteval),
        HasNoexceptSpec(HasNoexceptSpec), NoexceptValue(NoexceptValue),
        NoexceptExpr(NoexceptExpr), Attrs(Attrs) {}

  llvm::ArrayRef<ParmVarDecl *> getParams() const { return Params; }
  unsigned getNumParams() const { return Params.size(); }
  ParmVarDecl *getParamDecl(unsigned i) const { return Params[i]; }

  Stmt *getBody() const { return Body; }
  void setBody(Stmt *B) { Body = B; }

  bool isInline() const { return IsInline; }
  bool isConstexpr() const { return IsConstexpr; }
  bool isConsteval() const { return IsConsteval; }
  bool hasNoexceptSpec() const { return HasNoexceptSpec; }
  bool getNoexceptValue() const { return NoexceptValue; }
  Expr *getNoexceptExpr() const { return NoexceptExpr; }

  /// Whether this function is variadic (has ... parameter).
  bool isVariadic() const;

  /// Attribute access.
  class AttributeListDecl *getAttrs() const { return Attrs; }
  void setAttrs(class AttributeListDecl *A) { Attrs = A; }

  /// Check if this function has a specific attribute by name.
  bool hasAttr(llvm::StringRef Name) const;

  //===------------------------------------------------------------------===//
  // P7.1.1: Deducing this (P0847R7) - Explicit object parameter support
  //===------------------------------------------------------------------===//

  /// Get the explicit object parameter, if any.
  /// Returns nullptr if this function does not have an explicit object parameter.
  ParmVarDecl *getExplicitObjectParam() const { return ExplicitObjectParam; }

  /// Set the explicit object parameter for deducing this.
  void setExplicitObjectParam(ParmVarDecl *P) {
    ExplicitObjectParam = P;
    HasExplicitObjectParam = (P != nullptr);
  }

  /// Whether this function has an explicit object parameter (deducing this).
  bool hasExplicitObjectParam() const { return HasExplicitObjectParam; }

  /// Get the effective 'this' type, considering deducing this.
  /// If hasExplicitObjectParam(), returns the parameter type.
  /// Otherwise, returns the traditional this pointer type.
  QualType getThisType(ASTContext &Ctx) const;

  NodeKind getKind() const override { return NodeKind::FunctionDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::FunctionDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// ParmVarDecl - Parameter variable declaration
//===----------------------------------------------------------------------===//

/// ParmVarDecl - Parameter variable declaration.
class ParmVarDecl : public VarDecl {
  unsigned Index;
  bool IsExplicitObjectParam = false; // P7.1.1: deducing this

public:
  ParmVarDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
              unsigned Index, Expr *DefaultArg = nullptr)
      : VarDecl(Loc, Name, T, DefaultArg), Index(Index) {}

  unsigned getFunctionScopeIndex() const { return Index; }
  Expr *getDefaultArg() const { return getInit(); }

  // P7.1.1: Explicit object parameter (deducing this)
  bool isExplicitObjectParam() const { return IsExplicitObjectParam; }
  void setExplicitObjectParam(bool V = true) { IsExplicitObjectParam = V; }

  NodeKind getKind() const override { return NodeKind::ParmVarDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ParmVarDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// FieldDecl - Field declaration (class/struct member)
//===----------------------------------------------------------------------===//

/// FieldDecl - Field declaration (member of a class/struct/union).
class FieldDecl : public ValueDecl {
  Expr *BitWidth;
  Expr *InClassInitializer;
  bool IsMutable;
  AccessSpecifier Access;

public:
  FieldDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
            Expr *BitWidth = nullptr, bool IsMutable = false,
            Expr *InClassInit = nullptr,
            AccessSpecifier Access = AccessSpecifier::AS_private)
      : ValueDecl(Loc, Name, T), BitWidth(BitWidth),
        InClassInitializer(InClassInit), IsMutable(IsMutable), Access(Access) {}

  Expr *getBitWidth() const { return BitWidth; }
  bool isMutable() const { return IsMutable; }
  Expr *getInClassInitializer() const { return InClassInitializer; }
  bool hasInClassInitializer() const { return InClassInitializer != nullptr; }
  
  AccessSpecifier getAccess() const { return Access; }
  void setAccess(AccessSpecifier A) { Access = A; }
  
  bool isPublic() const { return Access == AccessSpecifier::AS_public; }
  bool isProtected() const { return Access == AccessSpecifier::AS_protected; }
  bool isPrivate() const { return Access == AccessSpecifier::AS_private; }

  NodeKind getKind() const override { return NodeKind::FieldDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::FieldDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// EnumConstantDecl - Enum constant declaration
//===----------------------------------------------------------------------===//

/// EnumConstantDecl - Enum constant declaration.
class EnumConstantDecl : public ValueDecl {
  Expr *InitExpr;
  llvm::APSInt Val;
  bool HasVal = false;

public:
  EnumConstantDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
                   Expr *InitExpr = nullptr)
      : ValueDecl(Loc, Name, T), InitExpr(InitExpr),
        Val(llvm::APInt(32, 0)) {}

  /// Returns the initializer expression (may be null).
  Expr *getInitExpr() const { return InitExpr; }

  /// Returns the cached enum value (valid only if hasVal() is true).
  llvm::APSInt getVal() const { return Val; }

  /// Sets the cached enum value (called during Sema analysis).
  void setVal(llvm::APSInt V) { Val = V; HasVal = true; }

  /// Returns true if the value has been evaluated and cached.
  bool hasVal() const { return HasVal; }

  // Legacy alias for getInitExpr()
  Expr *getInitVal() const { return InitExpr; }

  NodeKind getKind() const override { return NodeKind::EnumConstantDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::EnumConstantDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// TypeDecl - Base class for type declarations
//===----------------------------------------------------------------------===//

/// TypeDecl - Base class for declarations that introduce a type.
class TypeDecl : public NamedDecl {
protected:
  TypeDecl(SourceLocation Loc, llvm::StringRef Name)
      : NamedDecl(Loc, Name) {}

public:
  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::TypeDeclKind &&
           N->getKind() < NodeKind::NamespaceDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// TypedefDecl - Typedef declaration
//===----------------------------------------------------------------------===//

/// TypedefNameDecl - Base class for typedef and type alias declarations.
class TypedefNameDecl : public TypeDecl {
protected:
  QualType UnderlyingType;

public:
  TypedefNameDecl(SourceLocation Loc, llvm::StringRef Name, QualType Underlying)
      : TypeDecl(Loc, Name), UnderlyingType(Underlying) {}

  QualType getUnderlyingType() const { return UnderlyingType; }

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::TypedefDeclKind &&
           N->getKind() <= NodeKind::TypeAliasDeclKind;
  }
};

/// TypedefDecl - Typedef declaration.
class TypedefDecl : public TypedefNameDecl {
public:
  TypedefDecl(SourceLocation Loc, llvm::StringRef Name, QualType Underlying)
      : TypedefNameDecl(Loc, Name, Underlying) {}

  NodeKind getKind() const override { return NodeKind::TypedefDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TypedefDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// TagDecl - Base class for class/struct/union/enum declarations
//===----------------------------------------------------------------------===//

/// TagDecl - Base class for class, struct, union, and enum declarations.
class TagDecl : public TypeDecl {
public:
  enum TagKind {
    TK_struct,
    TK_class,
    TK_union,
    TK_enum
  };

private:
  TagKind TagKindValue;
  bool IsCompleteDefinition = false;

protected:
  TagDecl(SourceLocation Loc, llvm::StringRef Name, TagKind TK)
      : TypeDecl(Loc, Name), TagKindValue(TK) {}

public:
  TagKind getTagKind() const { return TagKindValue; }
  bool isStruct() const { return TagKindValue == TK_struct; }
  bool isClass() const { return TagKindValue == TK_class; }
  bool isUnion() const { return TagKindValue == TK_union; }
  bool isEnum() const { return TagKindValue == TK_enum; }

  /// isCompleteDefinition - Return true if this is a complete definition
  /// (has a body with '{...}'). Forward declarations like 'class Foo;' are not.
  bool isCompleteDefinition() const { return IsCompleteDefinition; }
  void setCompleteDefinition(bool V = true) { IsCompleteDefinition = V; }

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::TagDeclKind &&
           N->getKind() < NodeKind::NamespaceDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// EnumDecl - Enum declaration
//===----------------------------------------------------------------------===//

/// EnumDecl - Enum declaration.
class EnumDecl : public TagDecl, public DeclContext {
  llvm::SmallVector<EnumConstantDecl *, 8> Enumerators;
  QualType UnderlyingType; // The underlying type (e.g., int for "enum E : int")
  bool IsScoped = false;   // Whether this is an enum class/struct

public:
  EnumDecl(SourceLocation Loc, llvm::StringRef Name)
      : TagDecl(Loc, Name, TK_enum), DeclContext(DeclContextKind::Enum) {}

  llvm::ArrayRef<EnumConstantDecl *> enumerators() const { return Enumerators; }
  void addEnumerator(EnumConstantDecl *D) {
    Enumerators.push_back(D);
    DeclContext::addDecl(D);
  }

  QualType getUnderlyingType() const { return UnderlyingType; }
  void setUnderlyingType(QualType T) { UnderlyingType = T; }

  bool isScoped() const { return IsScoped; }
  void setScoped(bool Scoped = true) { IsScoped = Scoped; }

  NodeKind getKind() const override { return NodeKind::EnumDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::EnumDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// RecordDecl - Record declaration (class/struct/union)
//===----------------------------------------------------------------------===//

/// RecordDecl - Record declaration (class, struct, or union).
class RecordDecl : public TagDecl {
  llvm::SmallVector<FieldDecl *, 8> Fields;

public:
  RecordDecl(SourceLocation Loc, llvm::StringRef Name, TagKind TK)
      : TagDecl(Loc, Name, TK) {}

  llvm::ArrayRef<FieldDecl *> fields() const { return Fields; }
  void addField(FieldDecl *F) { Fields.push_back(F); }

  NodeKind getKind() const override { return NodeKind::RecordDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::RecordDeclKind ||
           N->getKind() == NodeKind::CXXRecordDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// CXXRecordDecl - C++ class declaration
//===----------------------------------------------------------------------===//

/// CXXRecordDecl - C++ class declaration with additional features like
/// base classes, member functions, access control, etc.
class CXXRecordDecl : public RecordDecl, public DeclContext {
public:
  /// BaseSpecifier - Describes a base class.
  class BaseSpecifier {
    QualType Type;
    SourceLocation Loc;
    bool IsVirtual;
    bool IsBaseOfClass;
    unsigned Access;

  public:
    BaseSpecifier(QualType T, SourceLocation Loc, bool IsVirtual,
                  bool IsBaseOfClass, unsigned Access)
        : Type(T), Loc(Loc), IsVirtual(IsVirtual),
          IsBaseOfClass(IsBaseOfClass), Access(Access) {}

    QualType getType() const { return Type; }
    SourceLocation getLocation() const { return Loc; }
    bool isVirtual() const { return IsVirtual; }
    bool isBaseOfClass() const { return IsBaseOfClass; }
    unsigned getAccessSpecifier() const { return Access; }
  };

private:
  llvm::SmallVector<BaseSpecifier, 4> Bases;
  llvm::SmallVector<CXXMethodDecl *, 16> Methods;
  llvm::SmallVector<Decl *, 32> Members;
  bool HasDefaultConstructor;
  bool HasCopyConstructor;
  bool HasMoveConstructor;
  bool HasDestructor;
  unsigned CurrentAccess; // Current access specifier (0=private, 1=protected, 2=public)

public:
  CXXRecordDecl(SourceLocation Loc, llvm::StringRef Name, TagKind TK = TK_class)
      : RecordDecl(Loc, Name, TK), DeclContext(DeclContextKind::CXXRecord),
        HasDefaultConstructor(false), HasCopyConstructor(false),
        HasMoveConstructor(false), HasDestructor(false),
        CurrentAccess(TK == TK_class ? 0 : 2) {}

  // Base classes
  llvm::ArrayRef<BaseSpecifier> bases() const { return Bases; }
  void addBase(const BaseSpecifier &Base) { Bases.push_back(Base); }
  unsigned getNumBases() const { return Bases.size(); }

  // Methods
  llvm::ArrayRef<CXXMethodDecl *> methods() const { return Methods; }
  void addMethod(CXXMethodDecl *M) { Methods.push_back(M); }

  // Members
  llvm::ArrayRef<Decl *> members() const { return Members; }
  void addMember(Decl *D) { Members.push_back(D); DeclContext::addDecl(D); }

  /// Access the DeclContext interface.
  DeclContext *getDeclContext() { return this; }
  const DeclContext *getDeclContext() const { return this; }

  // Special members
  bool hasDefaultConstructor() const { return HasDefaultConstructor; }
  bool hasCopyConstructor() const { return HasCopyConstructor; }
  bool hasMoveConstructor() const { return HasMoveConstructor; }
  bool hasDestructor() const { return HasDestructor; }

  // Access control
  unsigned getCurrentAccess() const { return CurrentAccess; }
  void setCurrentAccess(unsigned Access) { CurrentAccess = Access; }
  bool isDefaultAccessPublic() const { return getTagKind() == TK_struct || getTagKind() == TK_union; }

  /// Check if this class is derived from the given base class.
  /// Performs a recursive search through the inheritance hierarchy.
  /// \param Base The potential base class to check.
  /// \return true if this class inherits from Base (directly or indirectly).
  bool isDerivedFrom(const CXXRecordDecl *Base) const;

  NodeKind getKind() const override { return NodeKind::CXXRecordDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXRecordDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// CXXMethodDecl - C++ member function declaration
//===----------------------------------------------------------------------===//

/// CXXMethodDecl - C++ member function declaration.
class CXXMethodDecl : public FunctionDecl {
public:
  enum RefQualifierKind {
    RQ_None,   // No ref-qualifier
    RQ_LValue, // & ref-qualifier
    RQ_RValue  // && ref-qualifier
  };

private:
  CXXRecordDecl *Parent;
  bool IsStatic;
  bool IsConst;
  bool IsVolatile;
  bool IsVirtual;
  bool IsPureVirtual;
  bool IsOverride;
  bool IsFinal;
  bool IsDefaulted;
  bool IsDeleted;
  bool IsStaticOperator = false;  // P7.1.3: static operator() / static operator[]
  RefQualifierKind RefQualifier;
  AccessSpecifier Access;

public:
  CXXMethodDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
                llvm::ArrayRef<ParmVarDecl *> Params, CXXRecordDecl *Parent,
                Stmt *Body = nullptr, bool IsStatic = false, bool IsConst = false,
                bool IsVolatile = false, bool IsVirtual = false, 
                bool IsPureVirtual = false, bool IsOverride = false, bool IsFinal = false,
                bool IsDefaulted = false, bool IsDeleted = false,
                RefQualifierKind RefQual = RQ_None,
                bool HasNoexceptSpec = false, bool NoexceptValue = false,
                Expr *NoexceptExpr = nullptr,
                AccessSpecifier Access = AccessSpecifier::AS_private)
      : FunctionDecl(Loc, Name, T, Params, Body, false, false,
                     HasNoexceptSpec, NoexceptValue, NoexceptExpr),
        Parent(Parent), IsStatic(IsStatic), IsConst(IsConst), 
        IsVolatile(IsVolatile), IsVirtual(IsVirtual), IsPureVirtual(IsPureVirtual),
        IsOverride(IsOverride), IsFinal(IsFinal), IsDefaulted(IsDefaulted),
        IsDeleted(IsDeleted), RefQualifier(RefQual), Access(Access) {}

  CXXRecordDecl *getParent() const { return Parent; }

  bool isStatic() const { return IsStatic; }
  bool isConst() const { return IsConst; }
  bool isVolatile() const { return IsVolatile; }
  bool isVirtual() const { return IsVirtual; }
  bool isPureVirtual() const { return IsPureVirtual; }
  bool isOverride() const { return IsOverride; }
  bool isFinal() const { return IsFinal; }
  bool isDefaulted() const { return IsDefaulted; }
  bool isDeleted() const { return IsDeleted; }
  RefQualifierKind getRefQualifier() const { return RefQualifier; }
  bool hasRefQualifier() const { return RefQualifier != RQ_None; }

  //===------------------------------------------------------------------===//
  // P7.1.3: Static operator (P1169R4, P2589R1)
  //===------------------------------------------------------------------===//

  /// Whether this is a static operator() or static operator[].
  bool isStaticOperator() const { return IsStaticOperator; }
  void setStaticOperator(bool V) { IsStaticOperator = V; }

  /// Whether this is specifically a static call operator().
  bool isStaticCallOperator() const;

  /// Whether this is specifically a static subscript operator[].
  bool isStaticSubscriptOperator() const;
  
  AccessSpecifier getAccess() const { return Access; }
  void setAccess(AccessSpecifier A) { Access = A; }
  
  bool isPublic() const { return Access == AccessSpecifier::AS_public; }
  bool isProtected() const { return Access == AccessSpecifier::AS_protected; }
  bool isPrivate() const { return Access == AccessSpecifier::AS_private; }

  NodeKind getKind() const override { return NodeKind::CXXMethodDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::CXXMethodDeclKind &&
           N->getKind() <= NodeKind::CXXConversionDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// CXXCtorInitializer - Member initializer in constructor
//===----------------------------------------------------------------------===//

/// CXXCtorInitializer - Represents a single member initializer in a constructor's
/// initializer list, e.g., `member(value)` in `: member(value), ...`
class CXXCtorInitializer {
  SourceLocation MemberLoc;
  llvm::StringRef MemberName;
  QualType BaseType; // For base initializers: the base class type
  llvm::SmallVector<Expr *, 4> Args;
  bool IsBaseInitializer;
  bool IsDelegatingInitializer;

public:
  CXXCtorInitializer(SourceLocation Loc, llvm::StringRef Name,
                     llvm::ArrayRef<Expr *> Arguments,
                     bool IsBase = false, bool IsDelegating = false,
                     QualType BaseTy = QualType())
      : MemberLoc(Loc), MemberName(Name), BaseType(BaseTy),
        Args(Arguments.begin(), Arguments.end()),
        IsBaseInitializer(IsBase), IsDelegatingInitializer(IsDelegating) {}

  SourceLocation getMemberLocation() const { return MemberLoc; }
  llvm::StringRef getMemberName() const { return MemberName; }
  QualType getBaseType() const { return BaseType; }
  void setBaseType(QualType T) { BaseType = T; }
  llvm::ArrayRef<Expr *> getArguments() const { return Args; }
  bool isBaseInitializer() const { return IsBaseInitializer; }
  bool isDelegatingInitializer() const { return IsDelegatingInitializer; }
  bool isMemberInitializer() const { return !IsBaseInitializer && !IsDelegatingInitializer; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const;
};

//===----------------------------------------------------------------------===//
// CXXConstructorDecl - C++ constructor declaration
//===----------------------------------------------------------------------===//

/// CXXConstructorDecl - C++ constructor declaration.
class CXXConstructorDecl : public CXXMethodDecl {
  llvm::SmallVector<CXXCtorInitializer *, 8> Initializers;
  bool IsExplicit;

public:
  CXXConstructorDecl(SourceLocation Loc, CXXRecordDecl *Parent,
                     llvm::ArrayRef<ParmVarDecl *> Params, Stmt *Body = nullptr,
                     bool IsExplicit = false)
      : CXXMethodDecl(Loc, Parent->getName(), QualType(), Params, Parent, Body),
        IsExplicit(IsExplicit) {}

  bool isExplicit() const { return IsExplicit; }

  // Member initializers
  llvm::ArrayRef<CXXCtorInitializer *> initializers() const { return Initializers; }
  void addInitializer(CXXCtorInitializer *Init) { Initializers.push_back(Init); }
  unsigned getNumInitializers() const { return Initializers.size(); }

  NodeKind getKind() const override { return NodeKind::CXXConstructorDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXConstructorDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// CXXDestructorDecl - C++ destructor declaration
//===----------------------------------------------------------------------===//

/// CXXDestructorDecl - C++ destructor declaration.
class CXXDestructorDecl : public CXXMethodDecl {
public:
  CXXDestructorDecl(SourceLocation Loc, CXXRecordDecl *Parent,
                    Stmt *Body = nullptr);

  NodeKind getKind() const override { return NodeKind::CXXDestructorDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXDestructorDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// CXXConversionDecl - C++ conversion function declaration
//===----------------------------------------------------------------------===//

/// CXXConversionDecl - C++ conversion function declaration.
class CXXConversionDecl : public CXXMethodDecl {
  QualType ConversionType;

public:
  CXXConversionDecl(SourceLocation Loc, QualType ConvType,
                    CXXRecordDecl *Parent, Stmt *Body = nullptr);

  QualType getConversionType() const { return ConversionType; }

  NodeKind getKind() const override { return NodeKind::CXXConversionDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXConversionDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// AccessSpecDecl - Access specifier declaration
//===----------------------------------------------------------------------===//

/// AccessSpecDecl - Access specifier declaration (public, protected, private).
class AccessSpecDecl : public Decl {
private:
  AccessSpecifier Access;
  SourceLocation ColonLoc;

public:
  AccessSpecDecl(SourceLocation Loc, AccessSpecifier Access,
                 SourceLocation ColonLoc)
      : Decl(Loc), Access(Access), ColonLoc(ColonLoc) {}

  AccessSpecifier getAccess() const { return Access; }
  SourceLocation getColonLoc() const { return ColonLoc; }

  NodeKind getKind() const override { return NodeKind::AccessSpecDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::AccessSpecDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// NamespaceDecl - Namespace declaration
//===----------------------------------------------------------------------===//

/// NamespaceDecl - Namespace declaration.
class NamespaceDecl : public NamedDecl, public DeclContext {
  bool IsInline;

public:
  NamespaceDecl(SourceLocation Loc, llvm::StringRef Name, bool IsInline = false)
      : NamedDecl(Loc, Name), DeclContext(DeclContextKind::Namespace),
        IsInline(IsInline) {}

  void addDecl(Decl *D) { DeclContext::addDecl(D); }
  void addDecl(NamedDecl *D) { DeclContext::addDecl(D); }

  llvm::ArrayRef<Decl *> decls() const { return DeclContext::decls(); }

  DeclContext *getDeclContext() { return this; }
  const DeclContext *getDeclContext() const { return this; }

  bool isInline() const { return IsInline; }

  NodeKind getKind() const override { return NodeKind::NamespaceDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::NamespaceDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// TranslationUnitDecl - Top-level translation unit
//===----------------------------------------------------------------------===//

/// TranslationUnitDecl - The top-level declaration for a translation unit.
class TranslationUnitDecl : public Decl, public DeclContext {
public:
  TranslationUnitDecl(SourceLocation Loc)
      : Decl(Loc), DeclContext(DeclContextKind::TranslationUnit) {}

  /// Add a declaration to the translation unit.
  void addDecl(Decl *D) { DeclContext::addDecl(D); }
  void addDecl(NamedDecl *D) { DeclContext::addDecl(D); }

  /// Get all declarations.
  llvm::ArrayRef<Decl *> decls() const { return DeclContext::decls(); }

  /// Access the DeclContext interface.
  DeclContext *getDeclContext() { return this; }
  const DeclContext *getDeclContext() const { return this; }

  NodeKind getKind() const override { return NodeKind::TranslationUnitDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TranslationUnitDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// UsingDecl - Using declaration
//===----------------------------------------------------------------------===//

/// UsingDecl - Using declaration (using T::x).
class UsingDecl : public NamedDecl {
  llvm::StringRef NestedNameSpecifier; // e.g., "A::B::" for using A::B::C;
  bool HasNestedNameSpecifier;
  bool IsInheritingConstructor; // true for "using Base::Base;"

public:
  UsingDecl(SourceLocation Loc, llvm::StringRef Name,
            llvm::StringRef NestedName = "", bool HasNested = false,
            bool IsInheritingCtor = false)
      : NamedDecl(Loc, Name), NestedNameSpecifier(NestedName),
        HasNestedNameSpecifier(HasNested),
        IsInheritingConstructor(IsInheritingCtor) {}

  llvm::StringRef getNestedNameSpecifier() const { return NestedNameSpecifier; }
  bool hasNestedNameSpecifier() const { return HasNestedNameSpecifier; }
  bool isInheritingConstructor() const { return IsInheritingConstructor; }

  NodeKind getKind() const override { return NodeKind::UsingDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::UsingDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// UsingDirectiveDecl - Using directive declaration
//===----------------------------------------------------------------------===//

/// UsingDirectiveDecl - Using directive declaration (using namespace T).
class UsingDirectiveDecl : public NamedDecl {
  llvm::StringRef NestedNameSpecifier; // e.g., "A::B::" for using namespace A::B::C;
  bool HasNestedNameSpecifier;

public:
  UsingDirectiveDecl(SourceLocation Loc, llvm::StringRef Name,
                     llvm::StringRef NestedName = "", bool HasNested = false)
      : NamedDecl(Loc, Name), NestedNameSpecifier(NestedName),
        HasNestedNameSpecifier(HasNested) {}

  llvm::StringRef getNestedNameSpecifier() const { return NestedNameSpecifier; }
  bool hasNestedNameSpecifier() const { return HasNestedNameSpecifier; }

  NodeKind getKind() const override { return NodeKind::UsingDirectiveDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::UsingDirectiveDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// NamespaceAliasDecl - Namespace alias declaration
//===----------------------------------------------------------------------===//

/// NamespaceAliasDecl - Namespace alias declaration.
/// Example: namespace AB = A::B;
class NamespaceAliasDecl : public NamedDecl {
  llvm::StringRef AliasedName; // The full qualified name being aliased
  llvm::StringRef NestedNameSpecifier;

public:
  NamespaceAliasDecl(SourceLocation Loc, llvm::StringRef AliasName,
                     llvm::StringRef AliasedName,
                     llvm::StringRef NestedName = "")
      : NamedDecl(Loc, AliasName), AliasedName(AliasedName),
        NestedNameSpecifier(NestedName) {}

  llvm::StringRef getAliasedName() const { return AliasedName; }
  llvm::StringRef getNestedNameSpecifier() const { return NestedNameSpecifier; }

  NodeKind getKind() const override { return NodeKind::NamespaceAliasDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::NamespaceAliasDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// UsingEnumDecl - Using enum declaration (C++20)
//===----------------------------------------------------------------------===//

/// UsingEnumDecl - Using enum declaration (C++20).
/// Example: using enum Color;
class UsingEnumDecl : public Decl {
  llvm::StringRef EnumName;
  llvm::StringRef NestedNameSpecifier;
  bool HasNestedNameSpecifier;

public:
  UsingEnumDecl(SourceLocation Loc, llvm::StringRef EnumName,
                llvm::StringRef NestedName = "", bool HasNested = false)
      : Decl(Loc), EnumName(EnumName), NestedNameSpecifier(NestedName),
        HasNestedNameSpecifier(HasNested) {}

  llvm::StringRef getEnumName() const { return EnumName; }
  llvm::StringRef getNestedNameSpecifier() const { return NestedNameSpecifier; }
  bool hasNestedNameSpecifier() const { return HasNestedNameSpecifier; }

  NodeKind getKind() const override { return NodeKind::UsingEnumDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::UsingEnumDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// LabelDecl - Label declaration
//===----------------------------------------------------------------------===//

/// LabelDecl - Label declaration.
class LabelDecl : public NamedDecl {
public:
  LabelDecl(SourceLocation Loc, llvm::StringRef Name)
      : NamedDecl(Loc, Name) {}

  NodeKind getKind() const override { return NodeKind::LabelDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::LabelDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// TemplateDecl - Base class for template declarations
//===----------------------------------------------------------------------===//

/// TemplateDecl - Base class for template declarations.
class TemplateDecl : public NamedDecl {
protected:
  TemplateParameterList *Params = nullptr;
  Decl *TemplatedDecl;

public:
  TemplateDecl(SourceLocation Loc, llvm::StringRef Name, Decl *TemplatedDecl)
      : NamedDecl(Loc, Name), TemplatedDecl(TemplatedDecl) {}

  /// Get the template parameter list.
  TemplateParameterList *getTemplateParameterList() const { return Params; }

  /// Set the template parameter list.
  void setTemplateParameterList(TemplateParameterList *TPL) { Params = TPL; }

  /// Convenience: get template parameters as ArrayRef.
  llvm::ArrayRef<NamedDecl *> getTemplateParameters() const {
    return Params ? Params->getParams() : llvm::ArrayRef<NamedDecl *>();
  }

  /// Legacy: add a single template parameter (creates list if needed).
  void addTemplateParameter(NamedDecl *Param);

  Decl *getTemplatedDecl() const { return TemplatedDecl; }

  Expr *getRequiresClause() const {
    return Params ? Params->getRequiresClause() : nullptr;
  }
  void setRequiresClause(Expr *E);
  bool hasRequiresClause() const { return getRequiresClause() != nullptr; }

  NodeKind getKind() const override { return NodeKind::TemplateDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TemplateDeclKind ||
           N->getKind() == NodeKind::TemplateTemplateParmDeclKind ||
           N->getKind() == NodeKind::FunctionTemplateDeclKind ||
           N->getKind() == NodeKind::ClassTemplateDeclKind ||
           N->getKind() == NodeKind::VarTemplateDeclKind ||
           N->getKind() == NodeKind::TypeAliasTemplateDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// TemplateTypeParmDecl - Template type parameter declaration
//===----------------------------------------------------------------------===//

/// TemplateTypeParmDecl - Template type parameter declaration.
/// Example: template<typename T> or template<class T>
class TemplateTypeParmDecl : public TypeDecl {
  unsigned Depth;
  unsigned Index;
  bool IsParameterPack;
  bool IsTypename; // true for 'typename', false for 'class'
  QualType DefaultArgument;

public:
  TemplateTypeParmDecl(SourceLocation Loc, llvm::StringRef Name, unsigned Depth,
                       unsigned Index, bool IsParameterPack, bool IsTypename)
      : TypeDecl(Loc, Name), Depth(Depth), Index(Index),
        IsParameterPack(IsParameterPack), IsTypename(IsTypename) {}

  unsigned getDepth() const { return Depth; }
  unsigned getIndex() const { return Index; }
  bool isParameterPack() const { return IsParameterPack; }
  bool isTypename() const { return IsTypename; }

  QualType getDefaultArgument() const { return DefaultArgument; }
  void setDefaultArgument(QualType T) { DefaultArgument = T; }

  NodeKind getKind() const override { return NodeKind::TemplateTypeParmDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TemplateTypeParmDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// NonTypeTemplateParmDecl - Non-type template parameter declaration
//===----------------------------------------------------------------------===//

/// NonTypeTemplateParmDecl - Non-type template parameter declaration.
/// Example: template<int N> or template<auto X>
class NonTypeTemplateParmDecl : public ValueDecl {
  unsigned Depth;
  unsigned Index;
  bool IsParameterPack;
  Expr *DefaultArg;

public:
  NonTypeTemplateParmDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
                          unsigned Depth, unsigned Index, bool IsParameterPack)
      : ValueDecl(Loc, Name, T), Depth(Depth), Index(Index),
        IsParameterPack(IsParameterPack), DefaultArg(nullptr) {}

  unsigned getDepth() const { return Depth; }
  unsigned getIndex() const { return Index; }
  bool isParameterPack() const { return IsParameterPack; }

  Expr *getDefaultArgument() const { return DefaultArg; }
  void setDefaultArgument(Expr *Arg) { DefaultArg = Arg; }
  bool hasDefaultArgument() const { return DefaultArg != nullptr; }

  NodeKind getKind() const override { return NodeKind::NonTypeTemplateParmDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::NonTypeTemplateParmDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// TemplateTemplateParmDecl - Template template parameter declaration
//===----------------------------------------------------------------------===//

/// TemplateTemplateParmDecl - Template template parameter declaration.
/// Example: template<template<typename> class T>
class TemplateTemplateParmDecl : public TemplateDecl {
  unsigned Depth;
  unsigned Index;
  bool IsParameterPack;
  TemplateDecl *DefaultArg;
  Expr *Constraint = nullptr; // C++20 requires-clause constraint

public:
  TemplateTemplateParmDecl(SourceLocation Loc, llvm::StringRef Name,
                           unsigned Depth, unsigned Index, bool IsParameterPack)
      : TemplateDecl(Loc, Name, nullptr), Depth(Depth), Index(Index),
        IsParameterPack(IsParameterPack), DefaultArg(nullptr) {}

  unsigned getDepth() const { return Depth; }
  unsigned getIndex() const { return Index; }
  bool isParameterPack() const { return IsParameterPack; }

  TemplateDecl *getDefaultArgument() const { return DefaultArg; }
  void setDefaultArgument(TemplateDecl *Arg) { DefaultArg = Arg; }
  bool hasDefaultArgument() const { return DefaultArg != nullptr; }

  Expr *getConstraint() const { return Constraint; }
  void setConstraint(Expr *C) { Constraint = C; }
  bool hasConstraint() const { return Constraint != nullptr; }

  NodeKind getKind() const override { return NodeKind::TemplateTemplateParmDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TemplateTemplateParmDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// ModuleDecl - Module declaration (C++20)
//===----------------------------------------------------------------------===//

/// ModuleDecl - Module declaration.
/// Example: export module mylib; or module mylib:detail;
/// Also supports global module fragment: module;
/// And private module fragment: module :private;
class ModuleDecl : public NamedDecl {
  llvm::StringRef ModuleName;
  llvm::StringRef FullModuleName; // Full dotted module name (e.g., "std.core")
  llvm::StringRef PartitionName;
  llvm::SmallVector<llvm::StringRef, 4> Attributes; // Module attributes
  bool IsExported;
  bool IsModulePartition;
  bool IsGlobalModuleFragment;  // module; (global module fragment)
  bool IsPrivateModuleFragment; // module :private; (private module fragment)

public:
  ModuleDecl(SourceLocation Loc, llvm::StringRef ModuleName,
             bool IsExported = false, llvm::StringRef PartitionName = "",
             bool IsModulePartition = false,
             bool IsGlobalModuleFragment = false,
             bool IsPrivateModuleFragment = false)
      : NamedDecl(Loc, ModuleName), ModuleName(ModuleName),
        FullModuleName(ModuleName), PartitionName(PartitionName),
        IsExported(IsExported), IsModulePartition(IsModulePartition),
        IsGlobalModuleFragment(IsGlobalModuleFragment),
        IsPrivateModuleFragment(IsPrivateModuleFragment) {}

  llvm::StringRef getModuleName() const { return ModuleName; }
  llvm::StringRef getFullModuleName() const { return FullModuleName; }
  llvm::StringRef getPartitionName() const { return PartitionName; }
  llvm::ArrayRef<llvm::StringRef> getAttributes() const { return Attributes; }
  bool isExported() const { return IsExported; }
  bool isModulePartition() const { return IsModulePartition; }
  bool isGlobalModuleFragment() const { return IsGlobalModuleFragment; }
  bool isPrivateModuleFragment() const { return IsPrivateModuleFragment; }

  void setFullModuleName(llvm::StringRef Name) { FullModuleName = Name; }
  void addAttribute(llvm::StringRef Attr) { Attributes.push_back(Attr); }

  NodeKind getKind() const override { return NodeKind::ModuleDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ModuleDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// ImportDecl - Module import declaration (C++20)
//===----------------------------------------------------------------------===//

/// ImportDecl - Module import declaration.
/// Example: import std.core; or export import :submodule;
class ImportDecl : public NamedDecl {
  llvm::StringRef ModuleName;
  llvm::StringRef PartitionName;
  llvm::StringRef HeaderName; // Header name for header imports (e.g., "header.h")
  bool IsExported;
  bool IsHeaderImport; // True if importing a header (import "header.h")

public:
  ImportDecl(SourceLocation Loc, llvm::StringRef ModuleName,
             bool IsExported = false, llvm::StringRef PartitionName = "",
             llvm::StringRef HeaderName = "", bool IsHeaderImport = false)
      : NamedDecl(Loc, ModuleName), ModuleName(ModuleName),
        PartitionName(PartitionName), HeaderName(HeaderName),
        IsExported(IsExported), IsHeaderImport(IsHeaderImport) {}

  llvm::StringRef getModuleName() const { return ModuleName; }
  llvm::StringRef getPartitionName() const { return PartitionName; }
  llvm::StringRef getHeaderName() const { return HeaderName; }
  bool isExported() const { return IsExported; }
  bool isHeaderImport() const { return IsHeaderImport; }

  void setHeaderName(llvm::StringRef Name) { HeaderName = Name; }

  NodeKind getKind() const override { return NodeKind::ImportDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ImportDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// ExportDecl - Export declaration (C++20)
//===----------------------------------------------------------------------===//

/// ExportDecl - Export declaration.
/// Example: export int x; or export template<typename T> class Vector {};
class ExportDecl : public Decl {
  Decl *ExportedDecl;

public:
  ExportDecl(SourceLocation Loc, Decl *ExportedDecl)
      : Decl(Loc), ExportedDecl(ExportedDecl) {}

  Decl *getExportedDecl() const { return ExportedDecl; }

  NodeKind getKind() const override { return NodeKind::ExportDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ExportDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// StaticAssertDecl - Static assertion declaration
//===----------------------------------------------------------------------===//

/// StaticAssertDecl - Static assertion declaration.
/// Example: static_assert(sizeof(int) == 4, "int must be 4 bytes");
class StaticAssertDecl : public Decl {
  Expr *AssertExpr;
  llvm::StringRef Message;
  bool IsFailed;

public:
  StaticAssertDecl(SourceLocation Loc, Expr *E, llvm::StringRef Msg)
      : Decl(Loc), AssertExpr(E), Message(Msg), IsFailed(false) {}

  Expr *getAssertExpr() const { return AssertExpr; }
  llvm::StringRef getMessage() const { return Message; }
  bool isFailed() const { return IsFailed; }
  void setFailed(bool Failed) { IsFailed = Failed; }

  NodeKind getKind() const override { return NodeKind::StaticAssertDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::StaticAssertDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// LinkageSpecDecl - Linkage specification declaration
//===----------------------------------------------------------------------===//

/// LinkageSpecDecl - Linkage specification declaration.
/// Example: extern "C" { void foo(); } or extern "C++" { ... }
class LinkageSpecDecl : public Decl, public DeclContext {
public:
  enum Language { C, CXX };

private:
  Language Lang;
  bool HasBraces;

public:
  LinkageSpecDecl(SourceLocation Loc, Language L, bool HasBraces = true)
      : Decl(Loc), DeclContext(DeclContextKind::LinkageSpec), Lang(L),
        HasBraces(HasBraces) {}

  Language getLanguage() const { return Lang; }
  bool hasBraces() const { return HasBraces; }

  void addDecl(Decl *D) { DeclContext::addDecl(D); }
  void addDecl(NamedDecl *D) { DeclContext::addDecl(D); }
  llvm::ArrayRef<Decl *> decls() const { return DeclContext::decls(); }

  DeclContext *getDeclContext() { return this; }
  const DeclContext *getDeclContext() const { return this; }

  NodeKind getKind() const override { return NodeKind::LinkageSpecDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::LinkageSpecDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// TypeAliasDecl - Type alias declaration (C++11)
//===----------------------------------------------------------------------===//

/// TypeAliasDecl - Type alias declaration (C++11 using alias).
/// Example: using IntPtr = int*; or template<typename T> using Vec = std::vector<T>;
class TypeAliasDecl : public TypedefNameDecl {
  TemplateDecl *Template; // For alias templates

public:
  TypeAliasDecl(SourceLocation Loc, llvm::StringRef Name, QualType Underlying,
                TemplateDecl *TD = nullptr)
      : TypedefNameDecl(Loc, Name, Underlying), Template(TD) {}

  TemplateDecl *getTemplate() const { return Template; }

  NodeKind getKind() const override { return NodeKind::TypeAliasDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TypeAliasDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// FriendDecl - Friend declaration
//===----------------------------------------------------------------------===//

/// FriendDecl - Friend declaration.
/// Example: friend class Foo; or friend void bar(int);
class FriendDecl : public Decl {
  NamedDecl *FriendDecl_; // The friend declaration (function or class)
  QualType FriendType;    // If friend is a type (friend class X;)
  bool IsFriendType;      // true if friend is a type

public:
  FriendDecl(SourceLocation Loc, NamedDecl *FD, QualType FT = QualType(),
             bool IsType = false)
      : Decl(Loc), FriendDecl_(FD), FriendType(FT), IsFriendType(IsType) {}

  NamedDecl *getFriendDecl() const { return FriendDecl_; }
  QualType getFriendType() const { return FriendType; }
  bool isFriendType() const { return IsFriendType; }

  NodeKind getKind() const override { return NodeKind::FriendDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::FriendDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// ConceptDecl - Concept declaration (C++20)
//===----------------------------------------------------------------------===//

/// ConceptDecl - Concept declaration (C++20).
/// Example: template<typename T> concept Integral = requires { ... };
class ConceptDecl : public TypeDecl {
  Expr *ConstraintExpr; // The constraint expression
  TemplateDecl *Template;

public:
  ConceptDecl(SourceLocation Loc, llvm::StringRef Name, Expr *Constraint,
              TemplateDecl *TD = nullptr)
      : TypeDecl(Loc, Name), ConstraintExpr(Constraint), Template(TD) {}

  Expr *getConstraintExpr() const { return ConstraintExpr; }
  TemplateDecl *getTemplate() const { return Template; }

  NodeKind getKind() const override { return NodeKind::ConceptDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ConceptDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// AsmDecl - Asm declaration (C++98)
//===----------------------------------------------------------------------===//

/// AsmDecl - Asm declaration (inline assembly).
/// Example: asm("nop");
class AsmDecl : public Decl {
  llvm::StringRef AsmString; // The assembly string

public:
  AsmDecl(SourceLocation Loc, llvm::StringRef Asm)
      : Decl(Loc), AsmString(Asm) {}

  llvm::StringRef getAsmString() const { return AsmString; }

  NodeKind getKind() const override { return NodeKind::AsmDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::AsmDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// CXXDeductionGuideDecl - Deduction guide (C++17)
//===----------------------------------------------------------------------===//

/// CXXDeductionGuideDecl - Deduction guide declaration (C++17).
/// Example: template<typename T> Vector(T) -> Vector<T>;
class CXXDeductionGuideDecl : public FunctionDecl {
  bool IsExplicit; // Whether this is an explicit deduction guide

public:
  CXXDeductionGuideDecl(SourceLocation Loc, llvm::StringRef Name,
                        QualType Type, llvm::ArrayRef<ParmVarDecl *> Params,
                        bool IsExplicit = false)
      : FunctionDecl(Loc, Name, Type, Params), IsExplicit(IsExplicit) {}

  bool isExplicit() const { return IsExplicit; }

  NodeKind getKind() const override {
    return NodeKind::CXXDeductionGuideDeclKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::CXXDeductionGuideDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// AttributeDecl - Attribute specifier (C++11)
//===----------------------------------------------------------------------===//

/// AttributeDecl - Attribute specifier declaration.
/// Example: [[nodiscard]], [[deprecated("reason")]]
class AttributeDecl : public Decl {
  llvm::StringRef AttributeNamespace; // Optional namespace (e.g., "gnu" in [[gnu::unused]])
  llvm::StringRef AttributeName;       // The attribute name
  Expr *ArgumentExpr = nullptr;         // Optional argument expression

public:
  AttributeDecl(SourceLocation Loc, llvm::StringRef Namespace,
                llvm::StringRef Name, Expr *Arg = nullptr)
      : Decl(Loc), AttributeNamespace(Namespace), AttributeName(Name),
        ArgumentExpr(Arg) {}

  // Convenience constructor for attributes without namespace
  AttributeDecl(SourceLocation Loc, llvm::StringRef Name, Expr *Arg = nullptr)
      : Decl(Loc), AttributeName(Name), ArgumentExpr(Arg) {}

  bool hasNamespace() const { return !AttributeNamespace.empty(); }
  llvm::StringRef getNamespace() const { return AttributeNamespace; }
  llvm::StringRef getAttributeName() const { return AttributeName; }
  Expr *getArgumentExpr() const { return ArgumentExpr; }
  bool hasArgument() const { return ArgumentExpr != nullptr; }

  /// Get the full attribute name including namespace (e.g., "gnu::unused")
  std::string getFullName() const {
    if (hasNamespace()) {
      return (AttributeNamespace + "::" + AttributeName).str();
    }
    return AttributeName.str();
  }

  NodeKind getKind() const override { return NodeKind::AttributeDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::AttributeDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// AttributeListDecl - List of attribute specifiers (C++11)
//===----------------------------------------------------------------------===//

// Forward declaration for ContractAttr (defined in Attr.h)
class ContractAttr;

/// AttributeListDecl - List of attribute specifiers.
/// Example: [[nodiscard, deprecated("reason")]]
class AttributeListDecl : public Decl {
  llvm::SmallVector<AttributeDecl *, 4> Attrs;
  /// P7.3.1: Contract attributes stored with full semantic info.
  llvm::SmallVector<ContractAttr *, 2> Contracts;

public:
  AttributeListDecl(SourceLocation Loc) : Decl(Loc) {}

  void addAttribute(AttributeDecl *Attr) { Attrs.push_back(Attr); }

  /// Add a contract attribute with full semantic info.
  void addContract(ContractAttr *CA) { Contracts.push_back(CA); }

  llvm::ArrayRef<AttributeDecl *> getAttributes() const { return Attrs; }

  /// Get contract attributes for CodeGen.
  llvm::ArrayRef<ContractAttr *> getContracts() const { return Contracts; }

  bool hasContracts() const { return !Contracts.empty(); }

  size_t size() const { return Attrs.size(); }

  bool empty() const { return Attrs.empty() && Contracts.empty(); }

  NodeKind getKind() const override { return NodeKind::AttributeListDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::AttributeListDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// Template Specialization Declarations
//===----------------------------------------------------------------------===//

/// FunctionTemplateDecl - Represents a function template.
/// Example: template<typename T> void f(T x);
class FunctionTemplateDecl : public TemplateDecl {
public:
  FunctionTemplateDecl(SourceLocation Loc, llvm::StringRef Name, Decl *TemplatedDecl)
      : TemplateDecl(Loc, Name, TemplatedDecl) {}

  NodeKind getKind() const override { return NodeKind::FunctionTemplateDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::FunctionTemplateDeclKind;
  }
};

/// ClassTemplateDecl - Represents a class template.
/// Example: template<typename T> class Vector { ... };
class ClassTemplateDecl : public TemplateDecl {
  llvm::SmallVector<ClassTemplateSpecializationDecl *, 4> Specializations;

public:
  ClassTemplateDecl(SourceLocation Loc, llvm::StringRef Name, Decl *TemplatedDecl)
      : TemplateDecl(Loc, Name, TemplatedDecl) {}

  void addSpecialization(ClassTemplateSpecializationDecl *Spec) {
    Specializations.push_back(Spec);
  }

  llvm::ArrayRef<ClassTemplateSpecializationDecl *> getSpecializations() const {
    return Specializations;
  }

  /// Find an existing specialization that exactly matches the given arguments.
  /// Defined out-of-line in Decl.cpp.
  ClassTemplateSpecializationDecl *
  findSpecialization(llvm::ArrayRef<TemplateArgument> Args) const;

  /// Get all partial specializations.
  /// Defined out-of-line in Decl.cpp.
  llvm::SmallVector<ClassTemplatePartialSpecializationDecl *, 4>
  getPartialSpecializations() const;

  NodeKind getKind() const override { return NodeKind::ClassTemplateDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ClassTemplateDeclKind;
  }
};

/// VarTemplateDecl - Represents a variable template.
/// Example: template<typename T> constexpr T pi = T(3.14159);
class VarTemplateDecl : public TemplateDecl {
  llvm::SmallVector<VarTemplateSpecializationDecl *, 4> Specializations;

public:
  VarTemplateDecl(SourceLocation Loc, llvm::StringRef Name, Decl *TemplatedDecl)
      : TemplateDecl(Loc, Name, TemplatedDecl) {}

  void addSpecialization(VarTemplateSpecializationDecl *Spec) {
    Specializations.push_back(Spec);
  }

  llvm::ArrayRef<VarTemplateSpecializationDecl *> getSpecializations() const {
    return Specializations;
  }

  NodeKind getKind() const override { return NodeKind::VarTemplateDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::VarTemplateDeclKind;
  }
};

/// TypeAliasTemplateDecl - Represents an alias template.
/// Example: template<typename T> using Vec = Vector<T>;
class TypeAliasTemplateDecl : public TemplateDecl {
public:
  TypeAliasTemplateDecl(SourceLocation Loc, llvm::StringRef Name, Decl *TemplatedDecl)
      : TemplateDecl(Loc, Name, TemplatedDecl) {}

  NodeKind getKind() const override { return NodeKind::TypeAliasTemplateDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TypeAliasTemplateDeclKind;
  }
};

/// ClassTemplateSpecializationDecl - Represents a class template specialization.
/// Example: template<> class Vector<int> { ... };
class ClassTemplateSpecializationDecl : public CXXRecordDecl {
  ClassTemplateDecl *SpecializedTemplate;
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
  bool IsExplicitSpecialization;

public:
  ClassTemplateSpecializationDecl(SourceLocation Loc, llvm::StringRef Name,
                                   ClassTemplateDecl *Template,
                                   llvm::ArrayRef<TemplateArgument> Args,
                                   bool ExplicitSpec = false)
      : CXXRecordDecl(Loc, Name), SpecializedTemplate(Template),
        TemplateArgs(Args.begin(), Args.end()),
        IsExplicitSpecialization(ExplicitSpec) {}

  ClassTemplateDecl *getSpecializedTemplate() const { return SpecializedTemplate; }
  
  llvm::ArrayRef<TemplateArgument> getTemplateArgs() const { return TemplateArgs; }
  
  unsigned getNumTemplateArgs() const { return TemplateArgs.size(); }
  
  const TemplateArgument &getTemplateArg(unsigned Idx) const {
    return TemplateArgs[Idx];
  }

  bool isExplicitSpecialization() const { return IsExplicitSpecialization; }

  NodeKind getKind() const override {
    return NodeKind::ClassTemplateSpecializationDeclKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ClassTemplateSpecializationDeclKind;
  }
};

/// ClassTemplatePartialSpecializationDecl - Represents a class template partial specialization.
/// Example: template<typename T> class Vector<T*> { ... };
class ClassTemplatePartialSpecializationDecl : public ClassTemplateSpecializationDecl {
  TemplateParameterList *Params = nullptr;

public:
  ClassTemplatePartialSpecializationDecl(SourceLocation Loc, llvm::StringRef Name,
                                          ClassTemplateDecl *Template,
                                          llvm::ArrayRef<TemplateArgument> Args)
      : ClassTemplateSpecializationDecl(Loc, Name, Template, Args, false) {}

  void setTemplateParameterList(TemplateParameterList *TPL) { Params = TPL; }
  TemplateParameterList *getTemplateParameterList() const { return Params; }

  llvm::ArrayRef<NamedDecl *> getTemplateParameters() const {
    return Params ? Params->getParams() : llvm::ArrayRef<NamedDecl *>();
  }

  NodeKind getKind() const override {
    return NodeKind::ClassTemplatePartialSpecializationDeclKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ClassTemplatePartialSpecializationDeclKind;
  }
};

/// VarTemplateSpecializationDecl - Represents a variable template specialization.
/// Example: template<> constexpr int pi<int> = 3;
class VarTemplateSpecializationDecl : public VarDecl {
  VarTemplateDecl *SpecializedTemplate;
  llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
  bool IsExplicitSpecialization;

public:
  VarTemplateSpecializationDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
                                 VarTemplateDecl *Template,
                                 llvm::ArrayRef<TemplateArgument> Args,
                                 Expr *Init = nullptr,
                                 bool ExplicitSpec = false)
      : VarDecl(Loc, Name, T, Init), SpecializedTemplate(Template),
        TemplateArgs(Args.begin(), Args.end()),
        IsExplicitSpecialization(ExplicitSpec) {}

  VarTemplateDecl *getSpecializedTemplate() const { return SpecializedTemplate; }
  
  llvm::ArrayRef<TemplateArgument> getTemplateArgs() const { return TemplateArgs; }
  
  unsigned getNumTemplateArgs() const { return TemplateArgs.size(); }
  
  const TemplateArgument &getTemplateArg(unsigned Idx) const {
    return TemplateArgs[Idx];
  }

  bool isExplicitSpecialization() const { return IsExplicitSpecialization; }

  NodeKind getKind() const override {
    return NodeKind::VarTemplateSpecializationDeclKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::VarTemplateSpecializationDeclKind;
  }
};

/// VarTemplatePartialSpecializationDecl - Represents a variable template partial specialization.
class VarTemplatePartialSpecializationDecl : public VarTemplateSpecializationDecl {
  TemplateParameterList *Params = nullptr;

public:
  VarTemplatePartialSpecializationDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
                                        VarTemplateDecl *Template,
                                        llvm::ArrayRef<TemplateArgument> Args,
                                        Expr *Init = nullptr)
      : VarTemplateSpecializationDecl(Loc, Name, T, Template, Args, Init, false) {}

  void setTemplateParameterList(TemplateParameterList *TPL) { Params = TPL; }
  TemplateParameterList *getTemplateParameterList() const { return Params; }

  llvm::ArrayRef<NamedDecl *> getTemplateParameters() const {
    return Params ? Params->getParams() : llvm::ArrayRef<NamedDecl *>();
  }

  NodeKind getKind() const override {
    return NodeKind::VarTemplatePartialSpecializationDeclKind;
  }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::VarTemplatePartialSpecializationDeclKind;
  }
};

} // namespace blocktype
