//===--- Mangler.cpp - Itanium C++ ABI Name Mangling --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements Itanium C++ ABI name mangling for the BlockType compiler.
//
// Produces mangled names following the Itanium C++ ABI specification:
//   _Z3fooi         -> foo(int)
//   _Z3fooPKc       -> foo(char const*)
//   _ZN3Foo3barEi   -> Foo::bar(int)
//   _ZN3FooC1Ei     -> Foo::Foo(int) [complete constructor]
//   _ZN3FooD1Ev     -> Foo::~Foo() [complete destructor]
//   _ZTV3Foo        -> vtable for Foo
//   _ZTI3Foo        -> typeinfo for Foo
//   _ZTS3Foo        -> typeinfo name for Foo
//
//===----------------------------------------------------------------------===//

#include "blocktype/CodeGen/Mangler.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Type.h"

#include "llvm/Support/raw_ostream.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// 主要入口
//===----------------------------------------------------------------------===//

std::string Mangler::getMangledName(const FunctionDecl *FD) {
  if (!FD) return "";

  // CXXConstructorDecl
  if (auto *Ctor = llvm::dyn_cast<CXXConstructorDecl>(FD)) {
    std::string Name;
    Name += "_ZN";
    mangleNestedName(Ctor->getParent(), Name);
    mangleCtorName(Ctor, Name);
    return Name;
  }

  // CXXDestructorDecl
  if (auto *Dtor = llvm::dyn_cast<CXXDestructorDecl>(FD)) {
    std::string Name;
    Name += "_ZN";
    mangleNestedName(Dtor->getParent(), Name);
    mangleDtorName(Dtor, Name);
    return Name;
  }

  // CXXMethodDecl（非构造/析构）
  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(FD)) {
    std::string Name;
    Name += "_ZN";
    mangleNestedName(MD->getParent(), Name);
    mangleSourceName(MD->getName(), Name);
    // E 结束 nested-name，参数类型编码在 E 之后
    Name += 'E';
    mangleFunctionParamTypes(MD->getParams(), Name);
    return Name;
  }

  // 普通自由函数: 需要检查是否在命名空间内
  // _Z<name-len><name><param-types>           (全局作用域)
  // _ZN<namespace><name-len><name>E<params>   (命名空间内)
  {
    std::string Name = "_Z";

    // Collect namespace chain from FunctionDecl's DeclContext parent chain
    llvm::SmallVector<llvm::StringRef, 4> NsChain;
    DeclContext *Ctx = FD->getDeclContext()->getParent();
    while (Ctx) {
      if (Ctx->isNamespace()) {
        if (auto *ND = Ctx->getOwningDecl()) {
          if (!ND->getName().empty()) {
            NsChain.push_back(ND->getName());
          }
        }
      }
      Ctx = Ctx->getParent();
    }

    if (!NsChain.empty()) {
      // Function is inside namespace(s): use nested-name form
      Name += 'N';
      // Emit namespaces from outermost to innermost
      for (auto It = NsChain.rbegin(); It != NsChain.rend(); ++It) {
        mangleSourceName(*It, Name);
      }
      mangleSourceName(FD->getName(), Name);
      Name += 'E';
    } else {
      // Top-level function: simple source-name form
      mangleSourceName(FD->getName(), Name);
    }

    mangleFunctionParamTypes(FD->getParams(), Name);
    return Name;
  }
}

std::string Mangler::getMangledName(const VarDecl *VD) {
  if (!VD) return "";

  // 全局变量一般不 mangling（C-linkage 风格）。
  // 如果变量在命名空间内或有 C++ 限定，需要 mangling。
  // 当前 AST 不支持变量的父作用域，直接返回原始名称。
  return VD->getName().str();
}

