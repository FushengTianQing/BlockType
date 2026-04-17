//===--- TemplateInstantiation.cpp - Template Instantiation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/Casting.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// TemplateArgumentList
//===----------------------------------------------------------------------===//

llvm::ArrayRef<TemplateArgument>
TemplateArgumentList::getPackArgument() const {
  for (const auto &Arg : Args) {
    if (Arg.isPack())
      return Arg.getAsPack();
  }
  return {};
}

//===----------------------------------------------------------------------===//
// TemplateInstantiator
//===----------------------------------------------------------------------===//

TemplateInstantiator::TemplateInstantiator(Sema &S)
    : SemaRef(S), Context(S.getASTContext()) {}

//===----------------------------------------------------------------------===//
// Class Template Instantiation
//===----------------------------------------------------------------------===//

ClassTemplateSpecializationDecl *
TemplateInstantiator::InstantiateClassTemplate(
    ClassTemplateDecl *Template, llvm::ArrayRef<TemplateArgument> Args) {
  if (!Template)
    return nullptr;

  // 1. Check cache for existing specialization
  if (auto *Spec = FindExistingSpecialization(Template, Args))
    return Spec;

  // 2. Check recursion depth
  if (CurrentDepth >= MaxInstantiationDepth) {
    if (!isSFINAEContext())
      SemaRef.getDiagnostics().report(Template->getLocation(),
                                      DiagID::err_template_recursion);
    return nullptr;
  }

  // 3. Create new specialization decl
  auto *Spec = Context.create<ClassTemplateSpecializationDecl>(
      Template->getLocation(), Template->getName(), Template, Args);

  // 4. Get the pattern (templated CXXRecordDecl)
  CXXRecordDecl *Pattern =
      llvm::dyn_cast_or_null<CXXRecordDecl>(Template->getTemplatedDecl());
  if (!Pattern)
    return Spec;

  // Mark as complete definition
  Spec->setCompleteDefinition(true);

  // 5. Build argument mapping
  TemplateArgumentList ArgList(Args);
  ++CurrentDepth;

  // 6. Iterate pattern members, substitute template parameters
  for (Decl *M : Pattern->members()) {
    Decl *InstM = SubstituteDecl(M, ArgList, Spec);
    if (InstM)
      Spec->addMember(InstM);
  }

  // 7. Iterate fields
  for (FieldDecl *F : Pattern->fields()) {
    FieldDecl *InstF = SubstituteFieldDecl(F, ArgList);
    if (InstF)
      Spec->addField(InstF);
  }

  // 8. Iterate methods
  for (CXXMethodDecl *MD : Pattern->methods()) {
    CXXMethodDecl *InstMD = SubstituteCXXMethodDecl(MD, ArgList, Spec);
    if (InstMD)
      Spec->addMethod(InstMD);
  }

  // 9. Register specialization
  Template->addSpecialization(Spec);

  --CurrentDepth;
  return Spec;
}

//===----------------------------------------------------------------------===//
// Function Template Instantiation
//===----------------------------------------------------------------------===//

FunctionDecl *
TemplateInstantiator::InstantiateFunctionTemplate(
    FunctionTemplateDecl *Template, llvm::ArrayRef<TemplateArgument> Args) {
  if (!Template)
    return nullptr;

  // Get the pattern function
  FunctionDecl *Pattern =
      llvm::dyn_cast_or_null<FunctionDecl>(Template->getTemplatedDecl());
  if (!Pattern)
    return nullptr;

  if (CurrentDepth >= MaxInstantiationDepth) {
    if (!isSFINAEContext())
      SemaRef.getDiagnostics().report(Template->getLocation(),
                                      DiagID::err_template_recursion);
    return nullptr;
  }

  TemplateArgumentList ArgList(Args);
  ++CurrentDepth;

  // Substitute return type
  QualType RetType = SubstituteType(Pattern->getType(), ArgList);
  if (RetType.isNull())
    RetType = Pattern->getType();

  // Substitute parameter types
  llvm::SmallVector<ParmVarDecl *, 8> InstParams;
  for (unsigned I = 0; I < Pattern->getNumParams(); ++I) {
    ParmVarDecl *PVD = Pattern->getParamDecl(I);
    QualType ParamType = SubstituteType(PVD->getType(), ArgList);
    if (ParamType.isNull())
      ParamType = PVD->getType();

    auto *InstPVD = Context.create<ParmVarDecl>(
        PVD->getLocation(), PVD->getName(), ParamType,
        PVD->getFunctionScopeIndex());
    InstParams.push_back(InstPVD);
  }

  // Create the instantiated function
  auto *InstFD = Context.create<FunctionDecl>(
      Pattern->getLocation(), Pattern->getName(), RetType, InstParams,
      Pattern->getBody(), Pattern->isInline(), Pattern->isConstexpr());

  --CurrentDepth;
  return InstFD;
}

