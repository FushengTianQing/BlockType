//===--- Type.cpp - Type System Implementation --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/AST/Type.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Decl.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

//===----------------------------------------------------------------------===//
// TemplateArgument Implementation
//===----------------------------------------------------------------------===//

TemplateArgument::TemplateArgument(const TemplateArgument &Other)
    : Kind(Other.Kind), IsPackExpansion(Other.IsPackExpansion), Data() {
  switch (Kind) {
  case TemplateArgumentKind::Null:
  case TemplateArgumentKind::NullPtr:
  case TemplateArgumentKind::Expression:
    Data.AsExpr = Other.Data.AsExpr;
    break;
  case TemplateArgumentKind::Type:
    Data.AsType = Other.Data.AsType;
    break;
  case TemplateArgumentKind::Declaration:
    Data.AsDecl = Other.Data.AsDecl;
    break;
  case TemplateArgumentKind::Integral:
    ::new (&Data.AsIntegral) llvm::APSInt(Other.Data.AsIntegral);
    break;
  case TemplateArgumentKind::Template:
  case TemplateArgumentKind::TemplateExpansion:
    Data.AsTemplate = Other.Data.AsTemplate;
    break;
  case TemplateArgumentKind::Pack:
    PackArgs = Other.PackArgs;
    PackNumArgs = Other.PackNumArgs;
    break;
  }
}

TemplateArgument::TemplateArgument(TemplateArgument &&Other)
    : Kind(Other.Kind), IsPackExpansion(Other.IsPackExpansion), Data() {
  switch (Kind) {
  case TemplateArgumentKind::Null:
  case TemplateArgumentKind::NullPtr:
  case TemplateArgumentKind::Expression:
    Data.AsExpr = Other.Data.AsExpr;
    break;
  case TemplateArgumentKind::Type:
    Data.AsType = std::move(Other.Data.AsType);
    break;
  case TemplateArgumentKind::Declaration:
    Data.AsDecl = Other.Data.AsDecl;
    break;
  case TemplateArgumentKind::Integral:
    ::new (&Data.AsIntegral) llvm::APSInt(std::move(Other.Data.AsIntegral));
    break;
  case TemplateArgumentKind::Template:
  case TemplateArgumentKind::TemplateExpansion:
    Data.AsTemplate = Other.Data.AsTemplate;
    break;
  case TemplateArgumentKind::Pack:
    PackArgs = Other.PackArgs;
    PackNumArgs = Other.PackNumArgs;
    break;
  }
}

TemplateArgument &
TemplateArgument::operator=(const TemplateArgument &Other) {
  if (this == &Other) return *this;

  // Destroy current value
  switch (Kind) {
  case TemplateArgumentKind::Integral:
    Data.AsIntegral.~APSInt();
    break;
  default:
    break;
  }

  Kind = Other.Kind;
  IsPackExpansion = Other.IsPackExpansion;

  switch (Kind) {
  case TemplateArgumentKind::Null:
  case TemplateArgumentKind::NullPtr:
  case TemplateArgumentKind::Expression:
    Data.AsExpr = Other.Data.AsExpr;
    break;
  case TemplateArgumentKind::Type:
    Data.AsType = Other.Data.AsType;
    break;
  case TemplateArgumentKind::Declaration:
    Data.AsDecl = Other.Data.AsDecl;
    break;
  case TemplateArgumentKind::Integral:
    ::new (&Data.AsIntegral) llvm::APSInt(Other.Data.AsIntegral);
    break;
  case TemplateArgumentKind::Template:
  case TemplateArgumentKind::TemplateExpansion:
    Data.AsTemplate = Other.Data.AsTemplate;
    break;
  case TemplateArgumentKind::Pack:
    PackArgs = Other.PackArgs;
    PackNumArgs = Other.PackNumArgs;
    break;
  }
  return *this;
}

