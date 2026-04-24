#include <cassert>

#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype::ir;

int main() {
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
  assert(Layout->getPointerSizeInBits() == 64);
  assert(Layout->isLittleEndian() == true);

  auto LayoutARM = TargetLayout::Create("aarch64-unknown-linux-gnu");
  assert(LayoutARM->isLittleEndian() == true);

  IRTypeContext Ctx;
  assert(Layout->getTypeSizeInBits(Ctx.getInt32Ty()) == 32);

  return 0;
}