//===----------------------------------------------------------------------===//
// Pack Expansion
//===----------------------------------------------------------------------===//

llvm::SmallVector<Expr *, 4>
TemplateInstantiator::ExpandPack(Expr *Pattern,
                                 const TemplateArgumentList &Args) {
  llvm::SmallVector<Expr *, 4> Result;

  llvm::ArrayRef<TemplateArgument> PackArgs = Args.getPackArgument();
  if (PackArgs.empty())
    return Result;

  for (const TemplateArgument &PA : PackArgs) {
    TemplateArgumentList SingleArg;
    SingleArg.push_back(PA);

    Expr *Inst = SubstituteExpr(Pattern, SingleArg);
    if (Inst)
      Result.push_back(Inst);
  }

  return Result;
}

//===----------------------------------------------------------------------===//
// Type Substitution
//===----------------------------------------------------------------------===//

QualType TemplateInstantiator::SubstituteType(
    QualType T, const TemplateArgumentList &Args) {
  if (T.isNull())
    return T;

  const Type *Ty = T.getTypePtr();

  // TemplateTypeParmType → look up in mapping
  if (auto *TTP = llvm::dyn_cast<TemplateTypeParmType>(Ty))
    return SubstituteTemplateTypeParmType(TTP, Args);

  // TemplateSpecializationType → recursively substitute inner args
  if (auto *TST = llvm::dyn_cast<TemplateSpecializationType>(Ty))
    return SubstituteTemplateSpecializationType(TST, Args);

  // DependentType → try to resolve
  if (auto *DT = llvm::dyn_cast<DependentType>(Ty))
    return SubstituteDependentType(DT, Args);

  // PointerType → recursively substitute pointee
  if (auto *PT = llvm::dyn_cast<PointerType>(Ty)) {
    QualType SubPointee =
        SubstituteType(QualType(PT->getPointeeType(), Qualifier::None), Args);
    if (SubPointee.isNull())
      return QualType();
    return QualType(Context.getPointerType(SubPointee.getTypePtr()),
                    T.getQualifiers());
  }

  // ReferenceType → recursively substitute referenced
  if (auto *RT = llvm::dyn_cast<ReferenceType>(Ty)) {
    QualType SubRef = SubstituteType(
        QualType(RT->getReferencedType(), Qualifier::None), Args);
    if (SubRef.isNull())
      return QualType();
    if (RT->isLValueReference())
      return QualType(Context.getLValueReferenceType(SubRef.getTypePtr()),
                      T.getQualifiers());
    return QualType(Context.getRValueReferenceType(SubRef.getTypePtr()),
                    T.getQualifiers());
  }

  // ArrayType → recursively substitute element
  if (auto *AT = llvm::dyn_cast<ArrayType>(Ty)) {
    QualType SubElem =
        SubstituteType(QualType(AT->getElementType(), Qualifier::None), Args);
    if (SubElem.isNull())
      return QualType();
    if (auto *CAT = llvm::dyn_cast<ConstantArrayType>(AT))
      return QualType(Context.getConstantArrayType(SubElem.getTypePtr(),
                                                   CAT->getSizeExpr(),
                                                   CAT->getSize()),
                      T.getQualifiers());
    if (llvm::isa<IncompleteArrayType>(AT))
      return QualType(Context.getIncompleteArrayType(SubElem.getTypePtr()),
                      T.getQualifiers());
    if (auto *VAT = llvm::dyn_cast<VariableArrayType>(AT))
      return QualType(Context.getVariableArrayType(SubElem.getTypePtr(),
                                                   VAT->getSizeExpr()),
                      T.getQualifiers());
  }

  // FunctionType → substitute return + param types
  if (auto *FT = llvm::dyn_cast<FunctionType>(Ty))
    return SubstituteFunctionType(FT, Args);

  // MemberPointerType
  if (auto *MPT = llvm::dyn_cast<MemberPointerType>(Ty)) {
    QualType SubClass =
        SubstituteType(QualType(MPT->getClassType(), Qualifier::None), Args);
    QualType SubPointee =
        SubstituteType(QualType(MPT->getPointeeType(), Qualifier::None), Args);
    if (SubClass.isNull() || SubPointee.isNull())
      return QualType();
    return QualType(
        Context.getMemberPointerType(SubClass.getTypePtr(),
                                     SubPointee.getTypePtr()),
        T.getQualifiers());
  }

  // ElaboratedType
  if (auto *ET = llvm::dyn_cast<ElaboratedType>(Ty)) {
    QualType SubNamed = SubstituteType(
        QualType(ET->getNamedType(), Qualifier::None), Args);
    if (SubNamed.isNull())
      return QualType();
    return QualType(
        Context.getElaboratedType(SubNamed.getTypePtr(), ET->getQualifier()),
        T.getQualifiers());
  }

  // Non-template-parameter type → return as-is
  return T;
}