TemplateArgument &TemplateArgument::operator=(TemplateArgument &&Other) {
  if (this == &Other) return *this;

  // Destroy current value
  switch (Kind) {
  case TemplateArgumentKind::Integral:
    Data.AsIntegral.~APSInt();
    break;
  default:
    break;
  }

  Kind = Other.Kind;
  IsPackExpansion = Other.IsPackExpansion;

  switch (Kind) {
  case TemplateArgumentKind::Null:
  case TemplateArgumentKind::NullPtr:
  case TemplateArgumentKind::Expression:
    Data.AsExpr = Other.Data.AsExpr;
    break;
  case TemplateArgumentKind::Type:
    Data.AsType = std::move(Other.Data.AsType);
    break;
  case TemplateArgumentKind::Declaration:
    Data.AsDecl = Other.Data.AsDecl;
    break;
  case TemplateArgumentKind::Integral:
    ::new (&Data.AsIntegral) llvm::APSInt(std::move(Other.Data.AsIntegral));
    break;
  case TemplateArgumentKind::Template:
  case TemplateArgumentKind::TemplateExpansion:
    Data.AsTemplate = Other.Data.AsTemplate;
    break;
  case TemplateArgumentKind::Pack:
    PackArgs = Other.PackArgs;
    PackNumArgs = Other.PackNumArgs;
    break;
  }
  return *this;
}

TemplateArgument::~TemplateArgument() {
  switch (Kind) {
  case TemplateArgumentKind::Integral:
    Data.AsIntegral.~APSInt();
    break;
  default:
    break;
  }
}

void TemplateArgument::dump(llvm::raw_ostream &OS) const {
  switch (Kind) {
  case TemplateArgumentKind::Null:
    OS << "<null-template-arg>";
    break;
  case TemplateArgumentKind::Type:
    Data.AsType.dump(OS);
    break;
  case TemplateArgumentKind::Declaration:
    if (Data.AsDecl) {
      OS << "decl:" << Data.AsDecl->getName();
    } else {
      OS << "<null-decl>";
    }
    break;
  case TemplateArgumentKind::NullPtr:
    OS << "nullptr";
    break;
  case TemplateArgumentKind::Integral:
    OS << Data.AsIntegral;
    break;
  case TemplateArgumentKind::Template:
    if (Data.AsTemplate) {
      OS << Data.AsTemplate->getName();
    } else {
      OS << "<null-template>";
    }
    break;
  case TemplateArgumentKind::TemplateExpansion:
    if (Data.AsTemplate) {
      OS << Data.AsTemplate->getName() << "...";
    } else {
      OS << "<null-template-expansion>";
    }
    break;
  case TemplateArgumentKind::Expression:
    if (Data.AsExpr) {
      Data.AsExpr->dump(OS);
    } else {
      OS << "<null-expr>";
    }
    break;
  case TemplateArgumentKind::Pack:
    OS << "<pack:";
    for (unsigned I = 0; I < PackNumArgs; ++I) {
      if (I > 0) OS << ", ";
      PackArgs[I].dump(OS);
    }
    OS << ">";
    break;
  }

  if (IsPackExpansion && Kind != TemplateArgumentKind::Pack &&
      Kind != TemplateArgumentKind::TemplateExpansion) {
    OS << "...";
  }
}

void TemplateArgument::dump() const {
  dump(llvm::outs());
}

void TemplateArgumentLoc::dump(llvm::raw_ostream &OS) const {
  Argument.dump(OS);
}

//===----------------------------------------------------------------------===//
// BuiltinType Implementation
//===----------------------------------------------------------------------===//

bool BuiltinType::isSignedInteger() const {
  switch (Kind) {
  case BuiltinKind::Char:  // Platform-dependent
  case BuiltinKind::SignedChar:
  case BuiltinKind::Short:
  case BuiltinKind::Int:
  case BuiltinKind::Long:
  case BuiltinKind::LongLong:
  case BuiltinKind::Int128:
    return true;
  default:
    return false;
  }
}

bool BuiltinType::isUnsignedInteger() const {
  switch (Kind) {
  case BuiltinKind::UnsignedChar:
  case BuiltinKind::UnsignedShort:
  case BuiltinKind::UnsignedInt:
  case BuiltinKind::UnsignedLong:
  case BuiltinKind::UnsignedLongLong:
  case BuiltinKind::UnsignedInt128:
    return true;
  default:
    return false;
  }
}

