//===--- AccessControl.cpp - C++ Access Control --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AccessControl class for checking C++ access control.
//
// Task 4.5.2 — 访问控制检查
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/AccessControl.h"
#include "blocktype/Basic/Diagnostics.h"

#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Helper: extract the accessing class from a DeclContext chain
//===----------------------------------------------------------------------===//

static CXXRecordDecl *findEnclosingClass(DeclContext *DC) {
  for (DeclContext *C = DC; C; C = C->getParent()) {
    if (C->getDeclContextKind() == DeclContextKind::CXXRecord) {
      return static_cast<CXXRecordDecl *>(C);
    }
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Helper: extract the accessing function name from a DeclContext chain
//===----------------------------------------------------------------------===//

static llvm::StringRef findEnclosingFunctionName(DeclContext *DC) {
  for (DeclContext *C = DC; C; C = C->getParent()) {
    if (C->getDeclContextKind() == DeclContextKind::Function) {
      // FunctionDecl also inherits from DeclContext in this project.
      // DeclContext for a Function maps to the FunctionDecl's context.
      // We need to find the FunctionDecl that owns this DeclContext.
      // Walk the parent's decls to find a FunctionDecl matching this context.
      // However, DeclContext and Decl are unrelated by inheritance.
      // Instead, search the parent context for function declarations that
      // match by pointer equality (simplified).
      break;
    }
  }
  return "";
}

//===----------------------------------------------------------------------===//
// AccessControl — isAccessible
//===----------------------------------------------------------------------===//

bool AccessControl::isAccessible(NamedDecl *Member,
                                 AccessSpecifier MemberAccess,
                                 DeclContext *AccessingContext,
                                 CXXRecordDecl *ClassContext) {
  // Public members are always accessible
  if (MemberAccess == AccessSpecifier::AS_public)
    return true;

  if (!AccessingContext || !ClassContext)
    return false;

  CXXRecordDecl *AccessingClass = findEnclosingClass(AccessingContext);

  // If accessing from a class context
  if (AccessingClass) {
    // Same class: all members are accessible (including private)
    if (AccessingClass == ClassContext)
      return true;

    // Derived class: check protected/private inheritance
    if (AccessingClass->isDerivedFrom(ClassContext)) {
      // Protected members are accessible from derived classes
      if (MemberAccess == AccessSpecifier::AS_protected)
        return true;

      // Private members are not accessible from derived classes
      // (unless friend — checked separately in CheckMemberAccess)
      return false;
    }
  }

  // Not in a class context, or unrelated class:
  // Only public members are accessible
  return false;
}

//===----------------------------------------------------------------------===//
// AccessControl — CheckMemberAccess (问题6: 取消注释诊断代码)
//===----------------------------------------------------------------------===//

bool AccessControl::CheckMemberAccess(NamedDecl *Member,
                                      AccessSpecifier Access,
                                      CXXRecordDecl *MemberClass,
                                      DeclContext *AccessingContext,
                                      SourceLocation AccessLoc,
                                      DiagnosticsEngine &Diags) {
  // First check basic accessibility
  if (isAccessible(Member, Access, AccessingContext, MemberClass))
    return true;

  // Check friend access
  if (CheckFriendAccess(Member, MemberClass, AccessingContext))
    return true;

  // Access denied — report appropriate diagnostic
  if (Access == AccessSpecifier::AS_private) {
    Diags.report(AccessLoc, DiagID::err_access_private,
                 "'" + Member->getName().str() +
                 "' is a private member of '" +
                 MemberClass->getName().str() + "'");
    return false;
  }

  if (Access == AccessSpecifier::AS_protected) {
    Diags.report(AccessLoc, DiagID::err_access_protected,
                 "'" + Member->getName().str() +
                 "' is a protected member of '" +
                 MemberClass->getName().str() + "'");
    return false;
  }

  return false;
}

//===----------------------------------------------------------------------===//
// AccessControl — CheckBaseClassAccess (问题7: 完整实现)
//===----------------------------------------------------------------------===//

bool AccessControl::CheckBaseClassAccess(CXXRecordDecl *Base,
                                         AccessSpecifier Access,
                                         CXXRecordDecl *Derived,
                                         DeclContext *AccessingContext,
                                         SourceLocation AccessLoc,
                                         DiagnosticsEngine &Diags) {
  // Public base: always accessible
  if (Access == AccessSpecifier::AS_public)
    return true;

  if (!AccessingContext || !Derived)
    return false;

  // Find the accessing class context
  CXXRecordDecl *AccessingClass = findEnclosingClass(AccessingContext);

  // Private base: only accessible from the class itself (and friends)
  // Per C++ [class.access.base]: A private base class is accessible only
  // from members and friends of the derived class.
  if (Access == AccessSpecifier::AS_private) {
    // The accessing context must be the Derived class itself
    if (AccessingClass == Derived)
      return true;

    // Check friend access: if the accessing context is a friend of Derived
    // We use a temporary approach — check if any Decl in the accessing
    // context matches a friend declaration of Derived.
    for (auto *D : Derived->members()) {
      if (auto *FD = llvm::dyn_cast<FriendDecl>(D)) {
        // Friend type: check if accessing class is the friend type
        if (FD->isFriendType()) {
          QualType FT = FD->getFriendType();
          if (FT.getTypePtr()) {
            if (auto *RT = llvm::dyn_cast<RecordType>(FT.getTypePtr())) {
              if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                if (RD == AccessingClass)
                  return true;
              }
            }
          }
        }
        // Friend function: check if accessing function matches
        if (NamedDecl *FrDecl = FD->getFriendDecl()) {
          if (auto *FriendFn = llvm::dyn_cast<FunctionDecl>(FrDecl)) {
            // Check if the AccessingContext is a function context
            // and matches by name
            for (DeclContext *C = AccessingContext; C;
                 C = C->getParent()) {
              if (C->getDeclContextKind() == DeclContextKind::Function) {
                // Compare by walking parent's members for matching
                // function declarations
                DeclContext *Parent = C->getParent();
                if (Parent) {
                  for (auto &Child : Parent->decls()) {
                    if (auto *Fn = llvm::dyn_cast<FunctionDecl>(Child)) {
                      if (Fn->getName() == FriendFn->getName())
                        return true;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }

    Diags.report(AccessLoc, DiagID::err_access_private,
                 Base->getName().str() + " (private base)");
    return false;
  }

  // Protected base: accessible from the class, its derived classes, and friends
  // Per C++ [class.access.base]: A protected base class is accessible from
  // members and friends of the derived class, and from members of classes
  // derived from the derived class.
  if (Access == AccessSpecifier::AS_protected) {
    // Accessing from the Derived class itself
    if (AccessingClass == Derived)
      return true;

    // Accessing from a class derived from Derived
    if (AccessingClass && AccessingClass->isDerivedFrom(Derived))
      return true;

    // Check friend access (same logic as private base)
    for (auto *D : Derived->members()) {
      if (auto *FD = llvm::dyn_cast<FriendDecl>(D)) {
        if (FD->isFriendType()) {
          QualType FT = FD->getFriendType();
          if (FT.getTypePtr()) {
            if (auto *RT = llvm::dyn_cast<RecordType>(FT.getTypePtr())) {
              if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
                if (RD == AccessingClass)
                  return true;
              }
            }
          }
        }
      }
    }

    Diags.report(AccessLoc, DiagID::err_access_protected,
                 Base->getName().str() + " (protected base)");
    return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
// AccessControl — CheckFriendAccess (问题8: 完整实现)
//===----------------------------------------------------------------------===//

bool AccessControl::CheckFriendAccess(NamedDecl *Friend,
                                      CXXRecordDecl *Class,
                                      DeclContext *AccessingContext) {
  if (!Class || !AccessingContext)
    return false;

  // Find the accessing class (if any)
  CXXRecordDecl *AccessingClass = findEnclosingClass(AccessingContext);

  // Walk through the class's members to find friend declarations
  for (auto *D : Class->members()) {
    if (auto *FD = llvm::dyn_cast<FriendDecl>(D)) {
      // Case 1: friend class/struct (friend class X;)
      // Check if the accessing class matches the friend type
      if (FD->isFriendType()) {
        QualType FT = FD->getFriendType();
        if (FT.getTypePtr()) {
          // Compare the accessing class with the friend type
          if (auto *RT = llvm::dyn_cast<RecordType>(FT.getTypePtr())) {
            if (auto *RD = llvm::dyn_cast<CXXRecordDecl>(RT->getDecl())) {
              if (RD == AccessingClass)
                return true;
            }
          }
        }
        continue;
      }

      // Case 2: friend function (friend void f(); or friend void C::f();)
      if (NamedDecl *FriendDecl = FD->getFriendDecl()) {
        // Direct pointer comparison: same declaration
        if (FriendDecl == Friend)
          return true;

        // Name-based matching for function declarations
        // Per C++ [class.friend]: A friend declaration that declares a
        // function makes the function a friend of the class.
        if (auto *FriendFn = llvm::dyn_cast<FunctionDecl>(FriendDecl)) {
          // Walk up the DeclContext chain to find function contexts
          for (DeclContext *C = AccessingContext; C; C = C->getParent()) {
            if (C->getDeclContextKind() == DeclContextKind::Function) {
              // Look in the parent context for a FunctionDecl with the
              // same name as the friend function
              DeclContext *Parent = C->getParent();
              if (Parent) {
                for (auto &Child : Parent->decls()) {
                  if (auto *Fn = llvm::dyn_cast<FunctionDecl>(Child)) {
                    if (Fn->getName() == FriendFn->getName())
                      return true;
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// AccessControl — getEffectiveAccess
//===----------------------------------------------------------------------===//

AccessSpecifier AccessControl::getEffectiveAccess(NamedDecl *D) {
  // For members with explicit access specifiers
  if (auto *FD = llvm::dyn_cast<FieldDecl>(D))
    return FD->getAccess();

  if (auto *MD = llvm::dyn_cast<CXXMethodDecl>(D))
    return MD->getAccess();

  // Default to private for class members, public for non-class
  return AccessSpecifier::AS_public;
}

//===----------------------------------------------------------------------===//
// AccessControl — isDerivedFrom
//===----------------------------------------------------------------------===//

bool AccessControl::isDerivedFrom(CXXRecordDecl *Derived,
                                  CXXRecordDecl *Base) {
  if (!Derived || !Base)
    return false;

  return Derived->isDerivedFrom(Base);
}

} // namespace blocktype
