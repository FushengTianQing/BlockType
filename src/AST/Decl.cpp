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
#include "blocktype/AST/TemplateParameterList.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// TemplateDecl helper methods
//===----------------------------------------------------------------------===//

void TemplateDecl::addTemplateParameter(NamedDecl *Param) {
  if (!Params) {
    Params = new TemplateParameterList(
        SourceLocation(), SourceLocation(), SourceLocation(), {});
  }
  Params->addParam(Param);
}

void TemplateDecl::setRequiresClause(Expr *E) {
  if (!Params) {
    Params = new TemplateParameterList(
        SourceLocation(), SourceLocation(), SourceLocation(), {});
  }
  Params->setRequiresClause(E);
}

//===----------------------------------------------------------------------===//
// VarDecl
//===----------------------------------------------------------------------===//

void VarDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "VarDecl " << getName();
  if (IsStatic) {
    OS << " static";
  }
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
  if (HasNoexceptSpec) {
    OS << " noexcept";
    if (NoexceptExpr) {
      OS << "(";
      NoexceptExpr->dump(OS, 0);
      OS << ")";
    } else if (!NoexceptValue) {
      OS << "(false)";
    }
  }
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
  // Print access specifier
  if (Access == AccessSpecifier::AS_public)
    OS << " public";
  else if (Access == AccessSpecifier::AS_protected)
    OS << " protected";
  else
    OS << " private";
  if (IsMutable)
    OS << " mutable";
  if (BitWidth) {
    OS << " : ";
    BitWidth->dump(OS, 0);
  }
  if (InClassInitializer) {
    OS << "\n";
    printIndent(OS, Indent + 2);
    OS << "in-class initializer:\n";
    InClassInitializer->dump(OS, Indent + 4);
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
  OS << "EnumDecl " << getName();
  if (IsScoped) {
    OS << " scoped";
  }
  if (UnderlyingType.getTypePtr()) {
    OS << " : ";
    UnderlyingType.dump(OS);
  }
  OS << "\n";
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
  for (auto *D : DeclContext::decls()) {
    D->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// TranslationUnitDecl
//===----------------------------------------------------------------------===//

void TranslationUnitDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TranslationUnitDecl\n";
  for (auto *D : DeclContext::decls()) {
    D->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// UsingDecl
//===----------------------------------------------------------------------===//

void UsingDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "UsingDecl ";
  if (IsInheritingConstructor) {
    OS << "[inheriting-constructor] ";
  }
  if (HasNestedNameSpecifier) {
    OS << NestedNameSpecifier;
  }
  OS << getName() << "\n";
}

//===----------------------------------------------------------------------===//
// UsingDirectiveDecl
//===----------------------------------------------------------------------===//

void UsingDirectiveDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "UsingDirectiveDecl ";
  if (HasNestedNameSpecifier) {
    OS << NestedNameSpecifier;
  }
  OS << getName() << "\n";
}

//===----------------------------------------------------------------------===//
// NamespaceAliasDecl
//===----------------------------------------------------------------------===//

void NamespaceAliasDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "NamespaceAliasDecl " << getName() << " = ";
  if (!NestedNameSpecifier.empty()) {
    OS << NestedNameSpecifier;
  }
  OS << AliasedName << "\n";
}

//===----------------------------------------------------------------------===//
// UsingEnumDecl
//===----------------------------------------------------------------------===//

void UsingEnumDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "UsingEnumDecl ";
  if (HasNestedNameSpecifier) {
    OS << NestedNameSpecifier;
  }
  OS << EnumName << "\n";
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

