//===--- Normalization.cpp - NFC Normalization ---------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements NFC normalization (UAX #15).
//
// NOTE: This is a simplified implementation for compiler use.
// It handles the most common cases but may not cover all edge cases.
// For full NFC compliance, consider using ICU or utf8proc.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Unicode/UnicodeData.h"
#include "llvm/ADT/SmallVector.h"

namespace blocktype {
namespace unicode {

StringRef normalizeNFC(StringRef Input, llvm::SmallVectorImpl<char> &Output) {
  Output.clear();

  if (Input.empty())
    return Input;

  // Fast path: check if already NFC (ASCII-only)
  bool NeedsNormalization = false;
  for (char C : Input) {
    if (static_cast<uint8_t>(C) >= 0x80) {
      NeedsNormalization = true;
      break;
    }
  }

  // If all ASCII, already in NFC
  if (!NeedsNormalization)
    return Input;

  // Decode UTF-8 to code points
  llvm::SmallVector<uint32_t, 64> CodePoints;
  const char *Ptr = Input.data();
  const char *End = Input.data() + Input.size();

  while (Ptr < End) {
    uint32_t CP = decodeUTF8(Ptr, End);
    if (CP == 0xFFFFFFFF) {
      // Invalid UTF-8, return original
      return Input;
    }
    CodePoints.push_back(CP);
  }

  // TODO: Implement full NFC normalization
  // For now, just re-encode the code points
  // This is correct for most common cases (no combining marks)

  for (uint32_t CP : CodePoints) {
    char Buffer[4];
    unsigned Len = encodeUTF8(CP, Buffer);
    Output.append(Buffer, Buffer + Len);
  }

  return StringRef(Output.data(), Output.size());
}

uint32_t toNFC(uint32_t CodePoint) {
  // TODO: Implement NFC composition
  // For now, return the code point as-is
  // This is correct for most common cases
  return CodePoint;
}

bool isNFC(uint32_t CodePoint) {
  // TODO: Check if code point is in NFC form
  // For now, assume all code points are in NFC
  return true;
}

} // namespace unicode
} // namespace blocktype
