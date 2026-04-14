#include "blocktype/Basic/Unicode.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

bool Unicode::isIDStart(uint32_t CP) {
  return (CP >= 'A' && CP <= 'Z') || (CP >= 'a' && CP <= 'z') || CP == '_' ||
         (CP >= 0x4E00 && CP <= 0x9FFF); // CJK
}

bool Unicode::isIDContinue(uint32_t CP) {
  return isIDStart(CP) || (CP >= '0' && CP <= '9');
}

bool Unicode::isCJK(uint32_t CP) {
  return (CP >= 0x4E00 && CP <= 0x9FFF) ||
         (CP >= 0x3400 && CP <= 0x4DBF);
}

bool Unicode::isChinese(uint32_t CP) { return isCJK(CP); }

std::string Unicode::normalizeNFC(llvm::StringRef Input) {
  return Input.str(); // 简化实现
}

unsigned Unicode::getUTF8ByteLength(uint8_t FirstByte) {
  if ((FirstByte & 0x80) == 0) return 1;
  if ((FirstByte & 0xE0) == 0xC0) return 2;
  if ((FirstByte & 0xF0) == 0xE0) return 3;
  if ((FirstByte & 0xF8) == 0xF0) return 4;
  return 1;
}

uint32_t Unicode::decodeUTF8(const char *&Ptr, const char *End) {
  if (Ptr >= End) return 0;
  uint8_t First = *Ptr++;
  unsigned Len = getUTF8ByteLength(First);
  if (Len == 1) return First;
  
  uint32_t CP = First & (0x7F >> Len);
  for (unsigned i = 1; i < Len && Ptr < End; ++i) {
    CP = (CP << 6) | (*Ptr++ & 0x3F);
  }
  return CP;
}

void Unicode::encodeUTF8(uint32_t CP, char *&Ptr) {
  if (CP < 0x80) { *Ptr++ = CP; }
  else if (CP < 0x800) {
    *Ptr++ = 0xC0 | (CP >> 6);
    *Ptr++ = 0x80 | (CP & 0x3F);
  } else if (CP < 0x10000) {
    *Ptr++ = 0xE0 | (CP >> 12);
    *Ptr++ = 0x80 | ((CP >> 6) & 0x3F);
    *Ptr++ = 0x80 | (CP & 0x3F);
  } else {
    *Ptr++ = 0xF0 | (CP >> 18);
    *Ptr++ = 0x80 | ((CP >> 12) & 0x3F);
    *Ptr++ = 0x80 | ((CP >> 6) & 0x3F);
    *Ptr++ = 0x80 | (CP & 0x3F);
  }
}

} // namespace blocktype