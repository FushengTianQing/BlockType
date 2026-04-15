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
#include "blocktype/AST/Type.h"
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

//===----------------------------------------------------------------------===//
// Decl - Base class for all declarations
//===----------------------------------------------------------------------===//

/// Decl - Base class for all declaration nodes.
class Decl : public ASTNode {
protected:
  Decl(SourceLocation Loc) : ASTNode(Loc) {}

public:
  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::NamedDeclKind &&
           N->getKind() < NodeKind::NumNodeKinds;
  }
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

public:
  VarDecl(SourceLocation Loc, llvm::StringRef Name, QualType T, 
          Expr *Init = nullptr, bool IsStatic = false)
      : ValueDecl(Loc, Name, T), Init(Init), IsStatic(IsStatic) {}

  Expr *getInit() const { return Init; }
  void setInit(Expr *I) { Init = I; }
  bool isStatic() const { return IsStatic; }
  void setStatic(bool S) { IsStatic = S; }

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
  bool HasNoexceptSpec;
  bool NoexceptValue; // true if noexcept(true), false if noexcept(false)
  Expr *NoexceptExpr; // noexcept(expression)

public:
  FunctionDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
               llvm::ArrayRef<ParmVarDecl *> Params, Stmt *Body = nullptr,
               bool IsInline = false, bool IsConstexpr = false,
               bool HasNoexceptSpec = false, bool NoexceptValue = false,
               Expr *NoexceptExpr = nullptr)
      : ValueDecl(Loc, Name, T), Params(Params.begin(), Params.end()),
        Body(Body), IsInline(IsInline), IsConstexpr(IsConstexpr),
        HasNoexceptSpec(HasNoexceptSpec), NoexceptValue(NoexceptValue),
        NoexceptExpr(NoexceptExpr) {}

  llvm::ArrayRef<ParmVarDecl *> getParams() const { return Params; }
  unsigned getNumParams() const { return Params.size(); }
  ParmVarDecl *getParamDecl(unsigned i) const { return Params[i]; }

  Stmt *getBody() const { return Body; }
  void setBody(Stmt *B) { Body = B; }

  bool isInline() const { return IsInline; }
  bool isConstexpr() const { return IsConstexpr; }
  bool hasNoexceptSpec() const { return HasNoexceptSpec; }
  bool getNoexceptValue() const { return NoexceptValue; }
  Expr *getNoexceptExpr() const { return NoexceptExpr; }

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

public:
  ParmVarDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
              unsigned Index, Expr *DefaultArg = nullptr)
      : VarDecl(Loc, Name, T, DefaultArg), Index(Index) {}

  unsigned getFunctionScopeIndex() const { return Index; }
  Expr *getDefaultArg() const { return getInit(); }

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

public:
  FieldDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
            Expr *BitWidth = nullptr, bool IsMutable = false,
            Expr *InClassInit = nullptr)
      : ValueDecl(Loc, Name, T), BitWidth(BitWidth),
        InClassInitializer(InClassInit), IsMutable(IsMutable) {}

  Expr *getBitWidth() const { return BitWidth; }
  bool isMutable() const { return IsMutable; }
  Expr *getInClassInitializer() const { return InClassInitializer; }
  bool hasInClassInitializer() const { return InClassInitializer != nullptr; }

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
  Expr *InitVal;

public:
  EnumConstantDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
                   Expr *InitVal = nullptr)
      : ValueDecl(Loc, Name, T), InitVal(InitVal) {}

  Expr *getInitVal() const { return InitVal; }

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

protected:
  TagDecl(SourceLocation Loc, llvm::StringRef Name, TagKind TK)
      : TypeDecl(Loc, Name), TagKindValue(TK) {}

public:
  TagKind getTagKind() const { return TagKindValue; }
  bool isStruct() const { return TagKindValue == TK_struct; }
  bool isClass() const { return TagKindValue == TK_class; }
  bool isUnion() const { return TagKindValue == TK_union; }
  bool isEnum() const { return TagKindValue == TK_enum; }

  static bool classof(const ASTNode *N) {
    return N->getKind() >= NodeKind::TagDeclKind &&
           N->getKind() < NodeKind::NamespaceDeclKind;
  }
};

