//===--- IRMangler.cpp - Backend-Independent Itanium Name Mangling -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/IRMangler.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/IR/TargetLayout.h"

#include "llvm/Support/raw_ostream.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

frontend::IRMangler::IRMangler(const ir::TargetLayout& L) : Layout_(L) {
  (void)Layout_; // Will be used for platform-specific mangling in future phases
}

//===----------------------------------------------------------------------===//
// Primary entry points
//===----------------------------------------------------------------------===//

std::string frontend::IRMangler::mangleFunctionName(const NamedDecl* ND) {
  if (!ND) return "";
  resetSubstitutions();

  // Check most-derived types first (CXXConstructorDecl, CXXDestructorDecl)
  // because FunctionDecl::classof only matches exact FunctionDeclKind,
  // not CXXMethodDeclKind/CXXConstructorDeclKind/CXXDestructorDeclKind.

  // CXXConstructorDecl
  if (auto* Ctor = llvm::dyn_cast<CXXConstructorDecl>(ND)) {
    std::string Name;
    Name += "_ZN";
    mangleNestedName(Ctor->getParent(), Name);
    mangleCtorName(Ctor, Name);
    return Name;
  }

  // CXXDestructorDecl
  if (auto* Dtor = llvm::dyn_cast<CXXDestructorDecl>(ND)) {
    std::string Name;
    Name += "_ZN";
    mangleNestedName(Dtor->getParent(), Name);
    mangleDtorNameInternal(Dtor, Name);
    return Name;
  }

  // CXXMethodDecl (non-ctor/dtor)
  if (auto* MD = llvm::dyn_cast<CXXMethodDecl>(ND)) {
    std::string Name;
    Name += "_ZN";
    mangleNestedName(MD->getParent(), Name);
    mangleSourceName(MD->getName(), Name);
    Name += 'E';
    mangleFunctionParamTypes(MD->getParams(), Name);
    return Name;
  }

  // Plain FunctionDecl (free functions)
  if (auto* FD = llvm::dyn_cast<FunctionDecl>(ND)) {
    std::string Name = "_Z";
    llvm::SmallVector<llvm::StringRef, 4> NsChain;
    llvm::SmallVector<const void*, 4> NsEntityChain;
    DeclContext* Ctx = llvm::cast<DeclContext>(FD)->getParent();
    while (Ctx) {
      if (Ctx->isNamespace()) {
        if (auto* ND2 = Ctx->getOwningDecl()) {
          if (!ND2->getName().empty()) {
            NsChain.push_back(ND2->getName());
            NsEntityChain.push_back(ND2);
          }
        }
      }
      Ctx = Ctx->getParent();
    }

    if (!NsChain.empty()) {
      if (NsChain.size() == 1 && NsChain[0] == "std") {
        Name += "St";
        mangleSourceName(FD->getName(), Name);
      } else {
        Name += 'N';
        for (auto It = NsChain.rbegin(); It != NsChain.rend(); ++It) {
          if (*It == "std") { Name += "St"; continue; }
          auto EntityIt = NsEntityChain.rbegin() + (It - NsChain.rbegin());
          const void* Entity = *EntityIt;
          if (auto Subst = trySubstitution(Entity)) {
            Name += *Subst;
          } else {
            mangleSourceName(*It, Name);
            if (shouldAddSubstitution(*It)) addSubstitution(Entity);
          }
        }
        mangleSourceName(FD->getName(), Name);
        Name += 'E';
      }
    } else {
      mangleSourceName(FD->getName(), Name);
    }

    mangleFunctionParamTypes(FD->getParams(), Name);
    return Name;
  }

  // VarDecl — no mangling (C-linkage style)
  if (auto* VD = llvm::dyn_cast<VarDecl>(ND)) {
    return VD->getName().str();
  }

  return ND->getName().str();
}

std::string frontend::IRMangler::mangleVTable(const CXXRecordDecl* RD) {
  if (!RD) return "_ZTV";
  resetSubstitutions();

  std::string Name = "_ZTV";
  if (RD->getParent() || hasNamespaceParent(RD)) {
    Name += 'N';
    mangleNestedName(RD, Name);
    Name += 'E';
  } else {
    mangleSourceName(RD->getName(), Name);
  }
  return Name;
}

