#ifndef BLOCKTYPE_IR_TARGETLAYOUT_H
#define BLOCKTYPE_IR_TARGETLAYOUT_H

#include <cstdint>
#include <memory>
#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRType;

class TargetLayout {
  std::string TripleStr;
  uint64_t PointerSize, PointerAlign;
  uint64_t IntSize, LongSize, LongLongSize;
  uint64_t FloatSize, DoubleSize, LongDoubleSize;
  uint64_t MaxVectorAlign;
  bool IsLittleEndian, IsLinux, IsMacOS;

public:
  explicit TargetLayout(StringRef TargetTriple);

  uint64_t getTypeSizeInBits(IRType* T) const;
  uint64_t getTypeAlignInBits(IRType* T) const;
  uint64_t getPointerSizeInBits() const { return PointerSize * 8; }
  bool isLittleEndian() const { return IsLittleEndian; }

  static std::unique_ptr<TargetLayout> Create(StringRef Triple);
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_TARGETLAYOUT_H
