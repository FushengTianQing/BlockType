#ifndef BLOCKTYPE_IR_IRFORMAT_H
#define BLOCKTYPE_IR_IRFORMAT_H

#include <cstdint>
#include <string>

namespace blocktype {
namespace ir {

struct IRFormatVersion {
  uint16_t Major;
  uint16_t Minor;
  uint16_t Patch;

  static constexpr IRFormatVersion Current() { return {1, 0, 0}; }

  bool isCompatibleWith(IRFormatVersion ReaderVersion) const {
    if (Major != ReaderVersion.Major) return false;
    if (ReaderVersion.Minor < Minor) return false;
    return true;
  }

  std::string toString() const;
};

#pragma pack(push, 1)
struct IRFileHeader {
  char Magic[4] = {'B', 'T', 'I', 'R'};
  IRFormatVersion Version = IRFormatVersion::Current();
  uint32_t Flags = 0;
  uint32_t ModuleOffset = 0;
  uint32_t StringTableOffset = 0;
  uint32_t StringTableSize = 0;
};
#pragma pack(pop)

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRFORMAT_H
