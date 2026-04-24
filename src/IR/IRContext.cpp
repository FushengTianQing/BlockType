#include "blocktype/IR/IRContext.h"

#include "blocktype/IR/IRModule.h"

namespace blocktype {
namespace ir {

StringRef IRContext::saveString(StringRef Str) {
  size_t Len = Str.size();
  char* Mem = static_cast<char*>(Allocator.Allocate(Len + 1, alignof(char)));
  std::copy(Str.begin(), Str.end(), Mem);
  Mem[Len] = '\0';
  return StringRef(Mem, Len);
}

void IRContext::sealModule(IRModule& M) {
  M.seal();
}

} // namespace ir
} // namespace blocktype