std::string frontend::IRMangler::mangleTypeInfo(const CXXRecordDecl* RD) {
  if (!RD) return "_ZTI";
  resetSubstitutions();

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

std::string frontend::IRMangler::mangleThunk(const CXXMethodDecl* MD) {
  if (!MD) return "_ZThn0_";
  // Non-virtual thunk: _ZThn<offset>_<mangled-name>
  // Simplified: offset = 0 for now (B.9/B.10 will compute actual offsets)
  std::string Name = "_ZThn0_";
  Name += mangleFunctionName(MD);
  return Name;
}

std::string frontend::IRMangler::mangleGuardVariable(const VarDecl* VD) {
  if (!VD) return "_ZGV";
  // _ZGV + mangled name of the variable
  std::string Name = "_ZGV";
  Name += mangleFunctionName(VD);
  return Name;
}

std::string frontend::IRMangler::mangleStringLiteral(const StringLiteral* SL) {
  if (!SL) return "_ZL";
  // _ZL + <length> + <hex-encoded content>
  llvm::StringRef Val = SL->getValue();
  std::string Name = "_ZL";
  // Encode length as decimal
  llvm::SmallVector<char, 16> LenStr;
  llvm::raw_svector_ostream OS(LenStr);
  OS << Val.size();
  Name.append(LenStr.begin(), LenStr.end());
  // Hex-encode content
  for (char C : Val) {
    char Hex[3];
    unsigned char UC = static_cast<unsigned char>(C);
    Hex[0] = "0123456789abcdef"[UC >> 4];
    Hex[1] = "0123456789abcdef"[UC & 0xf];
    Hex[2] = '\0';
    Name += Hex;
  }
  return Name;
}

//===----------------------------------------------------------------------===//
// Auxiliary entry points
//===----------------------------------------------------------------------===//

std::string frontend::IRMangler::mangleTypeInfoName(const CXXRecordDecl* RD) {
  if (!RD) return "_ZTS";
  resetSubstitutions();

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

std::string frontend::IRMangler::mangleDtorName(const CXXRecordDecl* RD,
                                                 DtorVariant Variant) {
  if (!RD) return "_ZN5dummyD0Ev";
  resetSubstitutions();
  std::string Name = "_ZN";
  mangleNestedName(RD, Name);
  switch (Variant) {
  case DtorVariant::Deleting: Name += "D0"; break;
  case DtorVariant::Complete: Name += "D1"; break;
  }
  Name += 'E';
  Name += 'v'; // destructors have no parameters
  return Name;
}

//===----------------------------------------------------------------------===//
// Type mangling
//===----------------------------------------------------------------------===//

std::string frontend::IRMangler::mangleType(QualType T) {
  std::string Out;
  mangleQualType(T, Out);
  return Out;
}

std::string frontend::IRMangler::mangleType(const Type* T) {
  std::string Out;
  mangleQualType(QualType(T, Qualifier::None), Out);
  return Out;
}

//===----------------------------------------------------------------------===//
// Internal encoding methods
//===----------------------------------------------------------------------===//

void frontend::IRMangler::mangleBuiltinType(const BuiltinType* T,
                                             std::string& Out) {
  switch (T->getKind()) {
  case BuiltinKind::Void:           Out += 'v'; return;
  case BuiltinKind::Bool:           Out += 'b'; return;
  case BuiltinKind::Char:           Out += 'c'; return;
  case BuiltinKind::SignedChar:     Out += 'a'; return;
  case BuiltinKind::UnsignedChar:   Out += 'h'; return;
  case BuiltinKind::WChar:          Out += 'w'; return;
  case BuiltinKind::Char16:         Out += "Ds"; return;
  case BuiltinKind::Char32:         Out += "Di"; return;
  case BuiltinKind::Char8:          Out += "Du"; return;
  case BuiltinKind::Short:          Out += 's'; return;
  case BuiltinKind::Int:            Out += 'i'; return;
  case BuiltinKind::Long:           Out += 'l'; return;
  case BuiltinKind::LongLong:       Out += 'x'; return;
  case BuiltinKind::Int128:         Out += 'n'; return;
  case BuiltinKind::UnsignedShort:     Out += 't'; return;
  case BuiltinKind::UnsignedInt:       Out += 'j'; return;
  case BuiltinKind::UnsignedLong:      Out += 'm'; return;
  case BuiltinKind::UnsignedLongLong:  Out += 'y'; return;
  case BuiltinKind::UnsignedInt128:    Out += 'o'; return;
  case BuiltinKind::Float:          Out += 'f'; return;
  case BuiltinKind::Double:         Out += 'd'; return;
  case BuiltinKind::LongDouble:     Out += 'e'; return;
  case BuiltinKind::Float128:       Out += 'g'; return;
  case BuiltinKind::NullPtr:        Out += "Dn"; return;
  default:
    Out += 'u';
    mangleSourceName(T->getName(), Out);
    return;
  }
}

void frontend::IRMangler::manglePointerType(const PointerType* T,
                                             std::string& Out) {
  Out += 'P';
  mangleQualType(QualType(T->getPointeeType(), Qualifier::None), Out);
}

void frontend::IRMangler::mangleReferenceType(const ReferenceType* T,
                                               std::string& Out) {
  if (T->isLValueReference()) {
    Out += 'R';
  } else {
    Out += 'O';
  }
  mangleQualType(QualType(T->getReferencedType(), Qualifier::None), Out);
}

void frontend::IRMangler::mangleArrayType(const ArrayType* T,
                                           std::string& Out) {
  if (auto* CAT = llvm::dyn_cast<ConstantArrayType>(T)) {
    Out += 'A';
    llvm::SmallVector<char, 16> SizeStr;
    llvm::raw_svector_ostream OS(SizeStr);
    OS << CAT->getSize();
    Out.append(SizeStr.begin(), SizeStr.end());
    Out += '_';
    mangleQualType(QualType(CAT->getElementType(), Qualifier::None), Out);
  } else if (auto* IAT = llvm::dyn_cast<IncompleteArrayType>(T)) {
    Out += "A_";
    mangleQualType(QualType(IAT->getElementType(), Qualifier::None), Out);
  } else {
    Out += "A_";
    mangleQualType(QualType(T->getElementType(), Qualifier::None), Out);
  }
}

void frontend::IRMangler::mangleFunctionType(const FunctionType* T,
                                              std::string& Out) {
  Out += 'F';
  mangleQualType(QualType(T->getReturnType(), Qualifier::None), Out);
  for (const Type* ParamTy : T->getParamTypes()) {
    mangleQualType(QualType(ParamTy, Qualifier::None), Out);
  }
  if (T->isVariadic()) {
    Out += 'z';
  }
  Out += 'E';
}

void frontend::IRMangler::mangleRecordType(const RecordType* T,
                                            std::string& Out) {
  const void* Entity = T->getDecl();
  llvm::StringRef Name = T->getDecl()->getName();
  if (auto Subst = trySubstitution(Entity)) {
    Out += *Subst;
    return;
  }
  mangleSourceName(Name, Out);
  if (shouldAddSubstitution(Name)) addSubstitution(Entity);
}

void frontend::IRMangler::mangleEnumType(const EnumType* T,
                                          std::string& Out) {
  const void* Entity = T->getDecl();
  llvm::StringRef Name = T->getDecl()->getName();
  if (auto Subst = trySubstitution(Entity)) {
    Out += *Subst;
    return;
  }
  mangleSourceName(Name, Out);
  if (shouldAddSubstitution(Name)) addSubstitution(Entity);
}

void frontend::IRMangler::mangleTypedefType(const TypedefType* T,
                                             std::string& Out) {
  QualType Underlying = T->getDecl()->getUnderlyingType();
  mangleQualType(Underlying, Out);
}

void frontend::IRMangler::mangleElaboratedType(const ElaboratedType* T,
                                                std::string& Out) {
  mangleQualType(QualType(T->getNamedType(), Qualifier::None), Out);
}

void frontend::IRMangler::mangleTemplateSpecializationType(
    const TemplateSpecializationType* T, std::string& Out) {
  const void* TemplateEntity = T->getTemplateDecl()
                                    ? static_cast<const void*>(T->getTemplateDecl())
                                    : static_cast<const void*>(T);
  llvm::StringRef TemplateName = T->getTemplateName();
  if (auto Subst = trySubstitution(TemplateEntity)) {
    Out += *Subst;
  } else {
    mangleSourceName(TemplateName, Out);
    if (shouldAddSubstitution(TemplateName)) addSubstitution(TemplateEntity);
  }
  Out += 'I';
  for (const auto& Arg : T->getTemplateArgs()) {
    if (Arg.isType()) {
      mangleQualType(Arg.getAsType(), Out);
    } else if (Arg.isIntegral()) {
      Out += 'L';
      const llvm::APSInt& Val = Arg.getAsIntegral();
      if (Val.isSigned() && Val.isNegative()) {
        Out += 'n';
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
  }
  Out += 'E';
  addSubstitution(T);
}

void frontend::IRMangler::mangleQualType(QualType QT, std::string& Out) {
  if (QT.isNull()) {
    Out += 'v';
    return;
  }

  if (QT.isConstQualified()) {
    Out += 'K';
  }
  if (QT.isVolatileQualified()) {
    Out += 'V';
  }

  const Type* T = QT.getTypePtr();
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
    auto* MPT = llvm::cast<MemberPointerType>(T);
    Out += 'M';
    mangleQualType(QualType(MPT->getClassType(), Qualifier::None), Out);
    Out += '_';
    mangleQualType(QualType(MPT->getPointeeType(), Qualifier::None), Out);
    break;
  }
  case TypeClass::Auto: {
    auto* AT = llvm::cast<AutoType>(T);
    if (AT->isDeduced()) {
      mangleQualType(AT->getDeducedType(), Out);
    } else {
      Out += "Da";
    }
    break;
  }
  case TypeClass::Decltype: {
    auto* DT = llvm::cast<DecltypeType>(T);
    if (!DT->getUnderlyingType().isNull()) {
      mangleQualType(DT->getUnderlyingType(), Out);
    } else {
      Out += "Dn";
    }
    break;
  }
  case TypeClass::TemplateTypeParm: {
    auto* TTP = llvm::cast<TemplateTypeParmType>(T);
    llvm::StringRef ParamName;
    const void* Entity = nullptr;
    if (TTP->getDecl()) {
      ParamName = TTP->getDecl()->getName();
      Entity = TTP->getDecl();
    }
    if (ParamName.empty()) ParamName = "tparam";
    if (!Entity) Entity = TTP;
    if (auto Subst = trySubstitution(Entity)) {
      Out += *Subst;
    } else {
      mangleSourceName(ParamName, Out);
      if (shouldAddSubstitution(ParamName)) addSubstitution(Entity);
    }
    break;
  }
  case TypeClass::Dependent: {
    Out += 'u';
    auto* DT = llvm::cast<DependentType>(T);
    mangleSourceName(DT->getName(), Out);
    break;
  }
  case TypeClass::Unresolved: {
    auto* UT = llvm::cast<UnresolvedType>(T);
    mangleSourceName(UT->getName(), Out);
    break;
  }
  default:
    Out += 'v';
    break;
  }
}

void frontend::IRMangler::mangleFunctionParamTypes(
    llvm::ArrayRef<ParmVarDecl*> Params, std::string& Out) {
  if (Params.empty()) {
    Out += 'v';
    return;
  }
  for (const ParmVarDecl* PVD : Params) {
    mangleQualType(PVD->getType(), Out);
  }
}

void frontend::IRMangler::mangleNestedName(const CXXRecordDecl* RD,
                                            std::string& Out) {
  if (!RD) return;

  llvm::SmallVector<llvm::StringRef, 8> NameChain;
  llvm::SmallVector<const void*, 8> EntityChain;

  NameChain.push_back(RD->getName());
  EntityChain.push_back(RD);

  for (DeclContext* ParentCtx = RD->getDeclContext()->getParent();
       ParentCtx; ParentCtx = ParentCtx->getParent()) {
    if (ParentCtx->isCXXRecord()) {
      if (auto* ND = ParentCtx->getOwningDecl()) {
        NameChain.push_back(ND->getName());
        EntityChain.push_back(ND);
      }
    }
  }

  DeclContext* Ctx = RD->getDeclContext()->getParent();
  while (Ctx) {
    if (Ctx->isNamespace()) {
      if (auto* ND = Ctx->getOwningDecl()) {
        if (!ND->getName().empty()) {
          NameChain.push_back(ND->getName());
          EntityChain.push_back(ND);
        }
      }
    }
    Ctx = Ctx->getParent();
  }

  for (auto It = NameChain.rbegin(); It != NameChain.rend(); ++It) {
    if (*It == "std") { Out += "St"; continue; }
    auto EntityIt = EntityChain.rbegin() + (It - NameChain.rbegin());
    const void* Entity = *EntityIt;
    if (auto Subst = trySubstitution(Entity)) {
      Out += *Subst;
    } else {
      mangleSourceName(*It, Out);
      if (shouldAddSubstitution(*It)) addSubstitution(Entity);
    }
  }
}

bool frontend::IRMangler::hasNamespaceParent(const CXXRecordDecl* RD) {
  if (!RD) return false;
  DeclContext* Ctx = RD->getDeclContext()->getParent();
  while (Ctx) {
    if (Ctx->isNamespace()) {
      if (auto* ND = Ctx->getOwningDecl()) {
        if (!ND->getName().empty())
          return true;
      }
    }
    Ctx = Ctx->getParent();
  }
  return false;
}

void frontend::IRMangler::mangleSourceName(llvm::StringRef Name,
                                            std::string& Out) {
  Out += std::to_string(Name.size());
  Out += Name;
}

void frontend::IRMangler::mangleCtorName(const CXXConstructorDecl* Ctor,
                                          std::string& Out) {
  Out += "C1";
  Out += 'E';
  mangleFunctionParamTypes(Ctor->getParams(), Out);
}

void frontend::IRMangler::mangleDtorNameInternal(
    const CXXDestructorDecl* Dtor, std::string& Out, DtorVariant Variant) {
  switch (Variant) {
  case DtorVariant::Deleting: Out += "D0"; break;
  case DtorVariant::Complete: Out += "D1"; break;
  }
  Out += 'E';
  mangleFunctionParamTypes(Dtor->getParams(), Out);
}
