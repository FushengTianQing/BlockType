//===--- TemplateInstantiator.cpp - Template Instantiation Engine -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/SFINAE.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/Basic/DiagnosticIDs.h"
#include "llvm/Support/Casting.h"
#include <string>

namespace blocktype {

//===----------------------------------------------------------------------===//
// TemplateInstantiator Implementation
//===----------------------------------------------------------------------===//

TemplateInstantiator::TemplateInstantiator(Sema &Sema)
    : SemaRef(Sema) {}

FunctionDecl *TemplateInstantiator::InstantiateFunctionTemplate(
    FunctionTemplateDecl *FuncTemplate,
    llvm::ArrayRef<TemplateArgument> TemplateArgs) {
  
  if (FuncTemplate == nullptr) {
    return nullptr;
  }
  
  // Step 1: Check if a specialization already exists (cache lookup)
  if (auto *Existing = FuncTemplate->findSpecialization(TemplateArgs)) {
    return Existing;
  }
  
  // Step 2: Enter SFINAE context for substitution
  unsigned SavedErrors = SemaRef.getDiagnostics().getNumErrors();
  SFINAEGuard SFINAEGuard(SFContext, SavedErrors, &SemaRef.getDiagnostics());
  
  // Step 3: Delegate to Sema for actual instantiation
  // The SFINAE context will catch any substitution failures
  FunctionDecl *Result = SemaRef.InstantiateFunctionTemplate(
      FuncTemplate, TemplateArgs, FuncTemplate->getLocation());
  
  // Step 4: If substitution failed during instantiation, return nullptr
  // (the candidate is removed from overload set per SFINAE rules)
  if (Result == nullptr || SemaRef.getDiagnostics().hasErrorOccurred()) {
    return nullptr;
  }
  
  return Result;
}

CXXRecordDecl *TemplateInstantiator::InstantiateClassTemplate(
    ClassTemplateDecl *ClassTemplate,
    llvm::ArrayRef<TemplateArgument> TemplateArgs) {
  
  if (ClassTemplate == nullptr) {
    return nullptr;
  }
  
  // Check if a specialization already exists
  if (auto *Existing = ClassTemplate->findSpecialization(TemplateArgs)) {
    return Existing;
  }
  
  // TODO: Implement full class template instantiation
  // For now, return the templated declaration as a placeholder
  return llvm::dyn_cast<CXXRecordDecl>(ClassTemplate->getTemplatedDecl());
}

Expr *TemplateInstantiator::InstantiateFoldExpr(
    CXXFoldExpr *FoldExpr,
    llvm::ArrayRef<TemplateArgument> PackArgs) {
  
  if (FoldExpr == nullptr) {
    return nullptr;
  }
  
  // Get the pack expansion arguments
  llvm::SmallVector<TemplateArgument, 4> PackElements;
  for (const auto &Arg : PackArgs) {
    if (Arg.isPack()) {
      for (const auto &Elem : Arg.getAsPack()) {
        PackElements.push_back(Elem);
      }
    }
  }
  
  // If pack is empty, return the init value or pattern
  // For empty packs, fold expressions return their identity element
  // which is stored in LHS (for left fold) or RHS (for right fold)
  if (PackElements.empty()) {
    // Return LHS for left fold, RHS for right fold, or pattern as fallback
    if (FoldExpr->getLHS() != nullptr) {
      return FoldExpr->getLHS();
    }
    if (FoldExpr->getRHS() != nullptr) {
      return FoldExpr->getRHS();
    }
    return FoldExpr->getPattern();
  }
  
  // Build the folded expression chain
  // For fold expressions, we need to:
  // 1. Instantiate the pattern for each pack element
  // 2. Connect them with binary operators
  // 3. Handle init values (LHS/RHS)
  
  ASTContext &Context = SemaRef.getASTContext();
  Expr *Result = nullptr;
  
  // Helper to instantiate pattern with a template argument
  auto InstantiatePattern = [&](const TemplateArgument &Arg) -> Expr* {
    // For now, we handle simple cases
    // Full implementation would need proper pattern substitution
    
    if (Arg.isExpression()) {
      return Arg.getAsExpr();
    } else if (Arg.isIntegral()) {
      return Context.create<IntegerLiteral>(
          FoldExpr->getLocation(),
          Arg.getAsIntegral(),
          Context.getIntType());
    } else if (Arg.isType()) {
      // Type argument in expression context
      // This happens for patterns like sizeof(Ts), alignof(Ts)
      // The pattern itself (e.g., sizeof) handles the type directly
      // We need to instantiate the pattern with this type
      
      // For sizeof(Ts), the pattern is UnaryExprOrTypeTraitExpr
      // We create a new UnaryExprOrTypeTraitExpr with the substituted type
      if (auto *UnaryExpr = llvm::dyn_cast<UnaryExprOrTypeTraitExpr>(FoldExpr->getPattern())) {
        return Context.create<UnaryExprOrTypeTraitExpr>(
            UnaryExpr->getLocation(),
            UnaryExpr->getTraitKind(),
            Arg.getAsType());
      }
      
      // For other type-dependent expressions, we would need more sophisticated handling
      // For now, return the pattern as fallback
      return FoldExpr->getPattern();
    } else if (Arg.isDeclaration()) {
      if (auto *Decl = llvm::dyn_cast<ValueDecl>(Arg.getAsDecl())) {
        return Context.create<DeclRefExpr>(
            FoldExpr->getLocation(),
            Decl,
            Decl->getName());
      }
    }
    
    // Fallback: return the pattern
    return FoldExpr->getPattern();
  };
  
  // Instantiate pattern for each pack element
  llvm::SmallVector<Expr *, 8> InstantiatedElements;
  for (const auto &Elem : PackElements) {
    if (Expr *Instantiated = InstantiatePattern(Elem)) {
      InstantiatedElements.push_back(Instantiated);
    }
  }
  
  if (InstantiatedElements.empty()) {
    // No elements to fold, return init or pattern
    if (FoldExpr->getLHS() != nullptr) {
      return FoldExpr->getLHS();
    }
    if (FoldExpr->getRHS() != nullptr) {
      return FoldExpr->getRHS();
    }
    return FoldExpr->getPattern();
  }
  
  // Build the fold chain
  if (FoldExpr->isRightFold()) {
    // Right fold: e1 op (e2 op (e3 op ... [op init]))
    // Start from the rightmost element
    Result = InstantiatedElements.back();
    InstantiatedElements.pop_back();
    
    // Add init value if present
    if (FoldExpr->getRHS() != nullptr) {
      Result = Context.create<BinaryOperator>(
          Result->getLocation(),
          Result,
          FoldExpr->getRHS(),
          FoldExpr->getOperator());
    }
    
    // Fold from right to left
    for (int I = InstantiatedElements.size() - 1; I >= 0; --I) {
      Result = Context.create<BinaryOperator>(
          InstantiatedElements[I]->getLocation(),
          InstantiatedElements[I],
          Result,
          FoldExpr->getOperator());
    }
  } else {
    // Left fold: (([init op] e1) op e2) op e3 ...
    // Start from the leftmost element
    Result = InstantiatedElements.front();
    InstantiatedElements.erase(InstantiatedElements.begin());
    
    // Add init value if present
    if (FoldExpr->getLHS() != nullptr) {
      Result = Context.create<BinaryOperator>(
          Result->getLocation(),
          FoldExpr->getLHS(),
          Result,
          FoldExpr->getOperator());
    }
    
    // Fold from left to right
    for (Expr *Elem : InstantiatedElements) {
      Result = Context.create<BinaryOperator>(
          Elem->getLocation(),
          Result,
          Elem,
          FoldExpr->getOperator());
    }
  }
  
  return Result;
}

Expr *TemplateInstantiator::InstantiatePackIndexingExpr(
    PackIndexingExpr *PIE,
    llvm::ArrayRef<TemplateArgument> PackArgs) {
  
  if (PIE == nullptr) {
    return nullptr;
  }
  
  // Get the pack expansion arguments
  llvm::SmallVector<TemplateArgument, 4> PackElements;
  for (const auto &Arg : PackArgs) {
    if (Arg.isPack()) {
      for (const auto &Elem : Arg.getAsPack()) {
        PackElements.push_back(Elem);
      }
    }
  }
  
  // Get the index value
  Expr *IndexExpr = PIE->getIndex();
  uint64_t Index = 0;
  
  // Try to evaluate the index as a constant
  if (auto *IntLit = llvm::dyn_cast<IntegerLiteral>(IndexExpr)) {
    Index = IntLit->getValue().getZExtValue();
  } else {
    // Runtime index - cannot instantiate at compile time
    // Return the original expression for runtime evaluation
    return PIE;
  }
  
  // Check bounds
  if (PackElements.empty()) {
    // Empty pack - cannot index
    SemaRef.Diag(PIE->getLocation(), DiagID::err_pack_index_empty_pack);
    return nullptr;
  }
  
  if (Index >= PackElements.size()) {
    // Out of bounds - emit diagnostic
    SemaRef.Diag(IndexExpr->getLocation(), DiagID::err_pack_index_out_of_bounds,
                 std::to_string(Index) + " " + std::to_string(PackElements.size()));
    return nullptr;
  }
  
  // Build substituted expressions from pack elements
  llvm::SmallVector<Expr *, 4> SubstitutedExprs;
  ASTContext &Context = SemaRef.getASTContext();
  
  for (const auto &Elem : PackElements) {
    if (Elem.isIntegral()) {
      // Create an integer literal for integral arguments
      auto *IntLit = Context.create<IntegerLiteral>(SourceLocation(1),
                                                     Elem.getAsIntegral(),
                                                     Context.getIntType());
      SubstitutedExprs.push_back(IntLit);
    } else if (Elem.isType()) {
      // Type template arguments in expression context.
      // Create a TypeRefExpr to represent the type in the expression context.
      // This is used for pack indexing where the pack contains type arguments.
      QualType TypeArg = Elem.getAsType();
      if (!TypeArg.isNull()) {
        auto *TypeRef = Context.create<TypeRefExpr>(SourceLocation(1), TypeArg);
        SubstitutedExprs.push_back(TypeRef);
      }
    } else if (Elem.isExpression()) {
      // Use the expression directly
      if (Expr *ExprVal = Elem.getAsExpr()) {
        SubstitutedExprs.push_back(ExprVal);
      }
    } else if (Elem.isDeclaration()) {
      // Create a declaration reference for declaration arguments
      if (auto *D = Elem.getAsDecl()) {
        // DeclRefExpr expects (SourceLocation, ValueDecl*, StringRef)
        // ValueDecl is the base class for variables, functions, etc.
        if (auto *VD = llvm::dyn_cast<ValueDecl>(D)) {
          auto *DeclRef = Context.create<DeclRefExpr>(SourceLocation(1), VD, VD->getName());
          SubstitutedExprs.push_back(DeclRef);
        }
      }
    }
    // Note: Template template arguments are not directly representable as expressions
    // They would need special handling in a full implementation
  }
  
  // Set the substituted expressions on the PackIndexingExpr
  PIE->setSubstitutedExprs(SubstitutedExprs);
  
  // Return the Nth element if we have substituted expressions
  if (!SubstitutedExprs.empty() && Index < SubstitutedExprs.size()) {
    return SubstitutedExprs[Index];
  }
  
  // Return the original expression if we couldn't substitute
  return PIE;
}

} // namespace blocktype
