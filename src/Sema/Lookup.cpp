//===--- Lookup.cpp - Name Lookup Implementation ----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements name lookup: LookupResult, NestedNameSpecifier,
// Unqualified Lookup, Qualified Lookup, and ADL.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/Lookup.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/DeclContext.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/raw_ostream.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// LookupResult
//===----------------------------------------------------------------------===//

FunctionDecl *LookupResult::getAsFunction() const {
  if (Decls.empty()) return nullptr;
  if (Decls.size() > 1 && !Overloaded) return nullptr;

  for (auto *D : Decls) {
    if (auto *FD = dyn_cast<FunctionDecl>(static_cast<ASTNode *>(D)))
      return FD;
  }
  return nullptr;
}

TypeDecl *LookupResult::getAsTypeDecl() const {
  if (!isSingleResult()) return nullptr;
  return dyn_cast<TypeDecl>(static_cast<ASTNode *>(getFoundDecl()));
}

TagDecl *LookupResult::getAsTagDecl() const {
  if (!isSingleResult()) return nullptr;
  return dyn_cast<TagDecl>(static_cast<ASTNode *>(getFoundDecl()));
}

//===----------------------------------------------------------------------===//
// NestedNameSpecifier
//===----------------------------------------------------------------------===//

NestedNameSpecifier *NestedNameSpecifier::CreateGlobalSpecifier() {
  decltype(NestedNameSpecifier::Data) D;
  D.Namespace = nullptr;
  static NestedNameSpecifier Global(SpecifierKind::Global, D, nullptr);
  return &Global;
}

NestedNameSpecifier *NestedNameSpecifier::Create(ASTContext &Ctx,
                                                  NestedNameSpecifier *Prefix,
                                                  NamespaceDecl *NS) {
  void *Mem = Ctx.getAllocator().Allocate(sizeof(NestedNameSpecifier),
                                          alignof(NestedNameSpecifier));
  decltype(Data) D;
  D.Namespace = NS;
  return new (Mem) NestedNameSpecifier(Namespace, D, Prefix);
}

NestedNameSpecifier *NestedNameSpecifier::Create(ASTContext &Ctx,
                                                  NestedNameSpecifier *Prefix,
                                                  const blocktype::Type *T) {
  void *Mem = Ctx.getAllocator().Allocate(sizeof(NestedNameSpecifier),
                                          alignof(NestedNameSpecifier));
  decltype(Data) D;
  D.TypeSpec = T;
  return new (Mem) NestedNameSpecifier(TypeSpec, D, Prefix);
}

NestedNameSpecifier *NestedNameSpecifier::Create(ASTContext &Ctx,
                                                  NestedNameSpecifier *Prefix,
                                                  llvm::StringRef Identifier) {
  // Persist the string via ASTContext to avoid dangling pointer
  llvm::StringRef Saved = Ctx.saveString(Identifier);
  void *Mem = Ctx.getAllocator().Allocate(sizeof(NestedNameSpecifier),
                                          alignof(NestedNameSpecifier));
  decltype(Data) D;
  D.IdentifierStr = Saved.data();
  return new (Mem) NestedNameSpecifier(SpecifierKind::Identifier, D, Prefix);
}

std::string NestedNameSpecifier::getAsString() const {
  std::string Result;
  if (Prefix) {
    Result += Prefix->getAsString();
  }

  switch (Kind) {
  case Global:
    Result += "::";
    break;
  case Namespace:
    Result += Data.Namespace->getName();
    Result += "::";
    break;
  case TypeSpec:
  case TemplateTypeSpec:
    Result += "<type>::";
    break;
  case Identifier:
    Result += Data.IdentifierStr;
    Result += "::";
    break;
  }

  return Result;
}

//===----------------------------------------------------------------------===//
// Unqualified Lookup [Task 4.3.2]
//===----------------------------------------------------------------------===//

