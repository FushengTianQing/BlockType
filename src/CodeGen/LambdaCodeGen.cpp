
#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Decl.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"

using namespace blocktype;

//===----------------------------------------------------------------------===//
// P7.1.5: Lambda expression code generation
//===----------------------------------------------------------------------===//

llvm::Value *CodeGenFunction::EmitLambdaExpr(LambdaExpr *LE) {
  if (!LE) {
    return nullptr;
  }
  
  auto *ClosureClass = LE->getClosureClass();
  if (!ClosureClass) {
    return nullptr;
  }
  
  // P7.1.5: Register captured variables mapping for this lambda
  // We'll store it in a static map keyed by ClosureClass
  const auto &CapturedMap = LE->getCapturedVarsMap();
  for (const auto &Pair : CapturedMap) {
    // Store in CGM for later retrieval by operator()
    // For now, we'll use a simpler approach: register when emitting operator()
  }
  
  // Get the closure type
  llvm::StructType *ClosureTy = llvm::dyn_cast<llvm::StructType>(
      CGM.getTypes().ConvertType(LE->getType()));
  if (!ClosureTy) {
    return nullptr;
  }
  
  // Create alloca for the closure object on the stack
  llvm::AllocaInst *ClosureAlloca = Builder.CreateAlloca(
      ClosureTy, nullptr, "lambda_closure");
  
  // P7.1.5: Initialize capture members from context
  auto Captures = LE->getCaptures();
  unsigned FieldIndex = 0;
  
  for (const auto &Capture : Captures) {
    // Get the field for this capture
    if (FieldIndex >= ClosureTy->getNumElements()) {
      break; // Safety check
    }
    
    llvm::Value *CaptureValue = nullptr;
    
    if (Capture.Kind == LambdaCapture::InitCopy && Capture.InitExpr) {
      // Init capture: [x = expr] - evaluate the initialization expression
      CaptureValue = EmitExpr(Capture.InitExpr);
    } else if (Capture.CapturedDecl) {
      // Named capture: [x] or [&x] - load from captured variable
      if (auto *CapturedVar = llvm::dyn_cast<VarDecl>(Capture.CapturedDecl)) {
        // Create a DeclRefExpr to the captured variable
        auto &Ctx = CGM.getASTContext();
        auto *DRE = Ctx.create<DeclRefExpr>(CapturedVar->getLocation(), CapturedVar);
        DRE->setType(CapturedVar->getType());
        
        // Get the address using EmitLValue
        llvm::Value *VarAddr = EmitLValue(DRE);
        if (VarAddr) {
          // Load the value
          llvm::Type *ValTy = CGM.getTypes().ConvertType(CapturedVar->getType());
          if (ValTy) {
            CaptureValue = Builder.CreateLoad(ValTy, VarAddr, "capture_load");
          }
        }
      }
    }
    
    // Store the capture value to the corresponding field
    if (CaptureValue) {
      llvm::Value *FieldPtr = Builder.CreateStructGEP(
          ClosureTy, ClosureAlloca, FieldIndex,
          "capture_field_" + std::to_string(FieldIndex));
      Builder.CreateStore(CaptureValue, FieldPtr);
    }
    
    ++FieldIndex;
  }
  
  // Return the address of the closure object (this is an lvalue)
  return ClosureAlloca;
}
