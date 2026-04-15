//===--- UnicodeVersion.h - Unicode Version Info --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#pragma once

namespace blocktype {
namespace unicode {

/// Unicode version information
/// This is the version of Unicode data embedded in the compiler.
constexpr const char* UNICODE_VERSION = "17.0.0";
constexpr unsigned UNICODE_VERSION_MAJOR = 17;
constexpr unsigned UNICODE_VERSION_MINOR = 0;
constexpr unsigned UNICODE_VERSION_PATCH = 0;

/// Get Unicode version string
inline const char* getUnicodeVersion() {
  return UNICODE_VERSION;
}

} // namespace unicode
} // namespace blocktype