bool CXXRecordDecl::isDerivedFrom(const CXXRecordDecl *Base) const {
  if (!Base) {
    return false;
  }

  // Check direct base classes
  for (const auto &BaseSpec : Bases) {
    QualType BaseTy = BaseSpec.getType();
    if (auto *BaseRT = llvm::dyn_cast_or_null<RecordType>(BaseTy.getTypePtr())) {
      if (auto *BaseCXXRD = llvm::dyn_cast<CXXRecordDecl>(BaseRT->getDecl())) {
        // Check if this is the target base class
        if (BaseCXXRD == Base) {
          return true;
        }
        // Recursively check if the base class is derived from the target
        if (BaseCXXRD->isDerivedFrom(Base)) {
          return true;
        }
      }
    }
  }

  return false;
}

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
  // Print access specifier
  if (Access == AccessSpecifier::AS_public)
    OS << " public";
  else if (Access == AccessSpecifier::AS_protected)
    OS << " protected";
  else
    OS << " private";
  if (IsStatic)
    OS << " static";
  if (IsConst)
    OS << " const";
  if (IsVolatile)
    OS << " volatile";
  if (IsVirtual)
    OS << " virtual";
  if (IsPureVirtual)
    OS << " pure";
  if (IsOverride)
    OS << " override";
  if (IsFinal)
    OS << " final";
  if (IsDefaulted)
    OS << " default";
  if (IsDeleted)
    OS << " delete";
  if (RefQualifier == RQ_LValue)
    OS << " &";
  else if (RefQualifier == RQ_RValue)
    OS << " &&";
  if (hasNoexceptSpec()) {
    OS << " noexcept";
    if (getNoexceptExpr()) {
      OS << "(";
      getNoexceptExpr()->dump(OS, 0);
      OS << ")";
    } else if (!getNoexceptValue()) {
      OS << "(false)";
    }
  }
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

void CXXCtorInitializer::dump(raw_ostream &OS, unsigned Indent) const {
  for (unsigned I = 0; I < Indent; ++I)
    OS << "  ";
  if (IsBaseInitializer)
    OS << "base ";
  else if (IsDelegatingInitializer)
    OS << "delegating ";
  OS << MemberName << "(";
  for (unsigned i = 0; i < Args.size(); ++i) {
    if (i > 0)
      OS << ", ";
    Args[i]->dump(OS, 0);
  }
  OS << ")\n";
}

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

  if (!Initializers.empty()) {
    printIndent(OS, Indent + 2);
    OS << "member initializers:\n";
    for (auto *Init : Initializers) {
      Init->dump(OS, Indent + 4);
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
  case AccessSpecifier::AS_public:
    OS << "public";
    break;
  case AccessSpecifier::AS_protected:
    OS << "protected";
    break;
  case AccessSpecifier::AS_private:
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

  if (Params) {
    Params->dump(OS, Indent);
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
  if (Constraint) {
    OS << " requires ";
    Constraint->dump(OS, Indent);
  }
  OS << "\n";

  // Dump nested template parameters via TemplateParameterList
  if (auto *TPL = getTemplateParameterList()) {
    TPL->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// ModuleDecl
//===----------------------------------------------------------------------===//

void ModuleDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ModuleDecl ";
  if (IsExported)
    OS << "export ";
  if (IsGlobalModuleFragment) {
    OS << "(global module fragment)";
  } else if (IsPrivateModuleFragment) {
    OS << "(private module fragment)";
  } else {
    OS << ModuleName;
    if (!PartitionName.empty())
      OS << ":" << PartitionName;
  }
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// ImportDecl
//===----------------------------------------------------------------------===//

void ImportDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ImportDecl ";
  if (IsExported)
    OS << "export ";
  OS << ModuleName;
  if (!PartitionName.empty())
    OS << ":" << PartitionName;
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// ExportDecl
//===----------------------------------------------------------------------===//

void ExportDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ExportDecl\n";

  if (ExportedDecl) {
    ExportedDecl->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// StaticAssertDecl
//===----------------------------------------------------------------------===//

void StaticAssertDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "StaticAssertDecl";
  if (!Message.empty())
    OS << " \"" << Message << "\"";
  OS << "\n";
  if (AssertExpr) {
    AssertExpr->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// LinkageSpecDecl
//===----------------------------------------------------------------------===//

void LinkageSpecDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "LinkageSpecDecl " << (Lang == C ? "\"C\"" : "\"C++\"");
  if (HasBraces)
    OS << " {\n";
  else
    OS << "\n";

  for (auto *D : DeclContext::decls()) {
    D->dump(OS, Indent + 2);
  }

  if (HasBraces) {
    printIndent(OS, Indent);
    OS << "}\n";
  }
}

//===----------------------------------------------------------------------===//
// TypeAliasDecl
//===----------------------------------------------------------------------===//

void TypeAliasDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TypeAliasDecl " << getName() << " = '";
  UnderlyingType.dump(OS);
  OS << "'\n";
}

//===----------------------------------------------------------------------===//
// FriendDecl
//===----------------------------------------------------------------------===//

void FriendDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "FriendDecl ";
  if (IsFriendType) {
    OS << "type '";
    FriendType.dump(OS);
    OS << "'";
  } else if (FriendDecl_) {
    OS << "decl '";
    if (auto *FD = llvm::dyn_cast<FunctionDecl>(FriendDecl_)) {
      OS << FD->getName();
    } else if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(FriendDecl_)) {
      OS << RD->getName();
    }
    OS << "'";
  }
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// ConceptDecl
//===----------------------------------------------------------------------===//

void ConceptDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ConceptDecl " << getName();
  if (ConstraintExpr) {
    OS << " constraint:\n";
    ConstraintExpr->dump(OS, Indent + 2);
  } else {
    OS << "\n";
  }
  if (Template) {
    printIndent(OS, Indent + 2);
    OS << "Template Parameters:\n";
    for (auto *Param : Template->getTemplateParameters()) {
      Param->dump(OS, Indent + 4);
    }
  }
}

//===----------------------------------------------------------------------===//
// AsmDecl
//===----------------------------------------------------------------------===//

void AsmDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "AsmDecl \"" << AsmString << "\"\n";
}

//===----------------------------------------------------------------------===//
// CXXDeductionGuideDecl
//===----------------------------------------------------------------------===//

void CXXDeductionGuideDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXDeductionGuideDecl " << getName();
  if (IsExplicit) {
    OS << " explicit";
  }
  OS << "\n";
  printIndent(OS, Indent + 2);
  OS << "Type: ";
  getType().dump(OS);
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// AttributeDecl
//===----------------------------------------------------------------------===//

void AttributeDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "AttributeDecl [[";
  if (hasNamespace()) {
    OS << AttributeNamespace << "::";
  }
  OS << AttributeName;
  if (ArgumentExpr) {
    OS << "(";
    ArgumentExpr->dump(OS, Indent + 2);
    OS << ")";
  }
  OS << "]]\n";
}

void AttributeListDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "AttributeListDecl [[";
  for (size_t I = 0; I < Attrs.size(); ++I) {
    if (I > 0)
      OS << ", ";
    if (Attrs[I]->hasNamespace()) {
      OS << Attrs[I]->getNamespace() << "::";
    }
    OS << Attrs[I]->getAttributeName();
    if (Attrs[I]->hasArgument()) {
      OS << "(";
      Attrs[I]->getArgumentExpr()->dump(OS, Indent + 2);
      OS << ")";
    }
  }
  OS << "]]\n";
}

//===----------------------------------------------------------------------===//
// Template Specialization Declarations
//===----------------------------------------------------------------------===//

void FunctionTemplateDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "FunctionTemplateDecl " << getName() << "\n";
  if (!getTemplateParameters().empty()) {
    printIndent(OS, Indent + 1);
    OS << "TemplateParameters:\n";
    for (auto *Param : getTemplateParameters()) {
      Param->dump(OS, Indent + 2);
    }
  }
  if (getTemplatedDecl()) {
    printIndent(OS, Indent + 1);
    OS << "TemplatedDecl:\n";
    getTemplatedDecl()->dump(OS, Indent + 2);
  }
}

void ClassTemplateDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ClassTemplateDecl " << getName() << "\n";
  if (!getTemplateParameters().empty()) {
    printIndent(OS, Indent + 1);
    OS << "TemplateParameters:\n";
    for (auto *Param : getTemplateParameters()) {
      Param->dump(OS, Indent + 2);
    }
  }
  if (getTemplatedDecl()) {
    printIndent(OS, Indent + 1);
    OS << "TemplatedDecl:\n";
    getTemplatedDecl()->dump(OS, Indent + 2);
  }
  if (!Specializations.empty()) {
    printIndent(OS, Indent + 1);
    OS << "Specializations: " << Specializations.size() << "\n";
  }
}

void VarTemplateDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "VarTemplateDecl " << getName() << "\n";
  if (!getTemplateParameters().empty()) {
    printIndent(OS, Indent + 1);
    OS << "TemplateParameters:\n";
    for (auto *Param : getTemplateParameters()) {
      Param->dump(OS, Indent + 2);
    }
  }
  if (getTemplatedDecl()) {
    printIndent(OS, Indent + 1);
    OS << "TemplatedDecl:\n";
    getTemplatedDecl()->dump(OS, Indent + 2);
  }
}

void TypeAliasTemplateDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TypeAliasTemplateDecl " << getName() << "\n";
  if (!getTemplateParameters().empty()) {
    printIndent(OS, Indent + 1);
    OS << "TemplateParameters:\n";
    for (auto *Param : getTemplateParameters()) {
      Param->dump(OS, Indent + 2);
    }
  }
  if (getTemplatedDecl()) {
    printIndent(OS, Indent + 1);
    OS << "TemplatedDecl:\n";
    getTemplatedDecl()->dump(OS, Indent + 2);
  }
}

void ClassTemplateSpecializationDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ClassTemplateSpecializationDecl " << getName();
  if (IsExplicitSpecialization) {
    OS << " (explicit)";
  }
  OS << "\n";
  
  if (!TemplateArgs.empty()) {
    printIndent(OS, Indent + 1);
    OS << "TemplateArgs:\n";
    for (const auto &Arg : TemplateArgs) {
      printIndent(OS, Indent + 2);
      switch (Arg.getKind()) {
      case TemplateArgumentKind::Type:
        OS << "Type: ";
        Arg.getAsType().dump(OS);
        OS << "\n";
        break;
      case TemplateArgumentKind::NonType:
        OS << "NonType: <expr>\n";
        break;
      case TemplateArgumentKind::Template:
        OS << "Template: <template>\n";
        break;
      }
    }
  }
}

void ClassTemplatePartialSpecializationDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ClassTemplatePartialSpecializationDecl " << getName() << "\n";
  
  if (Params) {
    printIndent(OS, Indent + 1);
    OS << "PartialSpecParams:\n";
    Params->dump(OS, Indent + 2);
  }
  
  // Call base class dump for template args
  ClassTemplateSpecializationDecl::dump(OS, Indent);
}

void VarTemplateSpecializationDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "VarTemplateSpecializationDecl " << getName();
  if (IsExplicitSpecialization) {
    OS << " (explicit)";
  }
  OS << "\n";
  
  if (!TemplateArgs.empty()) {
    printIndent(OS, Indent + 1);
    OS << "TemplateArgs:\n";
    for (const auto &Arg : TemplateArgs) {
      printIndent(OS, Indent + 2);
      switch (Arg.getKind()) {
      case TemplateArgumentKind::Type:
        OS << "Type: ";
        Arg.getAsType().dump(OS);
        OS << "\n";
        break;
      case TemplateArgumentKind::NonType:
        OS << "NonType: <expr>\n";
        break;
      case TemplateArgumentKind::Template:
        OS << "Template: <template>\n";
        break;
      }
    }
  }
}

void VarTemplatePartialSpecializationDecl::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "VarTemplatePartialSpecializationDecl " << getName() << "\n";
  
  if (Params) {
    printIndent(OS, Indent + 1);
    OS << "PartialSpecParams:\n";
    Params->dump(OS, Indent + 2);
  }
  
  // Call base class dump
  VarTemplateSpecializationDecl::dump(OS, Indent);
}

} // namespace blocktype