LookupResult Sema::LookupUnqualifiedName(llvm::StringRef Name, Scope *S,
                                          LookupNameKind Kind) {
  LookupResult Result;

  // Member name lookup: only search the immediate scope (no parent walking).
  // This is used for class member access like obj.member or ptr->member.
  if (Kind == LookupNameKind::LookupMemberName) {
    if (S) {
      if (NamedDecl *D = S->lookupInScope(Name)) {
        Result.addDecl(D);
        // Collect function overloads in the same scope
        for (NamedDecl *ScopeD : S->decls()) {
          if (ScopeD == D) continue;
          if (ScopeD->getName() == Name) {
            if (isa<FunctionDecl>(static_cast<ASTNode *>(ScopeD)) ||
                isa<CXXMethodDecl>(static_cast<ASTNode *>(ScopeD))) {
              Result.addDecl(ScopeD);
            }
          }
        }
      }
    }
    if (Result.getNumDecls() > 1)
      Result.setOverloaded(true);
    return Result;
  }

  // Operator name lookup: ordinary lookup is the first phase.
  // The second phase (ADL) is handled by the caller invoking LookupADL separately.
  // Fall through to ordinary lookup logic.

  // Walk up the scope chain
  for (Scope *Cur = S; Cur; Cur = Cur->getParent()) {
    bool FoundInScope = false;

    if (NamedDecl *D = Cur->lookupInScope(Name)) {
      Result.addDecl(D);
      FoundInScope = true;

      // Tag lookup: only tags, skip non-tags
      if (Kind == LookupNameKind::LookupTagName) {
        if (isa<TagDecl>(static_cast<ASTNode *>(D)))
          return Result;
        FoundInScope = false;
        continue;
      }

      // Type name lookup: only type declarations
      if (Kind == LookupNameKind::LookupTypeName) {
        if (isa<TypeDecl>(static_cast<ASTNode *>(D))) {
          Result.setTypeName(true);
          return Result;
        }
        FoundInScope = false;
        continue;
      }

      // Namespace name lookup
      if (Kind == LookupNameKind::LookupNamespaceName) {
        if (isa<NamespaceDecl>(static_cast<ASTNode *>(D)))
          return Result;
        FoundInScope = false;
        continue;
      }

      // Non-function declaration hides everything, stop immediately
      if (!isa<FunctionDecl>(static_cast<ASTNode *>(D)) &&
          !isa<CXXMethodDecl>(static_cast<ASTNode *>(D))) {
        return Result;
      }

      // Function: collect overloads from the same scope
      for (NamedDecl *ScopeD : Cur->decls()) {
        if (ScopeD == D) continue;
        if (ScopeD->getName() == Name) {
          if (isa<FunctionDecl>(static_cast<ASTNode *>(ScopeD)) ||
              isa<CXXMethodDecl>(static_cast<ASTNode *>(ScopeD))) {
            Result.addDecl(ScopeD);
          }
        }
      }
    }

    // Process using directives in this scope.
    // Even when a function was found in scope, using directives may provide
    // additional overload candidates (C++ [namespace.qual]/2).
    for (NamespaceDecl *NS : Cur->getUsingDirectives()) {
      if (NamedDecl *UD = NS->lookup(Name)) {
        if (FoundInScope) {
          // We already found a function in this scope; only add functions
          // from using directives as overload candidates.
          if (isa<FunctionDecl>(static_cast<ASTNode *>(UD)) ||
              isa<CXXMethodDecl>(static_cast<ASTNode *>(UD))) {
            Result.addDecl(UD);
            // Also collect overloads from the using-directive namespace
            for (auto *NSD : NS->decls()) {
              auto *ND = dyn_cast<NamedDecl>(static_cast<ASTNode *>(NSD));
              if (ND && ND != UD && ND->getName() == Name) {
                if (isa<FunctionDecl>(static_cast<ASTNode *>(ND)) ||
                    isa<CXXMethodDecl>(static_cast<ASTNode *>(ND))) {
                  Result.addDecl(ND);
                }
              }
            }
          }
          // Non-function from using directive is hidden by the scope match
        } else {
          Result.addDecl(UD);
          if (!isa<FunctionDecl>(static_cast<ASTNode *>(UD)) &&
              !isa<CXXMethodDecl>(static_cast<ASTNode *>(UD))) {
            return Result;
          }
        }
      }
    }

    if (FoundInScope) {
      if (Result.getNumDecls() > 1)
        Result.setOverloaded(true);
      return Result;
    }
  }

  // Fall back to global symbol table
  if (Result.empty()) {
    if (Kind == LookupNameKind::LookupTagName) {
      if (auto *TD = Symbols.lookupTag(Name))
        Result.addDecl(TD);
    } else if (Kind == LookupNameKind::LookupNamespaceName) {
      if (auto *ND = Symbols.lookupNamespace(Name))
        Result.addDecl(ND);
    } else if (Kind == LookupNameKind::LookupTypeName) {
      if (auto *TD = Symbols.lookupTypedef(Name)) {
        Result.addDecl(TD);
        Result.setTypeName(true);
      } else if (auto *TD = Symbols.lookupTag(Name)) {
        Result.addDecl(TD);
        Result.setTypeName(true);
      } else if (auto *CTD = Symbols.lookupTemplate(Name)) {
        // Class template names are valid type names (e.g., vector in vector<int>)
        if (llvm::isa<ClassTemplateDecl>(CTD)) {
          Result.addDecl(CTD);
          Result.setTypeName(true);
        }
      }
    } else {
      for (NamedDecl *D : Symbols.lookupOrdinary(Name)) {
        Result.addDecl(D);
      }
      // Also check template and concept symbols (e.g., for template name
      // usage like f<int> where f is a function template).
      if (Result.empty()) {
        if (auto *TD = Symbols.lookupTemplate(Name))
          Result.addDecl(TD);
        if (auto *CD = Symbols.lookupConcept(Name))
          Result.addDecl(CD);
      }
    }
  }

  if (Result.getNumDecls() > 1)
    Result.setOverloaded(true);

  return Result;
}

