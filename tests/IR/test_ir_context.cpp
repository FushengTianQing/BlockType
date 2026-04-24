#include <cassert>

#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRType.h"

using namespace blocktype::ir;

int main() {
  IRContext Ctx;
  auto* IntTy = Ctx.create<IRIntegerType>(32);
  assert(IntTy != nullptr);
  assert(IntTy->getBitWidth() == 32);

  {
    IRContext Ctx2;
    for (int i = 0; i < 1000; ++i)
      Ctx2.create<IRIntegerType>(32);
  }

  bool CleanupCalled = false;
  {
    IRContext Ctx3;
    Ctx3.addCleanup([&]() { CleanupCalled = true; });
  }
  assert(CleanupCalled == true);

  assert(Ctx.getThreadingMode() == IRThreadingMode::SingleThread);
  Ctx.setThreadingMode(IRThreadingMode::MultiInstance);
  assert(Ctx.getThreadingMode() == IRThreadingMode::MultiInstance);

  return 0;
}
