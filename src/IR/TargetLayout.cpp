#include "blocktype/IR/TargetLayout.h"
#include "blocktype/IR/IRType.h"

#include <algorithm>
#include <cassert>

namespace blocktype {
namespace ir {

TargetLayout::TargetLayout(const std::string& TargetTriple)
  : TripleStr(TargetTriple) {
  PointerSize = 8;
  PointerAlign = 8;
  IntSize = 4;
  LongSize = 8;
  LongLongSize = 8;
  FloatSize = 4;
  DoubleSize = 8;
  LongDoubleSize = 16;
  MaxVectorAlign = 16;
  IsLittleEndian = true;

  if (TargetTriple.find("apple") != std::string::npos ||
      TargetTriple.find("macos") != std::string::npos ||
      TargetTriple.find("darwin") != std::string::npos) {
    IsMacOS = true;
    IsLinux = false;
  } else {
    IsMacOS = false;
    IsLinux = true;
  }

  (void)IsLinux;
}

uint64_t TargetLayout::getTypeSizeInBits(IRType* T) const {
  return T->getSizeInBits(*this);
}

uint64_t TargetLayout::getTypeAlignInBits(IRType* T) const {
  return T->getAlignInBits(*this);
}

std::unique_ptr<TargetLayout> TargetLayout::Create(const std::string& Triple) {
  return std::make_unique<TargetLayout>(Triple);
}

} // namespace ir
} // namespace blocktype