//===----------------------------------------------------------------------===//
// EnumDecl - Enum declaration
//===----------------------------------------------------------------------===//

/// EnumDecl - Enum declaration.
class EnumDecl : public TagDecl {
  llvm::SmallVector<EnumConstantDecl *, 8> Enumerators;

public:
  EnumDecl(SourceLocation Loc, llvm::StringRef Name)
      : TagDecl(Loc, Name, TK_enum) {}

  llvm::ArrayRef<EnumConstantDecl *> enumerators() const { return Enumerators; }
  void addEnumerator(EnumConstantDecl *D) { Enumerators.push_back(D); }

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
class CXXRecordDecl : public RecordDecl {
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
      : RecordDecl(Loc, Name, TK), HasDefaultConstructor(false),
        HasCopyConstructor(false), HasMoveConstructor(false),
        HasDestructor(false), CurrentAccess(TK == TK_class ? 0 : 2) {} // class默认private, struct/union默认public

  // Base classes
  llvm::ArrayRef<BaseSpecifier> bases() const { return Bases; }
  void addBase(const BaseSpecifier &Base) { Bases.push_back(Base); }
  unsigned getNumBases() const { return Bases.size(); }

  // Methods
  llvm::ArrayRef<CXXMethodDecl *> methods() const { return Methods; }
  void addMethod(CXXMethodDecl *M) { Methods.push_back(M); }

  // Members
  llvm::ArrayRef<Decl *> members() const { return Members; }
  void addMember(Decl *D) { Members.push_back(D); }

  // Special members
  bool hasDefaultConstructor() const { return HasDefaultConstructor; }
  bool hasCopyConstructor() const { return HasCopyConstructor; }
  bool hasMoveConstructor() const { return HasMoveConstructor; }
  bool hasDestructor() const { return HasDestructor; }

  // Access control
  unsigned getCurrentAccess() const { return CurrentAccess; }
  void setCurrentAccess(unsigned Access) { CurrentAccess = Access; }
  bool isDefaultAccessPublic() const { return getTagKind() == TK_struct || getTagKind() == TK_union; }

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
  RefQualifierKind RefQualifier;

public:
  CXXMethodDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
                llvm::ArrayRef<ParmVarDecl *> Params, CXXRecordDecl *Parent,
                Stmt *Body = nullptr, bool IsStatic = false, bool IsConst = false,
                bool IsVolatile = false, bool IsVirtual = false, 
                bool IsPureVirtual = false, bool IsOverride = false, bool IsFinal = false,
                bool IsDefaulted = false, bool IsDeleted = false,
                RefQualifierKind RefQual = RQ_None,
                bool HasNoexceptSpec = false, bool NoexceptValue = false,
                Expr *NoexceptExpr = nullptr)
      : FunctionDecl(Loc, Name, T, Params, Body, false, false,
                     HasNoexceptSpec, NoexceptValue, NoexceptExpr),
        Parent(Parent), IsStatic(IsStatic), IsConst(IsConst), 
        IsVolatile(IsVolatile), IsVirtual(IsVirtual), IsPureVirtual(IsPureVirtual),
        IsOverride(IsOverride), IsFinal(IsFinal), IsDefaulted(IsDefaulted),
        IsDeleted(IsDeleted), RefQualifier(RefQual) {}

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
  llvm::SmallVector<Expr *, 4> Args;
  bool IsBaseInitializer;
  bool IsDelegatingInitializer;

public:
  CXXCtorInitializer(SourceLocation Loc, llvm::StringRef Name,
                     llvm::ArrayRef<Expr *> Arguments,
                     bool IsBase = false, bool IsDelegating = false)
      : MemberLoc(Loc), MemberName(Name), Args(Arguments.begin(), Arguments.end()),
        IsBaseInitializer(IsBase), IsDelegatingInitializer(IsDelegating) {}

