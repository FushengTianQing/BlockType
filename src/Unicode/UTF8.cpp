//===--- UTF8.cpp - UTF-8 Utilities --------------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements UTF-8 encoding and decoding utilities.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Unicode/UnicodeData.h"

namespace blocktype {
namespace unicode {

uint32_t decodeUTF8(const char *&Ptr, const char *End) {
  if (Ptr >= End)
    return 0xFFFFFFFF;

  uint8_t FirstByte = static_cast<uint8_t>(*Ptr++);

  // ASCII (0-127)
  if (FirstByte < 0x80)
    return FirstByte;

  // Continuation byte (invalid as first byte)
  if ((FirstByte & 0xC0) == 0x80)
    return 0xFFFFFFFF;

  // Determine sequence length
  unsigned NumBytes = 0;
  uint32_t CodePoint = 0;

  if ((FirstByte & 0xE0) == 0xC0) {
    // 2-byte sequence: 110xxxxx 10xxxxxx
    NumBytes = 2;
    CodePoint = FirstByte & 0x1F;
  } else if ((FirstByte & 0xF0) == 0xE0) {
    // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
    NumBytes = 3;
    CodePoint = FirstByte & 0x0F;
  } else if ((FirstByte & 0xF8) == 0xF0) {
    // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    NumBytes = 4;
    CodePoint = FirstByte & 0x07;
  } else {
    // Invalid UTF-8 start byte
    return 0xFFFFFFFF;
  }

  // Check that we have enough bytes
  if (Ptr + (NumBytes - 1) > End)
    return 0xFFFFFFFF;

  // Decode continuation bytes
  for (unsigned i = 1; i < NumBytes; ++i) {
    uint8_t Byte = static_cast<uint8_t>(*Ptr++);
    if ((Byte & 0xC0) != 0x80) {
      // Invalid continuation byte
      return 0xFFFFFFFF;
    }
    CodePoint = (CodePoint << 6) | (Byte & 0x3F);
  }

  // Check for overlong encoding
  if (NumBytes == 2 && CodePoint < 0x80)
    return 0xFFFFFFFF;
  if (NumBytes == 3 && CodePoint < 0x800)
    return 0xFFFFFFFF;
  if (NumBytes == 4 && CodePoint < 0x10000)
    return 0xFFFFFFFF;

  // Check for invalid code points
  if (CodePoint > 0x10FFFF)
    return 0xFFFFFFFF;
  if (CodePoint >= 0xD800 && CodePoint <= 0xDFFF)
    return 0xFFFFFFFF; // Surrogate pairs

  return CodePoint;
}

unsigned encodeUTF8(uint32_t CodePoint, char *Buffer) {
  if (CodePoint < 0x80) {
    // ASCII
    Buffer[0] = static_cast<char>(CodePoint);
    return 1;
  } else if (CodePoint < 0x800) {
    // 2-byte sequence
    Buffer[0] = static_cast<char>(0xC0 | (CodePoint >> 6));
    Buffer[1] = static_cast<char>(0x80 | (CodePoint & 0x3F));
    return 2;
  } else if (CodePoint < 0x10000) {
    // 3-byte sequence
    Buffer[0] = static_cast<char>(0xE0 | (CodePoint >> 12));
    Buffer[1] = static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
    Buffer[2] = static_cast<char>(0x80 | (CodePoint & 0x3F));
    return 3;
  } else if (CodePoint < 0x110000) {
    // 4-byte sequence
    Buffer[0] = static_cast<char>(0xF0 | (CodePoint >> 18));
    Buffer[1] = static_cast<char>(0x80 | ((CodePoint >> 12) & 0x3F));
    Buffer[2] = static_cast<char>(0x80 | ((CodePoint >> 6) & 0x3F));
    Buffer[3] = static_cast<char>(0x80 | (CodePoint & 0x3F));
    return 4;
  }

  // Invalid code point
  return 0;
}

unsigned getUTF8Length(uint32_t CodePoint) {
  if (CodePoint < 0x80)
    return 1;
  if (CodePoint < 0x800)
    return 2;
  if (CodePoint < 0x10000)
    return 3;
  if (CodePoint < 0x110000)
    return 4;
  return 0;
}

} // namespace unicode
} // namespace blocktype
