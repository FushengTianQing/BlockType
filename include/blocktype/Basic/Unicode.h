#pragma once

#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <string>

namespace blocktype {

class Unicode {
public:
  static bool isIDStart(uint32_t CP);
  static bool isIDContinue(uint32_t CP);
  static bool isCJK(uint32_t CP);
  static bool isChinese(uint32_t CP);
  static std::string normalizeNFC(llvm::StringRef Input);
  static unsigned getUTF8ByteLength(uint8_t FirstByte);
  static uint32_t decodeUTF8(const char *&Ptr, const char *End);
  static void encodeUTF8(uint32_t CP, char *&Ptr);
};

} // namespace blocktype