std::string Mangler::getVTableName(const CXXRecordDecl *RD) {
  if (!RD) return "_ZTV";

  // Itanium C++ ABI: vtable name = _ZTV + <mangled-name>
  // For a top-level (non-nested, no namespace) class: _ZTV3Foo
  // For a nested class or class in namespace: _ZTVN3foo3BarE
  std::string Name = "_ZTV";
  if (RD->getParent() || hasNamespaceParent(RD)) {
    // Nested class or class in namespace: use N...E wrapper
    Name += 'N';
    mangleNestedName(RD, Name);
    Name += 'E';
  } else {
    // Top-level class: just the source name
    mangleSourceName(RD->getName(), Name);
  }
  return Name;
}

std::string Mangler::getRTTIName(const CXXRecordDecl *RD) {
  if (!RD) return "_ZTI";

  // Itanium C++ ABI: typeinfo name = _ZTI + <mangled-name>
  // Same nested/top-level distinction as vtable.
  std::string Name = "_ZTI";
  if (RD->getParent() || hasNamespaceParent(RD)) {
    Name += 'N';
    mangleNestedName(RD, Name);
    Name += 'E';
  } else {
    mangleSourceName(RD->getName(), Name);
  }
  return Name;
}

std::string Mangler::getTypeinfoName(const CXXRecordDecl *RD) {
  if (!RD) return "_ZTS";

  // Itanium C++ ABI: typeinfo string name = _ZTS + <mangled-name>
  // Same nested/top-level distinction as vtable.
  std::string Name = "_ZTS";
  if (RD->getParent() || hasNamespaceParent(RD)) {
    Name += 'N';
    mangleNestedName(RD, Name);
    Name += 'E';
  } else {
    mangleSourceName(RD->getName(), Name);
  }
  return Name;
}

//===----------------------------------------------------------------------===//
// Type mangling
//===----------------------------------------------------------------------===//

std::string Mangler::mangleType(QualType T) {
  std::string Out;
  mangleQualType(T, Out);
  return Out;
}

std::string Mangler::mangleType(const Type *T) {
  std::string Out;
  mangleQualType(QualType(T, Qualifier::None), Out);
  return Out;
}

//===----------------------------------------------------------------------===//
// 内部编码方法
//===----------------------------------------------------------------------===//

void Mangler::mangleBuiltinType(const BuiltinType *T, std::string &Out) {
  switch (T->getKind()) {
  // void
  case BuiltinKind::Void:      Out += 'v'; return;
  // bool
  case BuiltinKind::Bool:      Out += 'b'; return;
  // char types
  case BuiltinKind::Char:      Out += 'c'; return;
  case BuiltinKind::SignedChar:Out += 'a'; return;
  case BuiltinKind::UnsignedChar: Out += 'h'; return;
  case BuiltinKind::WChar:     Out += 'w'; return;
  case BuiltinKind::Char16:    Out += "Ds"; return;
  case BuiltinKind::Char32:    Out += "Di"; return;
  case BuiltinKind::Char8:     Out += "Du"; return;
  // signed integer types
  case BuiltinKind::Short:     Out += 's'; return;
  case BuiltinKind::Int:       Out += 'i'; return;
  case BuiltinKind::Long:      Out += 'l'; return;
  case BuiltinKind::LongLong:  Out += 'x'; return;
  case BuiltinKind::Int128:    Out += 'n'; return;
  // unsigned integer types
  case BuiltinKind::UnsignedShort:    Out += 't'; return;
  case BuiltinKind::UnsignedInt:      Out += 'j'; return;
  case BuiltinKind::UnsignedLong:     Out += 'm'; return;
  case BuiltinKind::UnsignedLongLong: Out += 'y'; return;
  case BuiltinKind::UnsignedInt128:   Out += 'o'; return;
  // floating point types
  case BuiltinKind::Float:     Out += 'f'; return;
  case BuiltinKind::Double:    Out += 'd'; return;
  case BuiltinKind::LongDouble:Out += 'e'; return;
  case BuiltinKind::Float128:  Out += 'g'; return;
  // nullptr_t
  case BuiltinKind::NullPtr:   Out += "Dn"; return;
  default:
    // Fallback: 使用 u<length><name> vendor extension
    Out += 'u';
    mangleSourceName(T->getName(), Out);
    return;
  }
}