bool BuiltinType::isFloatingPoint() const {
  switch (Kind) {
  case BuiltinKind::Float:
  case BuiltinKind::Double:
  case BuiltinKind::LongDouble:
  case BuiltinKind::Float128:
    return true;
  default:
    return false;
  }
}

bool BuiltinType::isInteger() const {
  return isSignedInteger() || isUnsignedInteger() ||
         Kind == BuiltinKind::Bool || Kind == BuiltinKind::Char ||
         Kind == BuiltinKind::WChar || Kind == BuiltinKind::Char16 ||
         Kind == BuiltinKind::Char32 || Kind == BuiltinKind::Char8;
}

const char *BuiltinType::getName() const {
  switch (Kind) {
  case BuiltinKind::Void: return "void";
  case BuiltinKind::Bool: return "bool";
  case BuiltinKind::Char: return "char";
  case BuiltinKind::SignedChar: return "signed char";
  case BuiltinKind::UnsignedChar: return "unsigned char";
  case BuiltinKind::WChar: return "wchar_t";
  case BuiltinKind::Char16: return "char16_t";
  case BuiltinKind::Char32: return "char32_t";
  case BuiltinKind::Char8: return "char8_t";
  case BuiltinKind::Short: return "short";
  case BuiltinKind::Int: return "int";
  case BuiltinKind::Long: return "long";
  case BuiltinKind::LongLong: return "long long";
  case BuiltinKind::Int128: return "__int128";
  case BuiltinKind::UnsignedShort: return "unsigned short";
  case BuiltinKind::UnsignedInt: return "unsigned int";
  case BuiltinKind::UnsignedLong: return "unsigned long";
  case BuiltinKind::UnsignedLongLong: return "unsigned long long";
  case BuiltinKind::UnsignedInt128: return "unsigned __int128";
  case BuiltinKind::Float: return "float";
  case BuiltinKind::Double: return "double";
  case BuiltinKind::LongDouble: return "long double";
  case BuiltinKind::Float128: return "__float128";
  case BuiltinKind::NullPtr: return "std::nullptr_t";
  default: llvm_unreachable("Unknown builtin type kind");
  }
}

void BuiltinType::dump(llvm::raw_ostream &OS) const {
  OS << getName();
}

//===----------------------------------------------------------------------===//
// PointerType Implementation
//===----------------------------------------------------------------------===//

void PointerType::dump(llvm::raw_ostream &OS) const {
  if (Pointee)
    Pointee->dump(OS);
  OS << "*";
}

//===----------------------------------------------------------------------===//
// ReferenceType Implementation
//===----------------------------------------------------------------------===//

void ReferenceType::dump(llvm::raw_ostream &OS) const {
  if (Referenced)
    Referenced->dump(OS);
  OS << (IsLValue ? "&" : "&&");
}

//===----------------------------------------------------------------------===//
// ArrayType Implementation
//===----------------------------------------------------------------------===//

void ArrayType::dump(llvm::raw_ostream &OS) const {
  // Base dump - should not be called directly
  if (ElementType)
    ElementType->dump(OS);
  OS << "[]";
}

void ConstantArrayType::dump(llvm::raw_ostream &OS) const {
  if (ElementType)
    ElementType->dump(OS);
  OS << "[" << SizeValue << "]";
}

void IncompleteArrayType::dump(llvm::raw_ostream &OS) const {
  if (ElementType)
    ElementType->dump(OS);
  OS << "[]";
}

void VariableArrayType::dump(llvm::raw_ostream &OS) const {
  if (ElementType)
    ElementType->dump(OS);
  OS << "[";
  if (SizeExpr)
    SizeExpr->dump(OS);
  else
    OS << "<vla>";
  OS << "]";
}

//===----------------------------------------------------------------------===//
// FunctionType Implementation
//===----------------------------------------------------------------------===//

