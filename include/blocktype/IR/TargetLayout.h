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
  uint64_t getIntSizeInBits() const { return IntSize * 8; }
  uint64_t getLongSizeInBits() const { return LongSize * 8; }
  uint64_t getLongLongSizeInBits() const { return LongLongSize * 8; }
  uint64_t getFloatSizeInBits() const { return FloatSize * 8; }
  uint64_t getDoubleSizeInBits() const { return DoubleSize * 8; }
  uint64_t getLongDoubleSizeInBits() const { return LongDoubleSize * 8; }
  bool isLittleEndian() const { return IsLittleEndian; }

  static std::unique_ptr<TargetLayout> Create(StringRef Triple);
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_TARGETLAYOUT_H