void Mangler::manglePointerType(const PointerType *T, std::string &Out) {
  Out += 'P';
  mangleQualType(QualType(T->getPointeeType(), Qualifier::None), Out);
}

void Mangler::mangleReferenceType(const ReferenceType *T, std::string &Out) {
  if (T->isLValueReference()) {
    Out += 'R';
  } else {
    Out += 'O';
  }
  mangleQualType(QualType(T->getReferencedType(), Qualifier::None), Out);
}

void Mangler::mangleArrayType(const ArrayType *T, std::string &Out) {
  if (auto *CAT = llvm::dyn_cast<ConstantArrayType>(T)) {
    Out += 'A';
    // 编码数组大小（十进制）
    llvm::SmallVector<char, 16> SizeStr;
    llvm::raw_svector_ostream OS(SizeStr);
    OS << CAT->getSize();
    Out.append(SizeStr.begin(), SizeStr.end());
    Out += '_';
    mangleQualType(QualType(CAT->getElementType(), Qualifier::None), Out);
  } else if (auto *IAT = llvm::dyn_cast<IncompleteArrayType>(T)) {
    Out += "A_";
    mangleQualType(QualType(IAT->getElementType(), Qualifier::None), Out);
  } else {
    // VariableArrayType — fallback
    Out += "A_";
    mangleQualType(QualType(T->getElementType(), Qualifier::None), Out);
  }
}

void Mangler::mangleFunctionType(const FunctionType *T, std::string &Out) {
  Out += 'F';
  // 返回类型
  mangleQualType(QualType(T->getReturnType(), Qualifier::None), Out);
  // 参数类型
  for (const Type *ParamTy : T->getParamTypes()) {
    mangleQualType(QualType(ParamTy, Qualifier::None), Out);
  }
  if (T->isVariadic()) {
    Out += 'z';
  }
  Out += 'E';
}

void Mangler::mangleRecordType(const RecordType *T, std::string &Out) {
  // source-name: <length><name>
  mangleSourceName(T->getDecl()->getName(), Out);
}

void Mangler::mangleEnumType(const EnumType *T, std::string &Out) {
  mangleSourceName(T->getDecl()->getName(), Out);
}

void Mangler::mangleTypedefType(const TypedefType *T, std::string &Out) {
  // 对于 typedef，mangling 可以用原始名称或底层类型。
  // Itanium ABI 标准做法: 如果 typedef 是 template 参数则编码为特定形式。
  // 简化处理：直接编码底层类型。
  // 但如果需要保留 typedef 名称，可以用 source-name。
  // 这里按 Clang 行为，对非模板 typedef 直接使用底层类型。
  QualType Underlying = T->getDecl()->getUnderlyingType();
  mangleQualType(Underlying, Out);
}

void Mangler::mangleElaboratedType(const ElaboratedType *T, std::string &Out) {
  // Elaborated type 只是包装器，编码底层命名类型
  mangleQualType(QualType(T->getNamedType(), Qualifier::None), Out);
}

void Mangler::mangleTemplateSpecializationType(
    const TemplateSpecializationType *T, std::string &Out) {
  // 编码为 N<template-name>I<args>E
  Out += 'N';
  mangleSourceName(T->getTemplateName(), Out);
  Out += 'I';
  for (const auto &Arg : T->getTemplateArgs()) {
    if (Arg.isType()) {
      mangleQualType(Arg.getAsType(), Out);
    } else if (Arg.isIntegral()) {
      // integral template argument: L<type><value>E
      Out += 'L';
      // encode type
      const llvm::APSInt &Val = Arg.getAsIntegral();
      if (Val.isSigned() && Val.isNegative()) {
        Out += 'n'; // negative sign
        llvm::SmallVector<char, 16> ValStr;
        llvm::raw_svector_ostream OS(ValStr);
        OS << (-Val);
        Out.append(ValStr.begin(), ValStr.end());
      } else {
        llvm::SmallVector<char, 16> ValStr;
        llvm::raw_svector_ostream OS(ValStr);
        OS << Val;
        Out.append(ValStr.begin(), ValStr.end());
      }
      Out += 'E';
    }
    // 其他 template argument 类型暂不处理
  }
  Out += 'E';
}