//===----------------------------------------------------------------------===//
// Qualified Lookup [Task 4.3.3]
//===----------------------------------------------------------------------===//

namespace {

/// Lookup a name in a CXXRecordDecl and all of its base classes.
/// Uses a visited set to handle diamond inheritance correctly.
void lookupInClassAndBases(CXXRecordDecl *Class, llvm::StringRef Name,
                           LookupResult &Result,
                           llvm::SmallPtrSetImpl<Decl *> &Visited) {
  if (!Class || Visited.count(Class))
    return;
  Visited.insert(Class);

  DeclContext *DC = static_cast<DeclContext *>(Class);

  // Lookup in this class's own context (not parent contexts).
  if (NamedDecl *D = DC->lookupInContext(Name)) {
    Result.addDecl(D);

    // Collect function overloads in the same class.
    for (auto *ChildD : DC->decls()) {
      auto *ND = dyn_cast<NamedDecl>(static_cast<ASTNode *>(ChildD));
      if (ND && ND != D && ND->getName() == Name) {
        if (isa<FunctionDecl>(static_cast<ASTNode *>(ND)) ||
            isa<CXXMethodDecl>(static_cast<ASTNode *>(ND))) {
          Result.addDecl(ND);
        }
      }
    }
  }

  // Recurse into base classes.
  for (const auto &Base : Class->bases()) {
    QualType BaseType = Base.getType();
    const Type *BT = BaseType.getTypePtr();
    if (BT && BT->isRecordType()) {
      auto *RT = static_cast<const RecordType *>(BT);
      if (auto *RD = RT->getDecl()) {
        if (auto *BaseCXXRD =
                dyn_cast<CXXRecordDecl>(static_cast<ASTNode *>(RD))) {
          lookupInClassAndBases(BaseCXXRD, Name, Result, Visited);
        }
      }
    }
  }
}

} // anonymous namespace

LookupResult Sema::LookupQualifiedName(llvm::StringRef Name,
                                         NestedNameSpecifier *NNS) {
  LookupResult Result;

  if (!NNS) return Result;

  DeclContext *DC = nullptr;

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Global:
    if (CurTU)
      DC = CurTU->getDeclContext();
    break;

  case NestedNameSpecifier::Namespace:
    DC = NNS->getAsNamespace();
    break;

  case NestedNameSpecifier::TypeSpec: {
    const blocktype::Type *T = NNS->getAsType();
    if (T && T->isRecordType()) {
      auto *RT = static_cast<const RecordType *>(T);
      RecordDecl *RD = RT->getDecl();
      if (RD) {
        // Qualified lookup "Class::name" must search in the class itself.
        // Only CXXRecordDecl inherits DeclContext and supports member lookup.
        // Plain RecordDecl (C-style struct) does not inherit DeclContext.
        auto *CXXRD = dyn_cast<CXXRecordDecl>(static_cast<ASTNode *>(RD));
        if (CXXRD)
          DC = static_cast<DeclContext *>(CXXRD);
        // Non-CXX records cannot serve as DeclContext; qualified lookup fails.
      }
    }
    if (!DC && T && T->isEnumType()) {
      auto *ET = static_cast<const EnumType *>(T);
      EnumDecl *ED = ET->getDecl();
      if (ED)
        DC = static_cast<DeclContext *>(ED);
    }
    break;
  }

  case NestedNameSpecifier::TemplateTypeSpec:
    break;

  case NestedNameSpecifier::Identifier:
    break;
  }

  if (!DC) return Result;

  // If DC is a CXXRecordDecl, perform lookup in the class and its base classes.
  // Non-class contexts (namespace, enum, TU) use simple lookup.
  if (DC->isCXXRecord()) {
    auto *Class = static_cast<CXXRecordDecl *>(DC);
    llvm::SmallPtrSet<Decl *, 8> Visited;
    lookupInClassAndBases(Class, Name, Result, Visited);
  } else {
    // Namespace / enum / TU: direct lookup.
    if (NamedDecl *D = DC->lookup(Name)) {
      Result.addDecl(D);

      // Collect function overloads
      for (auto *ChildD : DC->decls()) {
        auto *ND = dyn_cast<NamedDecl>(static_cast<ASTNode *>(ChildD));
        if (ND && ND != D && ND->getName() == Name) {
          if (isa<FunctionDecl>(static_cast<ASTNode *>(ND)) ||
              isa<CXXMethodDecl>(static_cast<ASTNode *>(ND))) {
            Result.addDecl(ND);
          }
        }
      }
    }
  }

  if (Result.getNumDecls() > 1)
    Result.setOverloaded(true);

  return Result;
}

