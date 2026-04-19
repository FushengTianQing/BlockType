//===--- Expr.cpp - Expression AST Node Implementation ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/Sema/Lookup.h"  // NestedNameSpecifier definition
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

// Helper function for Requirement classes (not derived from ASTNode)
static void printRequirementIndent(raw_ostream &OS, unsigned Indent) {
  for (unsigned I = 0; I < Indent; ++I)
    OS << "  ";
}

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
// ArraySubscriptExpr
//===----------------------------------------------------------------------===//

void ArraySubscriptExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ArraySubscriptExpr";
  if (Indices.size() > 1)
    OS << " [" << Indices.size() << " indices]";
  OS << "\n";
  if (Base)
    Base->dump(OS, Indent + 1);
  for (Expr *Idx : Indices) {
    if (Idx)
      Idx->dump(OS, Indent + 1);
  }
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

void UnaryExprOrTypeTraitExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << (Kind == UnaryExprOrTypeTrait::SizeOf ? "sizeof" : "alignof");
  if (IsArgumentType) {
    OS << "(type)\n";
    if (!ArgType.isNull())
      ArgType.dump(OS);
  } else {
    OS << "(expr)\n";
    if (ArgExpr)
      ArgExpr->dump(OS, Indent + 1);
  }
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
// InitListExpr
//===----------------------------------------------------------------------===//

void InitListExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "InitListExpr: {" << getNumInits() << " elements}\n";
  for (Expr *Init : Inits) {
    if (Init)
      Init->dump(OS, Indent + 1);
    else {
      printIndent(OS, Indent + 1);
      OS << "<nullptr>\n";
    }
  }
}

//===----------------------------------------------------------------------===//
// DesignatedInitExpr
//===----------------------------------------------------------------------===//

void DesignatedInitExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "DesignatedInitExpr: ";
  for (const auto &D : Designators) {
    if (D.isFieldDesignator()) {
      OS << "." << D.getFieldName();
    } else if (D.isArrayDesignator()) {
      OS << "[...]";
    }
  }
  OS << " = \n";
  if (Init) {
    Init->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// CXXNewExpr
//===----------------------------------------------------------------------===//

void CXXNewExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXNewExpr: new";
  if (ArraySize)
    OS << "[]";
  OS << "\n";
  if (ArraySize) {
    printIndent(OS, Indent + 1);
    OS << "array size:\n";
    ArraySize->dump(OS, Indent + 2);
  }
  if (Initializer)
    Initializer->dump(OS, Indent + 1);
}

//===----------------------------------------------------------------------===//
// CXXDeleteExpr
//===----------------------------------------------------------------------===//

void CXXDeleteExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXDeleteExpr: " << (IsArray ? "delete[]" : "delete");
  if (!AllocatedType.isNull()) {
    OS << " ";
    AllocatedType.dump(OS);
  }
  OS << "\n";
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
  OS << "CXXDynamicCastExpr: " << getCastKindName(Kind);
  if (!DestType.isNull() && DestType.getTypePtr()) {
    OS << " dest='";
    DestType.getTypePtr()->dump(OS);
    OS << "'";
  }
  OS << "\n";
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

//===----------------------------------------------------------------------===//
// CoawaitExpr
//===----------------------------------------------------------------------===//

void CoawaitExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CoawaitExpr: co_await\n";
  if (Operand != nullptr) {
    Operand->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// LambdaExpr
//===----------------------------------------------------------------------===//

void LambdaExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "LambdaExpr";
  if (IsMutable) {
    OS << " mutable";
  }
  OS << "\n";

  if (TemplateParams) {
    printIndent(OS, Indent + 1);
    OS << "TemplateParams:\n";
    TemplateParams->dump(OS, Indent + 2);
  }

  if (Attrs) {
    printIndent(OS, Indent + 1);
    OS << "Attributes:\n";
    for (auto *Attr : Attrs->getAttributes()) {
      if (Attr) {
        printIndent(OS, Indent + 2);
        OS << Attr->getFullName() << "\n";
      }
    }
  }

  if (!Captures.empty()) {
    printIndent(OS, Indent + 1);
    OS << "Captures:\n";
    for (const auto &Cap : Captures) {
      printIndent(OS, Indent + 2);
      switch (Cap.Kind) {
      case LambdaCapture::ByCopy:
        OS << "[=] " << Cap.Name;
        break;
      case LambdaCapture::ByRef:
        OS << "[&] " << Cap.Name;
        break;
      case LambdaCapture::InitCopy:
        OS << Cap.Name << " = ...";
        break;
      }
      OS << "\n";
    }
  }

  if (!Params.empty()) {
    printIndent(OS, Indent + 1);
    OS << "Parameters:\n";
    for (const auto *Param : Params) {
      if (Param != nullptr) {
        Param->dump(OS, Indent + 2);
      }
    }
  }

  if (Body != nullptr) {
    printIndent(OS, Indent + 1);
    OS << "Body:\n";
    Body->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// Requirements
//===----------------------------------------------------------------------===//

void TypeRequirement::dump(raw_ostream &OS, unsigned Indent) const {
  printRequirementIndent(OS, Indent);
  OS << "TypeRequirement: ";
  if (!Type.isNull()) {
    OS << "<type>";
  }
  OS << "\n";
}

void ExprRequirement::dump(raw_ostream &OS, unsigned Indent) const {
  printRequirementIndent(OS, Indent);
  OS << "ExprRequirement";
  if (IsNoexcept) {
    OS << " noexcept";
  }
  OS << "\n";
  if (Expression != nullptr) {
    Expression->dump(OS, Indent + 1);
  }
}

void CompoundRequirement::dump(raw_ostream &OS, unsigned Indent) const {
  printRequirementIndent(OS, Indent);
  OS << "CompoundRequirement";
  if (IsNoexcept) {
    OS << " noexcept";
  }
  if (!ReturnType.isNull()) {
    OS << " -> ";
    ReturnType.dump(OS);
  }
  OS << "\n";
  if (Expression != nullptr) {
    Expression->dump(OS, Indent + 1);
  }
  if (Body != nullptr) {
    Body->dump(OS, Indent + 1);
  }
}

void NestedRequirement::dump(raw_ostream &OS, unsigned Indent) const {
  printRequirementIndent(OS, Indent);
  OS << "NestedRequirement\n";
  if (Constraint != nullptr) {
    Constraint->dump(OS, Indent + 1);
  }
}

//===----------------------------------------------------------------------===//
// RequiresExpr
//===----------------------------------------------------------------------===//

void RequiresExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "RequiresExpr\n";

  if (!Requirements.empty()) {
    printIndent(OS, Indent + 1);
    OS << "Requirements:\n";
    for (const auto *Req : Requirements) {
      if (Req != nullptr) {
        Req->dump(OS, Indent + 2);
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// CXXFoldExpr
//===----------------------------------------------------------------------===//

void CXXFoldExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXFoldExpr '";
  // Print operator
  switch (Op) {
  case BinaryOpKind::Add: OS << "+"; break;
  case BinaryOpKind::Sub: OS << "-"; break;
  case BinaryOpKind::Mul: OS << "*"; break;
  case BinaryOpKind::Div: OS << "/"; break;
  case BinaryOpKind::And: OS << "&"; break;
  case BinaryOpKind::Or: OS << "|"; break;
  case BinaryOpKind::Xor: OS << "^"; break;
  case BinaryOpKind::LAnd: OS << "&&"; break;
  case BinaryOpKind::LOr: OS << "||"; break;
  case BinaryOpKind::Comma: OS << ","; break;
  default: OS << "???"; break;
  }
  OS << "' " << (IsRightFold ? "(right fold)" : "(left fold)") << "\n";

  if (LHS != nullptr) {
    printIndent(OS, Indent + 1);
    OS << "LHS:\n";
    LHS->dump(OS, Indent + 2);
  }
  if (RHS != nullptr) {
    printIndent(OS, Indent + 1);
    OS << "RHS:\n";
    RHS->dump(OS, Indent + 2);
  }
  if (Pattern != nullptr) {
    printIndent(OS, Indent + 1);
    OS << "Pattern:\n";
    Pattern->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// PackIndexingExpr
//===----------------------------------------------------------------------===//

void PackIndexingExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "PackIndexingExpr\n";

  if (Pack != nullptr) {
    printIndent(OS, Indent + 1);
    OS << "Pack:\n";
    Pack->dump(OS, Indent + 2);
  }
  if (Index != nullptr) {
    printIndent(OS, Indent + 1);
    OS << "Index:\n";
    Index->dump(OS, Indent + 2);
  }
}

//===----------------------------------------------------------------------===//
// ReflexprExpr
//===----------------------------------------------------------------------===//

void ReflexprExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "ReflexprExpr " << (reflectsType() ? "[type]" : "[expr]") << "\n";

  if (reflectsType()) {
    printIndent(OS, Indent + 1);
    OS << "ReflectedType: ";
    if (ReflectedType.getTypePtr()) {
      ReflectedType.getTypePtr()->dump(OS);
    } else {
      OS << "<<null>>\n";
    }
  } else if (Argument != nullptr) {
    printIndent(OS, Indent + 1);
    OS << "Argument:\n";
    Argument->dump(OS, Indent + 2);
  }

  if (ResultType.getTypePtr()) {
    printIndent(OS, Indent + 1);
    OS << "ResultType: ";
    ResultType.getTypePtr()->dump(OS);
  }
}

//===----------------------------------------------------------------------===//
// CXXDependentScopeMemberExpr
//===----------------------------------------------------------------------===//

void CXXDependentScopeMemberExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "CXXDependentScopeMemberExpr " << (IsArrow ? "->" : ".") << MemberName << "\n";

  if (Base) {
    printIndent(OS, Indent + 1);
    OS << "Base:\n";
    Base->dump(OS, Indent + 2);
  } else if (!BaseType.isNull()) {
    printIndent(OS, Indent + 1);
    OS << "BaseType: ";
    BaseType.dump(OS);
    OS << "\n";
  }
}

//===----------------------------------------------------------------------===//
// DependentScopeDeclRefExpr
//===----------------------------------------------------------------------===//

void DependentScopeDeclRefExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "DependentScopeDeclRefExpr ";
  if (Qualifier) {
    // Print qualifier kind without calling getAsString() to avoid
    // cross-library dependency (NestedNameSpecifier methods are in Sema).
    switch (Qualifier->getKind()) {
    case NestedNameSpecifier::Global: OS << "::"; break;
    case NestedNameSpecifier::Namespace:
      OS << "<ns>::"; break;
    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TemplateTypeSpec:
      OS << "<type>::"; break;
    case NestedNameSpecifier::Identifier:
      OS << Qualifier->getAsIdentifier() << "::"; break;
    }
  }
  OS << DeclName << "\n";
}

//===----------------------------------------------------------------------===//
// TemplateSpecializationExpr
//===----------------------------------------------------------------------===//

void TemplateSpecializationExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "TemplateSpecializationExpr: " << TemplateName;
  if (TemplateDecl) {
    OS << " (resolved)";
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
      case TemplateArgumentKind::Expression:
        OS << "Expression: ";
        if (Arg.getAsExpr()) {
          OS << "<expr>\n";
          Arg.getAsExpr()->dump(OS, Indent + 3);
        } else {
          OS << "<nullptr>\n";
        }
        break;
      case TemplateArgumentKind::Template:
        OS << "Template: <template>\n";
        break;
      case TemplateArgumentKind::Integral:
        OS << "Integral: " << Arg.getAsIntegral() << "\n";
        break;
      case TemplateArgumentKind::Declaration:
        OS << "Declaration: <decl>\n";
        break;
      case TemplateArgumentKind::NullPtr:
        OS << "NullPtr\n";
        break;
      case TemplateArgumentKind::TemplateExpansion:
        OS << "TemplateExpansion: <template...>\n";
        break;
      case TemplateArgumentKind::Pack:
        OS << "Pack: <" << Arg.getNumPackArguments() << " args>\n";
        break;
      case TemplateArgumentKind::Null:
        OS << "Null\n";
        break;
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// Expr base class
//===----------------------------------------------------------------------===//

bool Expr::isTypeDependent() const {
  return ExprTy && ExprTy->isDependentType();
}

//===----------------------------------------------------------------------===//
// P7.1.2: DecayCopyExpr dump
//===----------------------------------------------------------------------===//

void DecayCopyExpr::dump(raw_ostream &OS, unsigned Indent) const {
  printIndent(OS, Indent);
  OS << "DecayCopyExpr " << (IsDirectInit ? "auto{...}" : "auto(...)");
  if (!getType().isNull())
    OS << " type='" << getType().getTypePtr() << "'";
  OS << "\n";
  if (SubExpr)
    SubExpr->dump(OS, Indent + 1);
}

} // namespace blocktype