void Mangler::mangleQualType(QualType QT, std::string &Out) {
  if (QT.isNull()) {
    Out += 'v'; // void fallback
    return;
  }

  // 处理 CVR 限定符
  // Itanium ABI: const = K, volatile = V，限定符跟在类型之后
  // 但在参数位置时，限定符在类型之前
  if (QT.isConstQualified()) {
    Out += 'K';
  }
  if (QT.isVolatileQualified()) {
    Out += 'V';
  }

  const Type *T = QT.getTypePtr();
  if (!T) {
    Out += 'v';
    return;
  }

  switch (T->getTypeClass()) {
  case TypeClass::Builtin:
    mangleBuiltinType(llvm::cast<BuiltinType>(T), Out);
    break;
  case TypeClass::Pointer:
    manglePointerType(llvm::cast<PointerType>(T), Out);
    break;
  case TypeClass::LValueReference:
  case TypeClass::RValueReference:
    mangleReferenceType(llvm::cast<ReferenceType>(T), Out);
    break;
  case TypeClass::ConstantArray:
  case TypeClass::IncompleteArray:
  case TypeClass::VariableArray:
    mangleArrayType(llvm::cast<ArrayType>(T), Out);
    break;
  case TypeClass::Function:
    mangleFunctionType(llvm::cast<FunctionType>(T), Out);
    break;
  case TypeClass::Record:
    mangleRecordType(llvm::cast<RecordType>(T), Out);
    break;
  case TypeClass::Enum:
    mangleEnumType(llvm::cast<EnumType>(T), Out);
    break;
  case TypeClass::Typedef:
    mangleTypedefType(llvm::cast<TypedefType>(T), Out);
    break;
  case TypeClass::Elaborated:
    mangleElaboratedType(llvm::cast<ElaboratedType>(T), Out);
    break;
  case TypeClass::TemplateSpecialization:
    mangleTemplateSpecializationType(
        llvm::cast<TemplateSpecializationType>(T), Out);
    break;
  case TypeClass::MemberPointer: {
    // M<T::type>
    auto *MPT = llvm::cast<MemberPointerType>(T);
    Out += 'M';
    mangleQualType(QualType(MPT->getClassType(), Qualifier::None), Out);
    Out += '_'; // separator (simplified, not fully Itanium compliant)
    mangleQualType(QualType(MPT->getPointeeType(), Qualifier::None), Out);
    break;
  }
  case TypeClass::Auto: {
    // auto / decltype(auto)
    auto *AT = llvm::cast<AutoType>(T);
    if (AT->isDeduced()) {
      mangleQualType(AT->getDeducedType(), Out);
    } else {
      Out += "Da"; // auto
    }
    break;
  }
  case TypeClass::Decltype: {
    auto *DT = llvm::cast<DecltypeType>(T);
    if (!DT->getUnderlyingType().isNull()) {
      mangleQualType(DT->getUnderlyingType(), Out);
    } else {
      Out += "Dn"; // fallback
    }
    break;
  }
  case TypeClass::TemplateTypeParm: {
    // Template type parameter — encode as substitution or vendor extension
    Out += 'u';
    Out += "tparam"; // simplified vendor extension
    break;
  }
  case TypeClass::Dependent: {
    // Dependent type — vendor extension
    Out += 'u';
    auto *DT = llvm::cast<DependentType>(T);
    mangleSourceName(DT->getName(), Out);
    break;
  }
  case TypeClass::Unresolved: {
    auto *UT = llvm::cast<UnresolvedType>(T);
    mangleSourceName(UT->getName(), Out);
    break;
  }
  default:
    // Unknown type — fallback
    Out += 'v';
    break;
  }
}

void Mangler::mangleFunctionParamTypes(
    llvm::ArrayRef<ParmVarDecl *> Params, std::string &Out) {
  if (Params.empty()) {
    // Itanium ABI: 空参数列表编码为 'v'（void）
    Out += 'v';
    return;
  }
  for (const ParmVarDecl *PVD : Params) {
    mangleQualType(PVD->getType(), Out);
  }
}

