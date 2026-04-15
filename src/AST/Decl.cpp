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

} // namespace blocktype