bool TemplateInstantiator::isSFINAEContext() const {
  return SFINAE.isSFINAE();
}

Expr *TemplateInstantiator::SubstituteExpr(Expr *E,
                                           const TemplateArgumentList &Args) {
  if (!E)
    return nullptr;

  // For now, return as-is for non-dependent expressions.
  // Dependent expressions need full recursive substitution,
  // which will be expanded in later stages.
  return E;
}

Decl *TemplateInstantiator::SubstituteDecl(Decl *D,
                                           const TemplateArgumentList &Args,
                                           CXXRecordDecl *Parent) {
  if (!D)
    return nullptr;

  // Dispatch based on decl kind
  if (auto *VD = llvm::dyn_cast<VarDecl>(D))
    return SubstituteVarDecl(VD, Args);
  if (auto *FD = llvm::dyn_cast<FieldDecl>(D))
    return SubstituteFieldDecl(FD, Args);
  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(D))
    return SubstituteCXXMethodDecl(MD, Args, Parent);

  // Other decl kinds: return as-is for now
  return D;
}

//===----------------------------------------------------------------------===//
// Specialization Lookup
//===----------------------------------------------------------------------===//

ClassTemplateSpecializationDecl *
TemplateInstantiator::FindExistingSpecialization(
    ClassTemplateDecl *Template, llvm::ArrayRef<TemplateArgument> Args) {
  if (!Template)
    return nullptr;

  for (auto *Spec : Template->getSpecializations()) {
    auto SpecArgs = Spec->getTemplateArgs();
    if (SpecArgs.size() != Args.size())
      continue;

    bool Match = true;
    for (unsigned I = 0; I < Args.size(); ++I) {
      if (SpecArgs[I].getKind() != Args[I].getKind()) {
        Match = false;
        break;
      }
      // Simple comparison for type arguments
      if (SpecArgs[I].isType() && Args[I].isType()) {
        if (SpecArgs[I].getAsType() != Args[I].getAsType()) {
          Match = false;
          break;
        }
      } else if (SpecArgs[I].isIntegral() && Args[I].isIntegral()) {
        if (SpecArgs[I].getAsIntegral() != Args[I].getAsIntegral()) {
          Match = false;
          break;
        }
      }
    }
    if (Match)
      return Spec;
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Private Substitution Helpers
//===----------------------------------------------------------------------===//

QualType TemplateInstantiator::SubstituteTemplateTypeParmType(
    const TemplateTypeParmType *T, const TemplateArgumentList &Args) {
  unsigned Index = T->getIndex();

  // Look up by index in the argument list
  if (Index < Args.size()) {
    const TemplateArgument &Arg = Args[Index];
    if (Arg.isType())
      return Arg.getAsType();
  }

  // No substitution found — return original type
  return QualType(const_cast<TemplateTypeParmType *>(T), Qualifier::None);
}

QualType TemplateInstantiator::SubstituteTemplateSpecializationType(
    const TemplateSpecializationType *T,
    const TemplateArgumentList &Args) {

  // Substitute each template argument
  bool Changed = false;
  llvm::SmallVector<TemplateArgument, 4> NewArgs;

  for (const auto &Arg : T->getTemplateArgs()) {
    if (Arg.isType()) {
      QualType SubType = SubstituteType(Arg.getAsType(), Args);
      if (SubType.isNull())
        return QualType();
      if (SubType.getTypePtr() != Arg.getAsType().getTypePtr())
        Changed = true;
      NewArgs.push_back(TemplateArgument(SubType));
    } else {
      NewArgs.push_back(Arg);
    }
  }

  if (!Changed)
    return QualType(const_cast<TemplateSpecializationType *>(T),
                    Qualifier::None);

  // Create new TemplateSpecializationType with substituted args
  auto *NewTST = Context.getTemplateSpecializationType(T->getTemplateName());
  for (const auto &NA : NewArgs)
    NewTST->addTemplateArg(NA);
  return QualType(NewTST, Qualifier::None);
}

QualType TemplateInstantiator::SubstituteDependentType(
    const DependentType *T, const TemplateArgumentList &Args) {

  if (!T->getBaseType())
    return QualType(const_cast<DependentType *>(T), Qualifier::None);

  QualType SubBase = SubstituteType(
      QualType(T->getBaseType(), Qualifier::None), Args);
  if (SubBase.isNull())
    return QualType(const_cast<DependentType *>(T), Qualifier::None);

  return QualType(Context.getDependentType(SubBase.getTypePtr(), T->getName()),
                  Qualifier::None);
}

QualType TemplateInstantiator::SubstituteFunctionType(
    const FunctionType *FT, const TemplateArgumentList &Args) {

  // Substitute return type
  QualType SubRet =
      SubstituteType(QualType(FT->getReturnType(), Qualifier::None), Args);
  if (SubRet.isNull())
    return QualType();

  // Substitute parameter types
  llvm::SmallVector<const Type *, 4> SubParamTypes;
  for (const Type *PT : FT->getParamTypes()) {
    QualType SubPT = SubstituteType(QualType(PT, Qualifier::None), Args);
    if (SubPT.isNull())
      return QualType();
    SubParamTypes.push_back(SubPT.getTypePtr());
  }

  return QualType(Context.getFunctionType(SubRet.getTypePtr(), SubParamTypes,
                                          FT->isVariadic(), FT->isConst(),
                                          FT->isVolatile()),
                  Qualifier::None);
}

VarDecl *TemplateInstantiator::SubstituteVarDecl(
    VarDecl *VD, const TemplateArgumentList &Args) {
  if (!VD)
    return nullptr;

  QualType SubType = SubstituteType(VD->getType(), Args);
  if (SubType.isNull())
    SubType = VD->getType();

  Expr *SubInit = SubstituteExpr(VD->getInit(), Args);

  return Context.create<VarDecl>(VD->getLocation(), VD->getName(), SubType,
                                 SubInit, VD->isStatic(), VD->isConstexpr());
}

FieldDecl *TemplateInstantiator::SubstituteFieldDecl(
    FieldDecl *FD, const TemplateArgumentList &Args) {
  if (!FD)
    return nullptr;

  QualType SubType = SubstituteType(FD->getType(), Args);
  if (SubType.isNull())
    SubType = FD->getType();

  Expr *SubInit = SubstituteExpr(FD->getInClassInitializer(), Args);

  return Context.create<FieldDecl>(FD->getLocation(), FD->getName(), SubType,
                                   FD->getBitWidth(), FD->isMutable(), SubInit,
                                   FD->getAccess());
}

CXXMethodDecl *TemplateInstantiator::SubstituteCXXMethodDecl(
    CXXMethodDecl *MD, const TemplateArgumentList &Args,
    CXXRecordDecl *Parent) {
  if (!MD)
    return nullptr;

  QualType SubType = SubstituteType(MD->getType(), Args);
  if (SubType.isNull())
    SubType = MD->getType();

  // Substitute parameter types
  llvm::SmallVector<ParmVarDecl *, 8> InstParams;
  for (unsigned I = 0; I < MD->getNumParams(); ++I) {
    ParmVarDecl *PVD = MD->getParamDecl(I);
    QualType ParamType = SubstituteType(PVD->getType(), Args);
    if (ParamType.isNull())
      ParamType = PVD->getType();

    auto *InstPVD = Context.create<ParmVarDecl>(
        PVD->getLocation(), PVD->getName(), ParamType,
        PVD->getFunctionScopeIndex());
    InstParams.push_back(InstPVD);
  }

  // Use the instantiated parent (the ClassTemplateSpecializationDecl) if
  // provided; otherwise fall back to the original parent.
  CXXRecordDecl *MethodParent = Parent ? Parent : MD->getParent();

  return Context.create<CXXMethodDecl>(
      MD->getLocation(), MD->getName(), SubType, InstParams, MethodParent,
      MD->getBody(), MD->isStatic(), MD->isConst(), MD->isVolatile(),
      MD->isVirtual(), MD->isPureVirtual(), MD->isOverride(), MD->isFinal(),
      MD->isDefaulted(), MD->isDeleted(), MD->getRefQualifier(),
      MD->hasNoexceptSpec(), MD->getNoexceptValue(), MD->getNoexceptExpr(),
      MD->getAccess());
}
