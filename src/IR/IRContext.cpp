#include "blocktype/IR/IRContext.h"

namespace blocktype {
namespace ir {

std::string_view IRContext::saveString(std::string_view Str) {
  size_t Len = Str.size();
  char* Mem = static_cast<char*>(Allocator.Allocate(Len + 1, alignof(char)));
  std::copy(Str.begin(), Str.end(), Mem);
  Mem[Len] = '\0';
  return std::string_view(Mem, Len);
}

void IRContext::sealModule(IRModule& M) {
}

} // namespace ir
} // namespace blocktype
