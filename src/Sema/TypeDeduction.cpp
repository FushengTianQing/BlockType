//===--- TypeDeduction.cpp - Auto/Decltype Type Deduction --*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/TypeDeduction.h"
#include "blocktype/Sema/TemplateDeduction.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"

namespace blocktype {

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

//===----------------------------------------------------------------------===//
// auto deduction
//===----------------------------------------------------------------------===//

QualType TypeDeduction::deduceAutoType(QualType DeclaredType, Expr *Init) {
  if (!Init) return QualType();

  QualType T = Init->getType();
  if (T.isNull()) return QualType();

  // 1. Strip top-level reference
  if (T->isReferenceType()) {
    auto *RefTy = static_cast<const ReferenceType *>(T.getTypePtr());
    T = QualType(RefTy->getReferencedType(), T.getQualifiers());
  }

  // 2. Strip top-level const/volatile (auto drops top-level CV)
  //    Unless declared as const auto
  if (!DeclaredType.isConstQualified()) {
    T = T.withoutConstQualifier().withoutVolatileQualifier();
  }

  // 3. Array decay to pointer
  if (T->isArrayType()) {
    const Type *ElemType = nullptr;
    if (T->getTypeClass() == TypeClass::ConstantArray) {
      auto *CA = static_cast<const ConstantArrayType *>(T.getTypePtr());
      ElemType = CA->getElementType();
    } else if (T->getTypeClass() == TypeClass::IncompleteArray) {
      auto *IA = static_cast<const IncompleteArrayType *>(T.getTypePtr());
      ElemType = IA->getElementType();
    }
    if (ElemType) {
      T = QualType(Context.getPointerType(ElemType), Qualifier::None);
    }
  }

  // 4. Function decay to function pointer
  if (T->isFunctionType()) {
    T = QualType(Context.getPointerType(T.getTypePtr()), Qualifier::None);
  }

  return T;
}

QualType TypeDeduction::deduceAutoRefType(QualType DeclaredType, Expr *Init) {
  if (!Init) return QualType();

  QualType T = Init->getType();
  if (T.isNull()) return QualType();

  // auto& binds to lvalues only; binding to rvalues is ill-formed
  // unless the reference is const-qualified (const auto&).
  // However, const auto& is handled by deduceConstAutoRefType.
  // Here we handle plain auto& and auto& with other qualifiers.
  if (Init->isRValue() && !DeclaredType.isConstQualified()) {
    if (Diags) {
      Diags->report(Init->getLocation(),
                    DiagID::err_non_const_lvalue_ref_binds_to_rvalue);
    }
    return QualType();
  }

  // auto& preserves the type including CV qualifiers
  // For auto&, the deduced type is the init type with reference added.
  // Unlike plain auto, auto& does NOT strip top-level CV qualifiers
  // from the initializer — they are preserved.
  //
  // Per C++ [dcl.spec.auto]:
  //   auto& x = e;  →  T& where T is the type of e (without decay)
  //   const auto& x = e;  →  const T& (can bind to rvalues)
  return QualType(Context.getLValueReferenceType(T.getTypePtr()),
                  T.getQualifiers());
}

QualType TypeDeduction::deduceAutoForwardingRefType(Expr *Init) {
  if (!Init) return QualType();

  QualType T = Init->getType();
  if (T.isNull()) return QualType();

  // auto&& (forwarding reference / universal reference)
  // If init is lvalue → deduce as lvalue reference (T&)
  // If init is rvalue (xvalue or prvalue) → deduce as rvalue reference (T&&)
  if (Init->isLValue()) {
    return QualType(Context.getLValueReferenceType(T.getTypePtr()),
                    Qualifier::None);
  }
  return QualType(Context.getRValueReferenceType(T.getTypePtr()),
                  Qualifier::None);
}

QualType TypeDeduction::deduceAutoPointerType(Expr *Init) {
  if (!Init) return QualType();

  QualType T = Init->getType();
  if (T.isNull()) return QualType();

  // auto* x = &init; → deduce the pointee type
  if (T->isPointerType()) {
    auto *PtrTy = static_cast<const PointerType *>(T.getTypePtr());
    return QualType(PtrTy->getPointeeType(), T.getQualifiers());
  }

  // If init is not a pointer, error
  return QualType();
}

QualType TypeDeduction::deduceReturnType(Expr *ReturnExpr) {
  if (!ReturnExpr) return Context.getVoidType();

  QualType T = ReturnExpr->getType();
  if (T.isNull()) return QualType();

  // Strip reference for return type (auto return follows same rules as auto)
  return deduceAutoType(Context.getAutoType(), ReturnExpr);
}

QualType TypeDeduction::deduceFromInitList(llvm::ArrayRef<Expr *> Inits) {
  if (Inits.empty()) return QualType();

  // auto x = {1, 2, 3} → deduce from the first element
  // All elements must have the same type
  QualType FirstType = Inits[0]->getType();
  if (FirstType.isNull()) return QualType();

  // Verify all elements have the same type
  for (unsigned i = 1; i < Inits.size(); ++i) {
    QualType T = Inits[i]->getType();
    if (T.isNull() || T.getCanonicalType() != FirstType.getCanonicalType()) {
      // Inconsistent types in initializer list
      return QualType();
    }
  }

  return FirstType;
}

//===----------------------------------------------------------------------===//
// decltype deduction
//===----------------------------------------------------------------------===//

QualType TypeDeduction::deduceDecltypeType(Expr *E) {
  if (!E) return QualType();

  QualType T = E->getType();
  if (T.isNull()) return QualType();

  // decltype rules (C++ [dcl.type.decltype]):
  // - decltype(id) → declared type of id (no reference stripping)
  // - decltype(expr) → type of expr, preserving value category:
  //   - xvalue → T&&
  //   - lvalue → T&
  //   - prvalue → T
  if (E->isLValue()) {
    return QualType(Context.getLValueReferenceType(T.getTypePtr()),
                    T.getQualifiers());
  }
  if (E->isXValue()) {
    return QualType(Context.getRValueReferenceType(T.getTypePtr()),
                    T.getQualifiers());
  }
  // prvalue: return T as-is
  return T;
}

QualType TypeDeduction::deduceDecltypeDoubleParenType(Expr *E) {
  if (!E) return QualType();

  QualType T = E->getType();
  if (T.isNull()) return QualType();

  // C++ [dcl.type.decltype]p1:
  // decltype((x)) where x is an id-expression:
  //   - If x is an lvalue → T&
  //   - If x is an xvalue → T&&
  //   - If x is a prvalue → T
  //
  // The key difference from decltype(x): a parenthesized id-expression
  // is always an lvalue (even if the variable was declared as a prvalue).
  // For a bare name `x`, decltype(x) returns the declared type.
  // For `decltype((x))`, the inner (x) is an lvalue expression, so
  // the result is always a reference type unless x is a prvalue.
  //
  // In practice, the parser should set the value kind correctly:
  // - A DeclRefExpr for a variable is an lvalue
  // - A parenthesized expression preserves the value kind
  // So we just use the same logic as deduceDecltypeType, but the
  // parser guarantees that (x) is treated as an lvalue expression.

  if (E->isLValue()) {
    // (x) is an lvalue → T&
    return QualType(Context.getLValueReferenceType(T.getTypePtr()),
                    T.getQualifiers());
  }
  if (E->isXValue()) {
    // (x) is an xvalue → T&&
    return QualType(Context.getRValueReferenceType(T.getTypePtr()),
                    T.getQualifiers());
  }
  // prvalue: return T as-is (rare for parenthesized id-expressions)
  return T;
}

QualType TypeDeduction::deduceDecltypeAutoType(Expr *E) {
  if (!E) return QualType();

  // decltype(auto) follows decltype rules but for auto deduction context.
  // Per C++ [dcl.spec.auto]:
  //   If the declared type is decltype(auto), the type deduced is:
  //   - decltype(e) where e is the initializer expression
  //
  // This means:
  //   decltype(auto) x = expr;  →  type is decltype(expr)
  //   decltype(auto) x = 42;    →  int (prvalue → no reference)
  //   decltype(auto) x = var;   →  int (prvalue if var is read)
  //   decltype(auto) x = (var); →  int& (lvalue → reference)
  return deduceDecltypeType(E);
}

QualType TypeDeduction::deduceConstAutoRefType(Expr *Init) {
  if (!Init) return QualType();

  QualType T = Init->getType();
  if (T.isNull()) return QualType();

  // const auto& x = init;
  // const auto& can bind to both lvalues and rvalues.
  // The deduced type is const T& where T is the decayed type of init.
  //
  // Per C++ [dcl.spec.auto]:
  //   For const auto&, we first deduce auto as the pointee type
  //   (applying decay rules), then add const&.

  // 1. Strip top-level reference from init type
  if (T->isReferenceType()) {
    auto *RefTy = static_cast<const ReferenceType *>(T.getTypePtr());
    T = QualType(RefTy->getReferencedType(), T.getQualifiers());
  }

  // 2. Array decay to pointer
  if (T->isArrayType()) {
    const Type *ElemType = nullptr;
    if (T->getTypeClass() == TypeClass::ConstantArray) {
      auto *CA = static_cast<const ConstantArrayType *>(T.getTypePtr());
      ElemType = CA->getElementType();
    } else if (T->getTypeClass() == TypeClass::IncompleteArray) {
      auto *IA = static_cast<const IncompleteArrayType *>(T.getTypePtr());
      ElemType = IA->getElementType();
    }
    if (ElemType) {
      T = QualType(Context.getPointerType(ElemType), Qualifier::None);
    }
  }

  // 3. Function decay to function pointer
  if (T->isFunctionType()) {
    T = QualType(Context.getPointerType(T.getTypePtr()), Qualifier::None);
  }

  // 4. Build const T&
  QualType ConstT = T.withConst();
  return QualType(Context.getLValueReferenceType(ConstT.getTypePtr()),
                  Qualifier::None);
}

QualType TypeDeduction::deduceVolatileAutoRefType(Expr *Init) {
  if (!Init) return QualType();

  QualType T = Init->getType();
  if (T.isNull()) return QualType();

  // volatile auto& x = init;
  // Similar to const auto&, but adds volatile instead of const.
  // volatile auto& can only bind to lvalues (like auto&).

  if (Init->isRValue()) {
    if (Diags) {
      Diags->report(Init->getLocation(),
                    DiagID::err_non_const_lvalue_ref_binds_to_rvalue);
    }
    return QualType();
  }

  // Strip reference
  if (T->isReferenceType()) {
    auto *RefTy = static_cast<const ReferenceType *>(T.getTypePtr());
    T = QualType(RefTy->getReferencedType(), T.getQualifiers());
  }

  QualType VolatileT = T.withVolatile();
  return QualType(Context.getLValueReferenceType(VolatileT.getTypePtr()),
                  Qualifier::None);
}

QualType TypeDeduction::deduceCVAutoRefType(Expr *Init) {
  if (!Init) return QualType();

  QualType T = Init->getType();
  if (T.isNull()) return QualType();

  // const volatile auto& x = init;
  // Can bind to both lvalues and rvalues (const allows rvalue binding).
  // Same decay rules as const auto&.

  if (T->isReferenceType()) {
    auto *RefTy = static_cast<const ReferenceType *>(T.getTypePtr());
    T = QualType(RefTy->getReferencedType(), T.getQualifiers());
  }

  if (T->isArrayType()) {
    const Type *ElemType = nullptr;
    if (T->getTypeClass() == TypeClass::ConstantArray) {
      auto *CA = static_cast<const ConstantArrayType *>(T.getTypePtr());
      ElemType = CA->getElementType();
    } else if (T->getTypeClass() == TypeClass::IncompleteArray) {
      auto *IA = static_cast<const IncompleteArrayType *>(T.getTypePtr());
      ElemType = IA->getElementType();
    }
    if (ElemType) {
      T = QualType(Context.getPointerType(ElemType), Qualifier::None);
    }
  }

  if (T->isFunctionType()) {
    T = QualType(Context.getPointerType(T.getTypePtr()), Qualifier::None);
  }

  QualType CVT = T.withConst().withVolatile();
  return QualType(Context.getLValueReferenceType(CVT.getTypePtr()),
                  Qualifier::None);
}

QualType TypeDeduction::deduceConstAutoForwardingRefType(Expr *Init) {
  if (!Init) return QualType();

  QualType T = Init->getType();
  if (T.isNull()) return QualType();

  // const auto&& x = init;
  // const auto&& is an rvalue reference (NOT a forwarding reference),
  // because the const qualifier prevents forwarding reference deduction.
  // It can only bind to rvalues.
  if (Init->isLValue()) {
    if (Diags) {
      Diags->report(Init->getLocation(),
                    DiagID::err_non_const_lvalue_ref_binds_to_rvalue);
    }
    return QualType();
  }

  // Strip reference
  if (T->isReferenceType()) {
    auto *RefTy = static_cast<const ReferenceType *>(T.getTypePtr());
    T = QualType(RefTy->getReferencedType(), T.getQualifiers());
  }

  QualType ConstT = T.withConst();
  return QualType(Context.getRValueReferenceType(ConstT.getTypePtr()),
                  Qualifier::None);
}

QualType TypeDeduction::deduceReturnTypeDecltypeAuto(Expr *ReturnExpr) {
  if (!ReturnExpr) return Context.getVoidType();

  // decltype(auto) return type deduction:
  // Per C++ [dcl.spec.auto]:
  //   If the return type is decltype(auto), the deduced type is
  //   decltype(return-expr).
  //
  // This preserves value category:
  //   decltype(auto) f() { return x; }  →  decltype(x) = T (declared type)
  //   decltype(auto) f() { return (x); } → decltype((x)) = T& (lvalue ref)
  //   decltype(auto) f() { return 42; }  → decltype(42) = int
  return deduceDecltypeType(ReturnExpr);
}

QualType TypeDeduction::deduceStructuredBindingType(QualType EType,
                                                     unsigned Index) {
  if (EType.isNull()) return QualType();

  // C++ [dcl.struct.bind] structured binding type deduction:
  //
  // Case 1: Array type — each binding is an lvalue reference to the element
  //   auto [a, b] = arr;  →  a is arr[0] (type: element_type&)
  if (EType->isArrayType()) {
    const auto *AT = static_cast<const ArrayType *>(EType.getTypePtr());
    QualType ElemType(AT->getElementType(), Qualifier::None);
    return QualType(Context.getLValueReferenceType(ElemType.getTypePtr()),
                    Qualifier::None);
  }

  // Case 2: Record type (aggregate or tuple-like)
  //   For aggregate: each binding references the corresponding field
  //   For tuple-like: uses std::tuple_element<i>::type
  if (EType->isRecordType()) {
    const auto *RT = static_cast<const RecordType *>(EType.getTypePtr());
    auto *RD = RT->getDecl();
    if (!RD) return QualType();

    // Try to get the Index-th field
    unsigned FieldIdx = 0;
    for (auto *D : RD->fields()) {
      if (FieldIdx == Index) {
        QualType FieldType = D->getType();
        // For non-reference binding, return lvalue reference to field
        return QualType(Context.getLValueReferenceType(FieldType.getTypePtr()),
                        FieldType.getQualifiers());
      }
      ++FieldIdx;
    }

    // Index out of range for aggregate
    return QualType();
  }

  // Case 3: Reference type — look through to the referenced type
  if (EType->isReferenceType()) {
    auto *RefTy = static_cast<const ReferenceType *>(EType.getTypePtr());
    QualType RefdType(RefTy->getReferencedType(), EType.getQualifiers());
    return deduceStructuredBindingType(RefdType, Index);
  }

  return QualType();
}

//===----------------------------------------------------------------------===//
// Template argument deduction (placeholder)
//===----------------------------------------------------------------------===//

bool TypeDeduction::deduceTemplateArguments(
    TemplateDecl *Template,
    llvm::ArrayRef<Expr *> Args,
    llvm::SmallVectorImpl<TemplateArgument> &DeducedArgs) {
  if (!Template || !SemaRef) {
    return false;
  }

  // Delegate to TemplateDeduction::DeduceFunctionTemplateArguments
  // if the template is a FunctionTemplateDecl.
  if (auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(Template)) {
    TemplateDeductionInfo Info;
    TemplateDeductionResult Result =
        SemaRef->getTemplateDeduction().DeduceFunctionTemplateArguments(
            FTD, Args, Info);

    if (Result == TemplateDeductionResult::Success) {
      // Extract deduced arguments from Info
      auto Params = FTD->getTemplateParameterList();
      if (Params) {
        auto Deduced = Info.getDeducedArgs(Params->size());
        for (auto &Arg : Deduced) {
          DeducedArgs.push_back(Arg);
        }
      }
      return true;
    }
    return false;
  }

  // For non-function templates (class templates, etc.), we cannot
  // deduce from call arguments. Return false.
  return false;
}

} // namespace blocktype
