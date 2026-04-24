#include <cassert>

#include "blocktype/IR/IRConstant.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRValue.h"

using namespace blocktype::ir;

int main() {
  IRTypeContext Ctx;
  auto* Int32 = Ctx.getInt32Ty();

  auto* CI = new IRConstantInt(static_cast<IRIntegerType*>(Int32), 42u);
  assert(CI->getZExtValue() == 42);

  auto* V1 = new IRConstantInt(static_cast<IRIntegerType*>(Int32), 1u);
  auto* V2 = new IRConstantInt(static_cast<IRIntegerType*>(Int32), 2u);

  auto* Add = new IRInstruction(Opcode::Add, Int32, 0);
  Add->addOperand(V1);
  Add->addOperand(V2);
  assert(Add->getOperand(0) == V1);
  assert(Add->getOperand(1) == V2);
  assert(Add->getNumOperands() == 2);

  auto* Op0Use = &Add->operands()[0];
  assert(Op0Use->getUser() == Add);
  assert(Op0Use->get() == V1);
  assert(V1->getNumUses() == 1);

  auto* Undef1 = IRConstantUndef::get(Int32);
  auto* Undef2 = IRConstantUndef::get(Int32);
  assert(Undef1 == Undef2);

  APInt Val64(64, 100);
  assert(Val64.getBitWidth() == 64);
  assert(Val64.getZExtValue() == 100);
  assert(Val64.isZero() == false);

  APInt Zero = APInt::getZero(32);
  assert(Zero.isZero() == true);
  assert(Zero.getBitWidth() == 32);

  APInt AllOnes = APInt::getAllOnes(8);
  assert(AllOnes.isAllOnesValue() == true);

  APInt A(32, 10);
  APInt B(32, 3);
  assert((A + B).getZExtValue() == 13);
  assert((A - B).getZExtValue() == 7);
  assert((A * B).getZExtValue() == 30);
  assert(A.udiv(B).getZExtValue() == 3);
  assert(A.urem(B).getZExtValue() == 1);

  APInt Shifted = APInt(32, 1) << 4;
  assert(Shifted.getZExtValue() == 16);

  APInt Trunc = APInt(32, 0xFF).trunc(8);
  assert(Trunc.getBitWidth() == 8);
  assert(Trunc.getZExtValue() == 0xFF);

  APInt Extended = APInt(8, 0xFF).zext(32);
  assert(Extended.getBitWidth() == 32);
  assert(Extended.getZExtValue() == 0xFF);

  APInt SignExt = APInt(8, 0xFF).sext(32);
  assert(SignExt.getBitWidth() == 32);
  assert(SignExt.getSExtValue() == -1);

  APFloat F(APFloat::Semantics::Double, 3.14);
  assert(F.getSemantics() == APFloat::Semantics::Double);
  assert(F.isZero() == false);
  assert(F.isNaN() == false);

  APFloat FHalf(APFloat::Semantics::Half, 1.0);
  assert(FHalf.getBitWidth() == 16);

  APFloat FSum = APFloat(APFloat::Semantics::Double, 1.5) + APFloat(APFloat::Semantics::Double, 2.5);
  assert(FSum.getRawValue() == 4.0);

  return 0;
}
