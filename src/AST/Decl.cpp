//===--- Decl.cpp - Declaration AST Node Implementation -----*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Decl classes.
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// VarDecl
//===----------------------------------------------------------------------===//

void VarDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "VarDecl " << getName();
  if (T.getTypePtr()) {
    OS << " '";
    T.dump(OS);
    OS << "'";
  }
  if (Init) {
    OS << " cinit\n";
    Init->dump(OS, Indent + 2);
  } else {
    OS << "\n";
  }
}

//===----------------------------------------------------------------------===//
// FunctionDecl
//===----------------------------------------------------------------------===//

void FunctionDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "FunctionDecl " << getName();
  if (T.getTypePtr()) {
    OS << " '";
    T.dump(OS);
    OS << "'";
  }
  if (IsInline)
    OS << " inline";
  if (IsConstexpr)
    OS << " constexpr";
  OS << "\n";

  if (!Params.empty()) {
    for (auto *Param : Params) {
      Param->dump(OS, Indent + 2);
    }
  }

  if (Body) {
    Body->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// ParmVarDecl
//===----------------------------------------------------------------------===//

void ParmVarDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ParmVarDecl " << getName();
  if (T.getTypePtr()) {
    OS << " '";
    T.dump(OS);
    OS << "'";
  }
  if (Init) {
    OS << " = ";
    Init->dump(OS, 0);
  } else {
    OS << "\n";
  }
}

//===----------------------------------------------------------------------===//
// FieldDecl
//===----------------------------------------------------------------------===//

void FieldDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "FieldDecl " << getName();
  if (T.getTypePtr()) {
    OS << " '";
    T.dump(OS);
    OS << "'";
  }
  if (IsMutable)
    OS << " mutable";
  if (BitWidth) {
    OS << " : ";
    BitWidth->dump(OS, 0);
  } else {
    OS << "\n";
  }
}

//===----------------------------------------------------------------------===//
// EnumConstantDecl
//===----------------------------------------------------------------------===//

void EnumConstantDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "EnumConstantDecl " << getName();
  if (InitVal) {
    OS << " = ";
    InitVal->dump(OS, 0);
  } else {
    OS << "\n";
  }
}

//===----------------------------------------------------------------------===//
// TypedefDecl
//===----------------------------------------------------------------------===//

void TypedefDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TypedefDecl " << getName() << " '";
  UnderlyingType.dump(OS);
  OS << "'\n";
}

//===----------------------------------------------------------------------===//
// EnumDecl
//===----------------------------------------------------------------------===//

void EnumDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "EnumDecl " << getName() << "\n";
  for (auto *Enumerator : Enumerators) {
    Enumerator->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// RecordDecl
//===----------------------------------------------------------------------===//

void RecordDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << (isClass() ? "ClassDecl " : isStruct() ? "StructDecl " : "UnionDecl ");
  OS << getName() << "\n";
  for (auto *Field : Fields) {
    Field->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// NamespaceDecl
//===----------------------------------------------------------------------===//

void NamespaceDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "NamespaceDecl " << getName();
  if (IsInline)
    OS << " inline";
  OS << "\n";
  for (auto *D : Decls) {
    D->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// TranslationUnitDecl
//===----------------------------------------------------------------------===//

void TranslationUnitDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TranslationUnitDecl\n";
  for (auto *D : Decls) {
    D->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// UsingDecl
//===----------------------------------------------------------------------===//

void UsingDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "UsingDecl " << getName() << "\n";
}

//===----------------------------------------------------------------------===//
// UsingDirectiveDecl
//===----------------------------------------------------------------------===//

void UsingDirectiveDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "UsingDirectiveDecl " << getName() << "\n";
}

//===----------------------------------------------------------------------===//
// LabelDecl
//===----------------------------------------------------------------------===//

void LabelDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "LabelDecl " << getName() << "\n";
}

//===----------------------------------------------------------------------===//
// CXXRecordDecl
//===----------------------------------------------------------------------===//

void CXXRecordDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << (isClass() ? "CXXRecordDecl " : "RecordDecl ") << getName() << "\n";

  if (!Bases.empty()) {
    printIndent(OS, Indent + 2);
    OS << "Bases:\n";
    for (const auto &Base : Bases) {
      printIndent(OS, Indent + 4);
      OS << "base ";
      Base.getType().dump(OS);
      OS << "\n";
    }
  }

  for (auto *Member : Members) {
    Member->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// CXXMethodDecl
//===----------------------------------------------------------------------===//

void CXXMethodDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXMethodDecl " << getName();
  if (T.getTypePtr()) {
    OS << " '";
    T.dump(OS);
    OS << "'";
  }
  if (IsStatic)
    OS << " static";
  if (IsConst)
    OS << " const";
  if (IsVirtual)
    OS << " virtual";
  if (IsOverride)
    OS << " override";
  if (IsFinal)
    OS << " final";
  OS << "\n";

  if (!getParams().empty()) {
    for (auto *Param : getParams()) {
      Param->dump(OS, Indent + 2);
    }
  }

  if (getBody()) {
    getBody()->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// CXXConstructorDecl
//===----------------------------------------------------------------------===//

void CXXConstructorDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXConstructorDecl " << getParent()->getName();
  if (IsExplicit)
    OS << " explicit";
  OS << "\n";

  if (!getParams().empty()) {
    for (auto *Param : getParams()) {
      Param->dump(OS, Indent + 2);
    }
  }

  if (getBody()) {
    getBody()->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// CXXDestructorDecl
//===----------------------------------------------------------------------===//

CXXDestructorDecl::CXXDestructorDecl(SourceLocation Loc, CXXRecordDecl *Parent,
                                     Stmt *Body)
    : CXXMethodDecl(Loc, "", QualType(), llvm::ArrayRef<ParmVarDecl *>(), Parent,
                    Body) {
  // Name will be constructed as "~ClassName" when needed
}

void CXXDestructorDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXDestructorDecl ~" << getParent()->getName() << "\n";

  if (getBody()) {
    getBody()->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// CXXConversionDecl
//===----------------------------------------------------------------------===//

CXXConversionDecl::CXXConversionDecl(SourceLocation Loc, QualType ConvType,
                                     CXXRecordDecl *Parent, Stmt *Body)
    : CXXMethodDecl(Loc, "operator", ConvType, llvm::ArrayRef<ParmVarDecl *>(),
                    Parent, Body),
      ConversionType(ConvType) {}

void CXXConversionDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXConversionDecl operator ";
  ConversionType.dump(OS);
  OS << "\n";

  if (getBody()) {
    getBody()->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// AccessSpecDecl
//===----------------------------------------------------------------------===//

void AccessSpecDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "AccessSpecDecl ";
  switch (Access) {
  case AS_public:
    OS << "public";
    break;
  case AS_protected:
    OS << "protected";
    break;
  case AS_private:
    OS << "private";
    break;
  }
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// TemplateDecl
//===----------------------------------------------------------------------===//

void TemplateDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TemplateDecl " << getName() << "\n";

  if (!TemplateParams.empty()) {
    printIndent(OS, Indent + 2);
    OS << "TemplateParameters:\n";
    for (auto *Param : TemplateParams) {
      Param->dump(OS, Indent + 4);
    }
  }

  if (TemplatedDecl) {
    printIndent(OS, Indent + 2);
    OS << "TemplatedDecl:\n";
    TemplatedDecl->dump(OS, Indent + 4);
  }
}

//===----------------------------------------------------------------------===//
// TemplateTypeParmDecl
//===----------------------------------------------------------------------===//

void TemplateTypeParmDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TemplateTypeParmDecl " << getName();
  if (IsParameterPack)
    OS << " ...";
  OS << (IsTypename ? " typename" : " class");
  if (!DefaultArgument.isNull()) {
    OS << " = ";
    DefaultArgument.dump(OS);
  }
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// NonTypeTemplateParmDecl
//===----------------------------------------------------------------------===//

void NonTypeTemplateParmDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "NonTypeTemplateParmDecl " << getName();
  if (IsParameterPack)
    OS << " ...";
  OS << " '";
  T.dump(OS);
  OS << "'\n";
}

//===----------------------------------------------------------------------===//
// TemplateTemplateParmDecl
//===----------------------------------------------------------------------===//

void TemplateTemplateParmDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TemplateTemplateParmDecl " << getName();
  if (IsParameterPack)
    OS << " ...";
  OS << "\n";

  if (!TemplateParams.empty()) {
    printIndent(OS, Indent + 2);
    OS << "TemplateParameters:\n";
    for (auto *Param : TemplateParams) {
      Param->dump(OS, Indent + 4);
    }
  }
}

} // namespace blocktype
