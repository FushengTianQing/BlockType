
#include "blocktype/CodeGen/CodeGenFunction.h"
#include "blocktype/CodeGen/CodeGenModule.h"
#include "blocktype/CodeGen/CodeGenTypes.h"
#include "blocktype/AST/Expr.h"
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
  
  // Initialize capture members
  // For now, we zero-initialize the entire struct
  // TODO: Properly initialize each capture member from context
  uint64_t Size = CGM.getDataLayout().getTypeAllocSize(ClosureTy);
  if (Size > 0) {
    Builder.CreateMemSet(ClosureAlloca, 
                        llvm::ConstantInt::get(Builder.getInt8Ty(), 0),
                        Size,
                        llvm::MaybeAlign());
  }
  
  // Return the address of the closure object (this is an lvalue)
  return ClosureAlloca;
}
