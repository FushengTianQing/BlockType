//===--- UnicodeData.h - Unicode Character Data ----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Unicode character property queries for the compiler.
// Implements UAX #31 (Identifier and Pattern Syntax) and NFC normalization.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include <cstdint>

namespace blocktype {
namespace unicode {

//===----------------------------------------------------------------------===//
// UAX #31: Identifier and Pattern Syntax
//===----------------------------------------------------------------------===//

/// Returns true if the code point can start an identifier (ID_Start).
/// See: https://www.unicode.org/reports/tr31/#Default_Identifier_Syntax
bool isIDStart(uint32_t CodePoint);

/// Returns true if the code point can continue an identifier (ID_Continue).
/// See: https://www.unicode.org/reports/tr31/#Default_Identifier_Syntax
bool isIDContinue(uint32_t CodePoint);

/// Returns true if the code point can start an extended identifier (XID_Start).
/// XID_Start is like ID_Start but excludes pattern syntax characters.
bool isXIDStart(uint32_t CodePoint);

/// Returns true if the code point can continue an extended identifier (XID_Continue).
/// XID_Continue is like ID_Continue but excludes pattern syntax characters.
bool isXIDContinue(uint32_t CodePoint);

//===----------------------------------------------------------------------===//
// NFC Normalization (UAX #15)
//===----------------------------------------------------------------------===//

/// Normalizes a UTF-8 string to NFC form.
/// Returns the normalized string (may reference Input if already normalized).
StringRef normalizeNFC(StringRef Input, llvm::SmallVectorImpl<char> &Output);

/// Returns the NFC form of a single code point.
/// Returns the same code point if already in NFC.
uint32_t toNFC(uint32_t CodePoint);

/// Returns true if the code point is already in NFC form.
bool isNFC(uint32_t CodePoint);

//===----------------------------------------------------------------------===//
// UTF-8 Utilities
//===----------------------------------------------------------------------===//

/// Decodes a UTF-8 code point from a string.
/// Returns the code point and advances the pointer.
/// Returns 0xFFFFFFFF on error.
uint32_t decodeUTF8(const char *&Ptr, const char *End);

/// Encodes a Unicode code point to UTF-8.
/// Returns the number of bytes written (1-4).
unsigned encodeUTF8(uint32_t CodePoint, char *Buffer);

/// Returns the number of UTF-8 bytes needed to encode a code point.
unsigned getUTF8Length(uint32_t CodePoint);

} // namespace unicode
} // namespace blocktype
