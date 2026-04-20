
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
    } else {
      // Named capture: [x] or [&x]
      // Lookup the variable in current scope
      NamedDecl *CapturedDecl = nullptr;
      // For now, we need to find the VarDecl from the capture name
      // This requires access to Sema's symbol table, which we don't have here
      // So we'll use a simplified approach: assume the variable is in local scope
      
      // TODO: Properly lookup captured variable from Sema context
      // For now, skip initialization (will be zero)
      CaptureValue = nullptr;
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