//===----------------------------------------------------------------------===//
// ADL (Argument-Dependent Lookup) [Task 4.3.4]
//===----------------------------------------------------------------------===//

namespace {

/// Find the innermost enclosing namespace DeclContext for a given DeclContext.
/// Walks up the parent chain starting from DC's parent.
DeclContext *findEnclosingNamespace(DeclContext *DC) {
  if (!DC) return nullptr;
  DeclContext *Ctx = DC->getParent();
  while (Ctx) {
    if (Ctx->isNamespace())
      return Ctx;
    Ctx = Ctx->getParent();
  }
  return nullptr;
}

/// Find the NamespaceDecl* corresponding to a DeclContext that is a namespace.
/// Searches the parent's decls, then recursively through ancestors for
/// robustness with deeply nested namespaces.
NamespaceDecl *findNamespaceDecl(DeclContext *NSCtx) {
  if (!NSCtx || !NSCtx->isNamespace()) return nullptr;

  // Search up the ancestor chain for the NamespaceDecl.
  // Typically the immediate parent contains it, but we search ancestors
  // to handle edge cases.
  for (DeclContext *Parent = NSCtx->getParent(); Parent;
       Parent = Parent->getParent()) {
    for (auto *D : Parent->decls()) {
      auto *ND = dyn_cast<NamespaceDecl>(static_cast<ASTNode *>(D));
      if (ND && static_cast<DeclContext *>(ND) == NSCtx) {
        return ND;
      }
    }
  }
  return nullptr;
}

/// Add a namespace DeclContext to the associated namespaces set.
/// Handles inline namespaces: if the namespace is inline, also adds its
/// enclosing namespace.
void addAssociatedNamespace(DeclContext *NSCtx,
                            llvm::SmallPtrSetImpl<NamespaceDecl *> &Namespaces) {
  NamespaceDecl *NS = findNamespaceDecl(NSCtx);
  if (!NS) return;
  if (!Namespaces.insert(NS).second)
    return; // Already added

  // If this is an inline namespace, also add the enclosing namespace.
  if (NS->isInline()) {
    DeclContext *Enclosing = findEnclosingNamespace(NSCtx);
    if (Enclosing && Enclosing->isNamespace()) {
      addAssociatedNamespace(Enclosing, Namespaces);
    }
  }
}

} // anonymous namespace

void Sema::LookupADL(llvm::StringRef Name,
                      llvm::ArrayRef<Expr *> Args,
                      LookupResult &Result) {
  llvm::SmallPtrSet<NamespaceDecl *, 8> AssociatedNamespaces;
  llvm::SmallPtrSet<const RecordType *, 8> AssociatedClasses;

  for (Expr *Arg : Args) {
    CollectAssociatedNamespacesAndClasses(Arg->getType(),
                                           AssociatedNamespaces,
                                           AssociatedClasses);
  }

  // Look up the name in each associated namespace
  for (NamespaceDecl *NS : AssociatedNamespaces) {
    if (NamedDecl *D = NS->lookup(Name)) {
      Result.addDecl(D);
    }
  }

  // Look up friend functions in associated classes.
  // Per C++ ADL rules, only friend functions declared in the class are
  // visible through ADL. We search FriendDecl nodes in the class.
  for (const RecordType *RT : AssociatedClasses) {
    RecordDecl *RD = RT->getDecl();
    auto *CXXRD = dyn_cast<CXXRecordDecl>(static_cast<ASTNode *>(RD));
    if (!CXXRD) continue;

    DeclContext *ClassDC = static_cast<DeclContext *>(CXXRD);
    for (auto *D : ClassDC->decls()) {
      auto *FD = dyn_cast<FriendDecl>(static_cast<ASTNode *>(D));
      if (!FD || FD->isFriendType()) continue; // Skip friend class decls

      NamedDecl *FriendND = FD->getFriendDecl();
      if (!FriendND || FriendND->getName() != Name) continue;

      // Only free functions (not member functions) are ADL-visible friends.
      if (isa<FunctionDecl>(static_cast<ASTNode *>(FriendND))) {
        Result.addDecl(FriendND);
      }
    }
  }

  if (Result.getNumDecls() > 1)
    Result.setOverloaded(true);
}