  SourceLocation getMemberLocation() const { return MemberLoc; }
  llvm::StringRef getMemberName() const { return MemberName; }
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
public:
  enum AccessSpecifier {
    AS_public,
    AS_protected,
    AS_private
  };

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
class NamespaceDecl : public NamedDecl {
  llvm::SmallVector<Decl *, 16> Decls;
  bool IsInline;

public:
  NamespaceDecl(SourceLocation Loc, llvm::StringRef Name, bool IsInline = false)
      : NamedDecl(Loc, Name), IsInline(IsInline) {}

  llvm::ArrayRef<Decl *> decls() const { return Decls; }
  void addDecl(Decl *D) { Decls.push_back(D); }

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
class TranslationUnitDecl : public Decl {
  llvm::SmallVector<Decl *, 32> Decls;

public:
  TranslationUnitDecl(SourceLocation Loc) : Decl(Loc) {}

  llvm::ArrayRef<Decl *> decls() const { return Decls; }
  void addDecl(Decl *D) { Decls.push_back(D); }

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
public:
  UsingDecl(SourceLocation Loc, llvm::StringRef Name)
      : NamedDecl(Loc, Name) {}

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
public:
  UsingDirectiveDecl(SourceLocation Loc, llvm::StringRef Name)
      : NamedDecl(Loc, Name) {}

  NodeKind getKind() const override { return NodeKind::UsingDirectiveDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::UsingDirectiveDeclKind;
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
  llvm::SmallVector<NamedDecl *, 8> TemplateParams;
  Decl *TemplatedDecl;

public:
  TemplateDecl(SourceLocation Loc, llvm::StringRef Name, Decl *TemplatedDecl)
      : NamedDecl(Loc, Name), TemplatedDecl(TemplatedDecl) {}

  llvm::ArrayRef<NamedDecl *> getTemplateParameters() const {
    return TemplateParams;
  }
  void addTemplateParameter(NamedDecl *Param) { TemplateParams.push_back(Param); }

  Decl *getTemplatedDecl() const { return TemplatedDecl; }

  NodeKind getKind() const override { return NodeKind::TemplateDeclKind; }

  void dump(raw_ostream &OS, unsigned Indent = 0) const override;

  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::TemplateDeclKind ||
           N->getKind() == NodeKind::TemplateTemplateParmDeclKind;
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
class ModuleDecl : public NamedDecl {
  llvm::StringRef ModuleName;
  llvm::StringRef PartitionName;
  bool IsExported;
  bool IsModulePartition;

public:
  ModuleDecl(SourceLocation Loc, llvm::StringRef ModuleName,
             bool IsExported = false, llvm::StringRef PartitionName = "",
             bool IsModulePartition = false)
      : NamedDecl(Loc, ModuleName), ModuleName(ModuleName),
        PartitionName(PartitionName), IsExported(IsExported),
        IsModulePartition(IsModulePartition) {}

  llvm::StringRef getModuleName() const { return ModuleName; }
  llvm::StringRef getPartitionName() const { return PartitionName; }
  bool isExported() const { return IsExported; }
  bool isModulePartition() const { return IsModulePartition; }

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
  bool IsExported;

public:
  ImportDecl(SourceLocation Loc, llvm::StringRef ModuleName,
             bool IsExported = false, llvm::StringRef PartitionName = "")
      : NamedDecl(Loc, ModuleName), ModuleName(ModuleName),
        PartitionName(PartitionName), IsExported(IsExported) {}

  llvm::StringRef getModuleName() const { return ModuleName; }
  llvm::StringRef getPartitionName() const { return PartitionName; }
  bool isExported() const { return IsExported; }

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
class LinkageSpecDecl : public Decl {
public:
  enum Language { C, CXX };

private:
  Language Lang;
  llvm::SmallVector<Decl *, 8> Decls;
  bool HasBraces;

public:
  LinkageSpecDecl(SourceLocation Loc, Language L, bool HasBraces = true)
      : Decl(Loc), Lang(L), HasBraces(HasBraces) {}

  Language getLanguage() const { return Lang; }
  bool hasBraces() const { return HasBraces; }

  llvm::ArrayRef<Decl *> decls() const { return Decls; }
  void addDecl(Decl *D) { Decls.push_back(D); }

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

} // namespace blocktype
