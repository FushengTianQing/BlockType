#pragma once

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace blocktype {

/// UTF-8 validation and processing with SIMD optimization
///
/// This class provides high-performance UTF-8 validation using SIMD
/// instructions (SSE2 on x86, NEON on ARM) with scalar fallback.
class UTF8Validator {
public:
  /// Validate a UTF-8 sequence
  /// Returns true if the sequence is valid UTF-8
  static bool validate(llvm::StringRef Str) {
    return validate(Str.data(), Str.size());
  }
  
  /// Validate a UTF-8 sequence
  static bool validate(const char *Ptr, size_t Length);
  
  /// Find the first invalid UTF-8 byte
  /// Returns pointer to the first invalid byte, or End if all valid
  static const char* findInvalid(const char *Start, const char *End);
  
  /// Check if a byte is a valid UTF-8 lead byte
  static bool isLeadByte(uint8_t Byte) {
    // ASCII or lead byte of multi-byte sequence
    return (Byte & 0xC0) != 0x80;
  }
  
  /// Check if a byte is a UTF-8 continuation byte (10xxxxxx)
  static bool isContinuationByte(uint8_t Byte) {
    return (Byte & 0xC0) == 0x80;
  }
  
  /// Get the expected byte count for a UTF-8 lead byte
  static unsigned getExpectedByteCount(uint8_t LeadByte) {
    if ((LeadByte & 0x80) == 0) return 1;      // 0xxxxxxx
    if ((LeadByte & 0xE0) == 0xC0) return 2;   // 110xxxxx
    if ((LeadByte & 0xF0) == 0xE0) return 3;   // 1110xxxx
    if ((LeadByte & 0xF8) == 0xF0) return 4;   // 11110xxx
    return 0; // Invalid lead byte
  }

private:
  //===------------------------------------------------------------------===//
  // Platform-specific implementations
  //===------------------------------------------------------------------===//
  
#if defined(__SSE2__)
  /// SSE2-optimized validation (x86/x86_64)
  static bool validateSSE2(const char *Ptr, size_t Length);
  static const char* findInvalidSSE2(const char *Start, const char *End);
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  /// NEON-optimized validation (ARM/AArch64)
  static bool validateNEON(const char *Ptr, size_t Length);
  static const char* findInvalidNEON(const char *Start, const char *End);
#endif
  
  /// Scalar fallback implementation
  static bool validateScalar(const char *Ptr, size_t Length);
  static const char* findInvalidScalar(const char *Start, const char *End);
};

} // namespace blocktype
