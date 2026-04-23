//===--- LambdaAnalysis.cpp - Lambda Sema Analysis ----------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements lambda expression semantic analysis:
// - Capture variable ODR-use checking
// - Init capture type deduction
// - Generic lambda template parameter deduction
// - Lambda to function pointer conversion
//
// Per audit P1-5: Lambda Sema was ~70% complete, missing ODR-use
// checking, init capture deduction, and generic lambda support.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Sema/LambdaAnalysis.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Sema/TypeDeduction.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Basic/DiagnosticIDs.h"

#include "llvm/Support/Casting.h"

namespace blocktype {

//===----------------------------------------------------------------------===//
// LambdaAnalysis implementation
//===----------------------------------------------------------------------===//

LambdaAnalysis::LambdaAnalysis(Sema &S)
    : SemaRef(S), Context(S.getASTContext()) {}

//===----------------------------------------------------------------------===//
// Capture variable ODR-use checking
//===----------------------------------------------------------------------===//

bool LambdaAnalysis::CheckCaptureODRUse(LambdaExpr *Lambda) {
  if (!Lambda) return false;

  bool HasErrors = false;

  for (const auto &Capture : Lambda->getCaptures()) {
    // Skip init captures — they create new variables, not ODR-use
    if (Capture.Kind == LambdaCapture::InitCopy)
      continue;

    // Find the captured variable declaration
    NamedDecl *CapturedDecl = Capture.CapturedDecl;
    if (!CapturedDecl) {
      // Try to look up the variable by name
      CapturedDecl = SemaRef.LookupName(Capture.Name);
    }

    if (!CapturedDecl) continue;

    auto *VD = llvm::dyn_cast<VarDecl>(CapturedDecl);
    if (!VD) continue;

    // Per C++ [expr.prim.lambda.capture]:
    //   An entity captured by reference must have automatic storage duration
    //   and must be odr-usable in the lambda's context.
    //
    //   An entity captured by copy must be odr-usable or be a reference
    //   that refers to a variable with automatic storage duration.

    if (Capture.Kind == LambdaCapture::ByRef) {
      // Reference capture: the variable must have automatic storage duration
      // and must still be alive when the lambda is invoked.
      // Static variables should not be captured by reference in a lambda
      // that might outlive the variable (though this is technically allowed,
      // it's a common source of bugs).
      if (VD->isStatic()) {
        // Static variables don't need capture at all — they're always accessible
        // Warn about unnecessary capture
        SemaRef.Diag(Capture.Loc, DiagID::warn_lambda_capture_static,
                     Capture.Name);
        continue;
      }

      // Check if the variable has automatic storage duration
      // (i.e., is a local variable, not a global or namespace-scope variable)
      if (!isLocalVariable(VD)) {
        SemaRef.Diag(Capture.Loc, DiagID::err_lambda_capture_odr_use,
                     Capture.Name);
        HasErrors = true;
      }
    }
    // ByCopy: always valid for odr-usable variables
  }

  return !HasErrors;
}

//===----------------------------------------------------------------------===//
// Init capture type deduction
//===----------------------------------------------------------------------===//

QualType LambdaAnalysis::DeduceInitCaptureType(const LambdaCapture &Capture) {
  if (Capture.Kind != LambdaCapture::InitCopy)
    return QualType();

  if (!Capture.InitExpr)
    return QualType();

  // Per C++ [expr.prim.lambda.capture]:
  //   For an init-capture like `x = expr`, the type of x is deduced
  //   from the type of expr using auto deduction rules.
  //
  //   - `x = expr` → auto x = expr; (copy capture, deduced as auto)
  //   - `x = expr` → auto x = expr; (same as auto variable declaration)
  //   - `&x = expr` → auto& x = expr; (reference capture)

  QualType InitType = Capture.InitExpr->getType();
  if (InitType.isNull()) {
    SemaRef.Diag(Capture.Loc, DiagID::err_lambda_init_capture_deduction_failed,
                 Capture.Name);
    return QualType();
  }

  // Use TypeDeduction to deduce the auto type
  TypeDeduction TD(Context);

  // For init captures, we use auto deduction rules
  QualType DeducedType = TD.deduceAutoType(Context.getAutoType(),
                                             Capture.InitExpr);
  if (DeducedType.isNull()) {
    SemaRef.Diag(Capture.Loc, DiagID::err_lambda_init_capture_deduction_failed,
                 Capture.Name);
    return QualType();
  }

  return DeducedType;
}

//===----------------------------------------------------------------------===//
// Generic lambda support
//===----------------------------------------------------------------------===//

bool LambdaAnalysis::CheckGenericLambda(LambdaExpr *Lambda) {
  if (!Lambda) return false;

  TemplateParameterList *TPL = Lambda->getTemplateParameters();
  if (!TPL) return true; // Not a generic lambda, nothing to check

  auto Params = TPL->getParams();
  if (Params.empty()) {
    SemaRef.Diag(Lambda->getLocation(),
                 DiagID::err_lambda_generic_no_auto_param);
    return false;
  }

  // Check that at least one parameter has auto type
  // (generic lambda requires at least one auto parameter)
  bool HasAutoParam = false;
  for (auto *Param : Lambda->getParams()) {
    QualType ParamType = Param->getType();
    if (!ParamType.isNull() &&
        ParamType->getTypeClass() == TypeClass::Auto) {
      HasAutoParam = true;
      break;
    }
  }

  if (!HasAutoParam) {
    // A lambda with template parameters but no auto parameters
    // is technically valid (it could use the template params in its body),
    // but it's unusual. We allow it but don't require auto params.
  }

  return true;
}

QualType LambdaAnalysis::DeduceGenericLambdaCallOperatorType(
    LambdaExpr *Lambda, llvm::ArrayRef<Expr *> CallArgs) {
  if (!Lambda) return QualType();

  TemplateParameterList *TPL = Lambda->getTemplateParameters();
  if (!TPL) return QualType();

  // For a generic lambda like:
  //   auto f = [](auto x, auto y) { return x + y; };
  // When called with f(1, 2.0), we need to deduce:
  //   x → int, y → double
  //
  // This is done by matching the call arguments against the
  // lambda's auto parameters.

  llvm::SmallVector<ParmVarDecl *, 4> LambdaParams;
  for (auto *P : Lambda->getParams()) {
    LambdaParams.push_back(P);
  }

  // Deduce template arguments from call arguments
  llvm::SmallVector<TemplateArgument, 4> DeducedArgs;
  unsigned AutoParamIndex = 0;

  for (unsigned I = 0; I < CallArgs.size() && I < LambdaParams.size(); ++I) {
    QualType ParamType = LambdaParams[I]->getType();

    // If the parameter is auto, deduce from the call argument
    if (!ParamType.isNull() &&
        ParamType->getTypeClass() == TypeClass::Auto) {
      QualType ArgType = CallArgs[I]->getType();
      if (!ArgType.isNull()) {
        DeducedArgs.push_back(TemplateArgument(ArgType));
        ++AutoParamIndex;
      }
    }
  }

  // If we deduced any template arguments, substitute them into the return type.
  // For generic lambdas, the return type may contain auto/TemplateTypeParmType
  // that needs to be resolved from the deduced arguments.
  if (!DeducedArgs.empty() && Lambda->getReturnType().getTypePtr()) {
    TemplateInstantiation Inst;
    TemplateParameterList *TPL = Lambda->getTemplateParameters();
    if (TPL) {
      auto TPLParams = TPL->getParams();
      for (unsigned I = 0; I < std::min(DeducedArgs.size(), TPLParams.size()); ++I) {
        if (auto *ParamDecl = llvm::dyn_cast_or_null<TemplateTypeParmDecl>(TPLParams[I])) {
          Inst.addSubstitution(ParamDecl, DeducedArgs[I]);
        }
      }
    }
    QualType SubstReturnType = Inst.substituteType(Lambda->getReturnType());
    if (!SubstReturnType.isNull()) {
      return SubstReturnType;
    }
  }

  // Fallback: return the lambda's declared return type as-is
  return Lambda->getReturnType();
}

//===----------------------------------------------------------------------===//
// Lambda to function pointer conversion
//===----------------------------------------------------------------------===//

QualType LambdaAnalysis::GetLambdaConversionFunctionType(
    LambdaExpr *Lambda) const {
  if (!Lambda) return QualType();

  // Per C++ [expr.prim.lambda.closure]:
  //   A non-generic, non-capturing lambda can be converted to a
  //   function pointer with the same signature.
  //
  //   A generic, non-capturing lambda can be converted to a
  //   function pointer template.

  // Check if the lambda has captures
  if (!Lambda->getCaptures().empty()) {
    // Lambda with captures cannot be converted to function pointer
    return QualType();
  }

  // Check if the lambda is generic
  if (Lambda->getTemplateParameters()) {
    // Generic lambda — conversion produces a template, not a simple
    // function pointer. For now, return null.
    return QualType();
  }

  // Build the function pointer type from the lambda's signature
  QualType RetType = Lambda->getReturnType();
  if (RetType.isNull()) return QualType();

  llvm::SmallVector<const Type *, 4> ParamTypes;
  for (auto *P : Lambda->getParams()) {
    if (!P->getType().isNull()) {
      ParamTypes.push_back(P->getType().getTypePtr());
    }
  }

  // Create function type: RetType (*)(ParamTypes...)
  FunctionType *FT = Context.getFunctionType(
      RetType.getTypePtr(), ParamTypes, false, false, false);
  if (!FT) return QualType();

  // Create pointer-to-function type
  PointerType *PtrFT = Context.getPointerType(FT);
  if (!PtrFT) return QualType();

  return QualType(PtrFT, Qualifier::None);
}

bool LambdaAnalysis::CanConvertToFunctionPointer(LambdaExpr *Lambda) const {
  return !GetLambdaConversionFunctionType(Lambda).isNull();
}

//===----------------------------------------------------------------------===//
// Private helpers
//===----------------------------------------------------------------------===//

bool LambdaAnalysis::isLocalVariable(VarDecl *VD) const {
  if (!VD) return false;

  // A variable has automatic storage duration if:
  //   - It is not static
  //   - It is not at namespace/translation-unit scope
  if (VD->isStatic()) return false;

  // Check if the variable is at translation-unit scope
  // (global/namespace scope variables don't have automatic storage duration)
  DeclContext *DC = VD->getDeclContext();
  if (!DC) return false;

  // Translation unit context → not a local variable
  if (llvm::isa<TranslationUnitDecl>(DC)) return false;

  // Namespace context → not a local variable
  if (llvm::isa<NamespaceDecl>(DC)) return false;

  // Function or block scope → local variable
  return true;
}

} // namespace blocktype