void FunctionType::dump(llvm::raw_ostream &OS) const {
  if (ReturnType)
    ReturnType->dump(OS);
  OS << " (";
  bool First = true;
  for (const Type *Param : ParamTypes) {
    if (!First)
      OS << ", ";
    First = false;
    if (Param)
      Param->dump(OS);
  }
  if (IsVariadic) {
    if (!First)
      OS << ", ";
    OS << "...";
  }
  OS << ")";
  if (MethodIsConst)
    OS << " const";
  if (MethodIsVolatile)
    OS << " volatile";
}

//===----------------------------------------------------------------------===//
// RecordType Implementation
//===----------------------------------------------------------------------===//

void RecordType::dump(llvm::raw_ostream &OS) const {
  // Print record name from RecordDecl
  if (Decl) {
    llvm::StringRef Name = Decl->getName();
    if (!Name.empty()) {
      OS << Name;
    } else {
      OS << "<anonymous record>";
    }
  } else {
    OS << "<record>";
  }
}

//===----------------------------------------------------------------------===//
// EnumType Implementation
//===----------------------------------------------------------------------===//

void EnumType::dump(llvm::raw_ostream &OS) const {
  // Print enum name from EnumDecl
  if (Decl) {
    llvm::StringRef Name = Decl->getName();
    if (!Name.empty()) {
      OS << Name;
    } else {
      OS << "<anonymous enum>";
    }
  } else {
    OS << "<enum>";
  }
}

//===----------------------------------------------------------------------===//
// TypedefType Implementation
//===----------------------------------------------------------------------===//

void TypedefType::dump(llvm::raw_ostream &OS) const {
  // Print typedef name from TypedefNameDecl
  if (Decl) {
    llvm::StringRef Name = Decl->getName();
    if (!Name.empty()) {
      OS << Name;
    } else {
      OS << "<anonymous typedef>";
    }
  } else {
    OS << "<typedef>";
  }
}

//===----------------------------------------------------------------------===//
// ElaboratedType Implementation
//===----------------------------------------------------------------------===//

void ElaboratedType::dump(llvm::raw_ostream &OS) const {
  if (!Qualifier.empty())
    OS << Qualifier << "::";
  if (NamedType)
    NamedType->dump(OS);
}

//===----------------------------------------------------------------------===//
// TemplateSpecializationType Implementation
//===----------------------------------------------------------------------===//

void TemplateSpecializationType::dump(llvm::raw_ostream &OS) const {
  OS << TemplateName;
  if (!TemplateArgs.empty()) {
    OS << "<";
    bool First = true;
    for (const TemplateArgument &Arg : TemplateArgs) {
      if (!First)
        OS << ", ";
      First = false;
      Arg.dump(OS);
    }
    OS << ">";
  }
}

//===----------------------------------------------------------------------===//
// AutoType Implementation
//===----------------------------------------------------------------------===//

void AutoType::dump(llvm::raw_ostream &OS) const {
  if (IsDeduced) {
    OS << "auto -> ";
    DeducedType.dump(OS);
  } else {
    OS << "auto";
  }
}

//===----------------------------------------------------------------------===//
// DecltypeType Implementation
//===----------------------------------------------------------------------===//

void DecltypeType::dump(llvm::raw_ostream &OS) const {
  OS << "decltype(";
  if (UnderlyingType.isNull()) {
    OS << "<expr>";
  } else {
    UnderlyingType.dump(OS);
  }
  OS << ")";
}

//===----------------------------------------------------------------------===//
// UnresolvedType Implementation
//===----------------------------------------------------------------------===//

void UnresolvedType::dump(llvm::raw_ostream &OS) const {
  OS << Name << " <unresolved>";
}

//===----------------------------------------------------------------------===//
// MemberPointerType Implementation
//===----------------------------------------------------------------------===//

void MemberPointerType::dump(llvm::raw_ostream &OS) const {
  if (PointeeType)
    PointeeType->dump(OS);
  OS << " ";
  if (ClassType)
    ClassType->dump(OS);
  OS << "::*";
}

