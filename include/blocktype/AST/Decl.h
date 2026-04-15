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

public:
  VarDecl(SourceLocation Loc, llvm::StringRef Name, QualType T, Expr *Init = nullptr)
      : ValueDecl(Loc, Name, T), Init(Init) {}

  Expr *getInit() const { return Init; }
  void setInit(Expr *I) { Init = I; }

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

public:
  FunctionDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
               llvm::ArrayRef<ParmVarDecl *> Params, Stmt *Body = nullptr,
               bool IsInline = false, bool IsConstexpr = false)
      : ValueDecl(Loc, Name, T), Params(Params.begin(), Params.end()),
        Body(Body), IsInline(IsInline), IsConstexpr(IsConstexpr) {}

  llvm::ArrayRef<ParmVarDecl *> getParams() const { return Params; }
  unsigned getNumParams() const { return Params.size(); }
  ParmVarDecl *getParamDecl(unsigned i) const { return Params[i]; }

  Stmt *getBody() const { return Body; }
  void setBody(Stmt *B) { Body = B; }

  bool isInline() const { return IsInline; }
  bool isConstexpr() const { return IsConstexpr; }

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
  bool IsMutable;

public:
  FieldDecl(SourceLocation Loc, llvm::StringRef Name, QualType T,
            Expr *BitWidth = nullptr, bool IsMutable = false)
      : ValueDecl(Loc, Name, T), BitWidth(BitWidth), IsMutable(IsMutable) {}

  Expr *getBitWidth() const { return BitWidth; }
  bool isMutable() const { return IsMutable; }

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

/// TypedefDecl - Typedef declaration.
class TypedefDecl : public TypeDecl {
  QualType UnderlyingType;

public:
  TypedefDecl(SourceLocation Loc, llvm::StringRef Name, QualType Underlying)
      : TypeDecl(Loc, Name), UnderlyingType(Underlying) {}

  QualType getUnderlyingType() const { return UnderlyingType; }

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
    return N->getKind() == NodeKind::RecordDeclKind;
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

} // namespace blocktype
