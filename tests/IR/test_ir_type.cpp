#include <cassert>

#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype::ir;

int main() {
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");

  assert(IRIntegerType(32).getSizeInBits(*Layout) == 32);

  IRTypeContext Ctx;
  auto* Int32_1 = Ctx.getInt32Ty();
  auto* Int32_2 = Ctx.getInt32Ty();
  assert(Int32_1 == Int32_2);

  auto* PtrI8_1 = Ctx.getPointerType(Ctx.getInt8Ty());
  auto* PtrI8_2 = Ctx.getPointerType(Ctx.getInt8Ty());
  assert(PtrI8_1 == PtrI8_2);

  auto* IntTy = Ctx.getInt32Ty();
  assert(IntTy->getKind() == IRType::Integer);
  assert(IntTy->getBitWidth() == 32);

  auto* Float64Ty = Ctx.getDoubleTy();
  assert(Float64Ty->getKind() == IRType::Float);
  assert(Float64Ty->getBitWidth() == 64);

  return 0;
}