//===----------------------------------------------------------------------===//
// TemplateTypeParmType Implementation
//===----------------------------------------------------------------------===//

void TemplateTypeParmType::dump(llvm::raw_ostream &OS) const {
  // Print template parameter name or placeholder
  if (Decl) {
    // Print actual parameter name from Decl
    llvm::StringRef Name = Decl->getName();
    if (!Name.empty()) {
      OS << Name;
    } else {
      OS << "T" << Index;
    }
  } else {
    OS << "T" << Index;
  }
  if (IsParameterPack) {
    OS << "...";
  }
}

//===----------------------------------------------------------------------===//
// DependentType Implementation
//===----------------------------------------------------------------------===//

void DependentType::dump(llvm::raw_ostream &OS) const {
  if (BaseType)
    BaseType->dump(OS);
  if (!Name.empty())
    OS << "::" << Name;
  else
    OS << "::<dependent>";
}

//===----------------------------------------------------------------------===//
// Type Implementation
//===----------------------------------------------------------------------===//

bool Type::isDependentType() const {
  switch (getTypeClass()) {
  case TypeClass::TemplateTypeParm:
  case TypeClass::Dependent:
    return true;
  case TypeClass::Pointer:
    return cast<PointerType>(this)->getPointeeType()->isDependentType();
  case TypeClass::LValueReference:
  case TypeClass::RValueReference:
    return cast<ReferenceType>(this)->getReferencedType()->isDependentType();
  case TypeClass::ConstantArray:
  case TypeClass::IncompleteArray:
  case TypeClass::VariableArray:
    return cast<ArrayType>(this)->getElementType()->isDependentType();
  case TypeClass::Function:
    // Check return type and parameter types
    if (cast<FunctionType>(this)->getReturnType()->isDependentType())
      return true;
    for (const Type *Param : cast<FunctionType>(this)->getParamTypes()) {
      if (Param->isDependentType())
        return true;
    }
    return false;
  case TypeClass::MemberPointer: {
    auto *MPT = cast<MemberPointerType>(this);
    return MPT->getClassType()->isDependentType() ||
           MPT->getPointeeType()->isDependentType();
  }
  case TypeClass::TemplateSpecialization: {
    // Template specializations are dependent if any template argument is
    // dependent
    auto *TST = cast<TemplateSpecializationType>(this);
    for (const TemplateArgument &Arg : TST->getTemplateArgs()) {
      if (Arg.isType()) {
        if (Arg.getAsType()->isDependentType()) {
          return true;
        }
      }
      // Template template parameters and non-type template parameters
      // could also be dependent, but we'll handle those cases later
    }
    return false;
  }
  case TypeClass::Decltype: {
    // decltype(expr) is dependent if expr is type-dependent
    auto *DT = cast<DecltypeType>(this);
    if (Expr *E = DT->getExpression()) {
      // Check if the expression is type-dependent
      // First check if the underlying type is set and dependent
      QualType Underlying = DT->getUnderlyingType();
      if (!Underlying.isNull() && Underlying->isDependentType()) {
        return true;
      }
      // Check if the expression itself is type-dependent
      if (E->isTypeDependent()) {
        return true;
      }
    }
    return false;
  }
  default:
    return false;
  }
}

void Type::dump() const {
  dump(llvm::errs());
}

void Type::dump(llvm::raw_ostream &OS) const {
  OS << "Type";
}

//===----------------------------------------------------------------------===//
// QualType Implementation
//===----------------------------------------------------------------------===//

void QualType::dump() const {
  dump(llvm::errs());
}

void QualType::dump(llvm::raw_ostream &OS) const {
  if (!Ty) {
    OS << "<null>";
    return;
  }

  // Print qualifiers before the type
  if (isConstQualified())
    OS << "const ";
  if (isVolatileQualified())
    OS << "volatile ";
  if (isRestrictQualified())
    OS << "restrict ";

  Ty->dump(OS);
}

std::string QualType::getAsString() const {
  std::string Str;
  llvm::raw_string_ostream OS(Str);
  dump(OS);
  return Str;
}

} // namespace blocktype