void Mangler::mangleNestedName(const CXXRecordDecl *RD,
                                std::string &Out) {
  if (!RD) return;

  // Itanium C++ ABI: nested-name-specifier encodes the full qualification
  // from outermost namespace/class to innermost.
  //   ns::Outer::Inner  →  2ns5Outer5Inner
  //
  // We build the parent chain from innermost to outermost, then emit
  // in reverse order (outermost first).

  llvm::SmallVector<llvm::StringRef, 8> NameChain;

  // First, add the record's own name.
  NameChain.push_back(RD->getName());

  // Walk up the parent chain: nested classes and namespaces.
  for (DeclContext *ParentCtx = RD->getDeclContext()->getParent();
       ParentCtx; ParentCtx = ParentCtx->getParent()) {
    if (ParentCtx->isCXXRecord()) {
      if (auto *ND = ParentCtx->getOwningDecl()) {
        NameChain.push_back(ND->getName());
      }
    }
  }

  // Check if the class's DeclContext has namespace parents.
  // CXXRecordDecl inherits from DeclContext, so we can walk the
  // DeclContext parent chain to find enclosing namespaces.
  DeclContext *Ctx = RD->getDeclContext()->getParent();
  while (Ctx) {
    if (Ctx->isNamespace()) {
      if (auto *ND = Ctx->getOwningDecl()) {
        if (!ND->getName().empty()) {
          NameChain.push_back(ND->getName());
        }
      }
    }
    Ctx = Ctx->getParent();
  }

  // Emit names from outermost to innermost (reverse the chain)
  for (auto It = NameChain.rbegin(); It != NameChain.rend(); ++It) {
    mangleSourceName(*It, Out);
  }
}

bool Mangler::hasNamespaceParent(const CXXRecordDecl *RD) {
  if (!RD) return false;
  DeclContext *Ctx = RD->getDeclContext()->getParent();
  while (Ctx) {
    if (Ctx->isNamespace()) {
      if (auto *ND = Ctx->getOwningDecl()) {
        if (!ND->getName().empty())
          return true;
      }
    }
    Ctx = Ctx->getParent();
  }
  return false;
}

void Mangler::mangleSourceName(llvm::StringRef Name, std::string &Out) {
  // source-name ::= <positive length number> <identifier>
  Out += std::to_string(Name.size());
  Out += Name;
}

void Mangler::mangleCtorName(const CXXConstructorDecl *Ctor,
                              std::string &Out) {
  // Constructor ::= C1  (complete object constructor)
  //              ::= C2  (base object constructor)
  //              ::= C3  (allocating constructor)
  // 使用 C1（完整对象构造函数）作为默认
  Out += "C1";
  Out += 'E';
  // 参数类型（不含 this）
  mangleFunctionParamTypes(Ctor->getParams(), Out);
}

void Mangler::mangleDtorName(const CXXDestructorDecl *Dtor,
                              std::string &Out,
                              DtorVariant Variant) {
  // Destructor ::= D0  (deleting destructor)
  //            ::= D1  (complete object destructor)
  //            ::= D2  (base object destructor)
  switch (Variant) {
  case DtorVariant::Deleting: Out += "D0"; break;
  case DtorVariant::Complete: Out += "D1"; break;
  }
  Out += 'E';
  // 析构函数通常无参数
  mangleFunctionParamTypes(Dtor->getParams(), Out);
}

std::string Mangler::getMangledDtorName(const CXXRecordDecl *RD,
                                         DtorVariant Variant) {
  if (!RD) return "_ZN5dummyD0Ev";
  std::string Name = "_ZN";
  mangleNestedName(RD, Name);
  switch (Variant) {
  case DtorVariant::Deleting: Name += "D0"; break;
  case DtorVariant::Complete: Name += "D1"; break;
  }
  Name += 'E';
  Name += 'v'; // 析构函数无参数
  return Name;
}