void Sema::CollectAssociatedNamespacesAndClasses(
    QualType T,
    llvm::SmallPtrSetImpl<NamespaceDecl *> &Namespaces,
    llvm::SmallPtrSetImpl<const RecordType *> &Classes) {
  if (T.isNull()) return;

  const Type *Ty = T.getTypePtr();

  // Class type: add class + enclosing namespace
  if (Ty->isRecordType()) {
    auto *RT = static_cast<const RecordType *>(Ty);
    Classes.insert(RT);

    RecordDecl *RD = RT->getDecl();
    auto *CXXRD = dyn_cast<CXXRecordDecl>(static_cast<ASTNode *>(RD));
    if (CXXRD) {
      DeclContext *NSCtx = findEnclosingNamespace(CXXRD->getDeclContext());
      if (NSCtx)
        addAssociatedNamespace(NSCtx, Namespaces);

      // Process base classes for ADL
      for (const auto &Base : CXXRD->bases()) {
        QualType BaseType = Base.getType();
        if (BaseType.getTypePtr() && BaseType->isRecordType()) {
          auto *BaseRT = static_cast<const RecordType *>(BaseType.getTypePtr());
          Classes.insert(BaseRT);
          RecordDecl *BaseRD = BaseRT->getDecl();
          auto *BaseCXXRD = dyn_cast<CXXRecordDecl>(
              static_cast<ASTNode *>(BaseRD));
          if (BaseCXXRD) {
            DeclContext *BaseNS = findEnclosingNamespace(
                BaseCXXRD->getDeclContext());
            if (BaseNS)
              addAssociatedNamespace(BaseNS, Namespaces);
          }
        }
      }
    }
  }

  // Enum type: add enclosing namespace
  if (Ty->isEnumType()) {
    auto *ET = static_cast<const EnumType *>(Ty);
    EnumDecl *ED = ET->getDecl();
    if (ED) {
      DeclContext *EnumDC = static_cast<DeclContext *>(ED);
      DeclContext *NSCtx = findEnclosingNamespace(EnumDC);
      if (NSCtx)
        addAssociatedNamespace(NSCtx, Namespaces);
    }
  }

  // Template specialization type: recurse into template arguments
  // e.g., std::vector<int> → collect from int's namespace (none) and std.
  if (Ty->getTypeClass() == TypeClass::TemplateSpecialization) {
    auto *TST = static_cast<const TemplateSpecializationType *>(Ty);
    for (const auto &Arg : TST->getTemplateArgs()) {
      if (Arg.isType()) {
        CollectAssociatedNamespacesAndClasses(Arg.getAsType(),
                                               Namespaces, Classes);
      }
    }
  }

  // Pointer type: recurse into pointee
  if (Ty->isPointerType()) {
    auto *PT = static_cast<const PointerType *>(Ty);
    CollectAssociatedNamespacesAndClasses(
        QualType(PT->getPointeeType(), Qualifier::None),
        Namespaces, Classes);
  }

  // Reference type: recurse into referenced type
  if (Ty->isReferenceType()) {
    auto *RT = static_cast<const ReferenceType *>(Ty);
    CollectAssociatedNamespacesAndClasses(
        QualType(RT->getReferencedType(), Qualifier::None),
        Namespaces, Classes);
  }

  // Array type: recurse into element type
  if (Ty->isArrayType()) {
    auto *AT = static_cast<const ArrayType *>(Ty);
    CollectAssociatedNamespacesAndClasses(
        QualType(AT->getElementType(), Qualifier::None),
        Namespaces, Classes);
  }
}
