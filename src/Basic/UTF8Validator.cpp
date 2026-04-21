#include "blocktype/Basic/UTF8Validator.h"
#include <cstring>

namespace blocktype {

//===----------------------------------------------------------------------===//
// Public API - dispatch to platform-specific implementation
//===----------------------------------------------------------------------===//

bool UTF8Validator::validate(const char *Ptr, size_t Length) {
  if (Length == 0) return true;
  if (!Ptr) return false;
  
  // Choose the best available implementation
#if defined(__SSE2__)
  return validateSSE2(Ptr, Length);
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
  return validateNEON(Ptr, Length);
#else
  return validateScalar(Ptr, Length);
#endif
}

const char* UTF8Validator::findInvalid(const char *Start, const char *End) {
  if (!Start || !End || Start >= End) return End;
  
#if defined(__SSE2__)
  return findInvalidSSE2(Start, End);
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
  return findInvalidNEON(Start, End);
#else
  return findInvalidScalar(Start, End);
#endif
}

//===----------------------------------------------------------------------===//
// Scalar fallback implementation
//===----------------------------------------------------------------------===//

bool UTF8Validator::validateScalar(const char *Ptr, size_t Length) {
  const unsigned char *Bytes = reinterpret_cast<const unsigned char*>(Ptr);
  size_t i = 0;
  
  while (i < Length) {
    unsigned char Byte = Bytes[i];
    
    // ASCII (0xxxxxxx)
    if ((Byte & 0x80) == 0) {
      i++;
      continue;
    }
    
    // Multi-byte sequence
    unsigned NumBytes = getExpectedByteCount(Byte);
    if (NumBytes == 0 || NumBytes == 1) {
      return false; // Invalid lead byte
    }
    
    // Check if we have enough bytes
    if (i + NumBytes > Length) {
      return false;
    }
    
    // Validate continuation bytes
    for (unsigned j = 1; j < NumBytes; j++) {
      if (!isContinuationByte(Bytes[i + j])) {
        return false;
      }
    }
    
    // Check for overlong encoding
    uint32_t CodePoint = 0;
    switch (NumBytes) {
    case 2:
      CodePoint = ((Byte & 0x1F) << 6) | (Bytes[i+1] & 0x3F);
      if (CodePoint < 0x80) return false; // Overlong
      break;
    case 3:
      CodePoint = ((Byte & 0x0F) << 12) | ((Bytes[i+1] & 0x3F) << 6) | (Bytes[i+2] & 0x3F);
      if (CodePoint < 0x800) return false; // Overlong
      if (CodePoint >= 0xD800 && CodePoint <= 0xDFFF) return false; // Surrogate
      break;
    case 4:
      CodePoint = ((Byte & 0x07) << 18) | ((Bytes[i+1] & 0x3F) << 12) | 
                  ((Bytes[i+2] & 0x3F) << 6) | (Bytes[i+3] & 0x3F);
      if (CodePoint < 0x10000) return false; // Overlong
      if (CodePoint > 0x10FFFF) return false; // Beyond Unicode range
      break;
    }
    
    i += NumBytes;
  }
  
  return true;
}

const char* UTF8Validator::findInvalidScalar(const char *Start, const char *End) {
  const unsigned char *Ptr = reinterpret_cast<const unsigned char*>(Start);
  const unsigned char *EndPtr = reinterpret_cast<const unsigned char*>(End);
  
  while (Ptr < EndPtr) {
    unsigned char Byte = *Ptr;
    
    // ASCII
    if ((Byte & 0x80) == 0) {
      Ptr++;
      continue;
    }
    
    // Multi-byte sequence
    unsigned NumBytes = getExpectedByteCount(Byte);
    if (NumBytes == 0 || NumBytes == 1) {
      return reinterpret_cast<const char*>(Ptr);
    }
    
    // Check if we have enough bytes
    if (Ptr + NumBytes > EndPtr) {
      return reinterpret_cast<const char*>(Ptr);
    }
    
    // Validate continuation bytes
    for (unsigned j = 1; j < NumBytes; j++) {
      if (!isContinuationByte(Ptr[j])) {
        return reinterpret_cast<const char*>(Ptr);
      }
    }
    
    // Check for overlong encoding and invalid code points
    uint32_t CodePoint = 0;
    switch (NumBytes) {
    case 2:
      CodePoint = ((Byte & 0x1F) << 6) | (Ptr[1] & 0x3F);
      if (CodePoint < 0x80) return reinterpret_cast<const char*>(Ptr);
      break;
    case 3:
      CodePoint = ((Byte & 0x0F) << 12) | ((Ptr[1] & 0x3F) << 6) | (Ptr[2] & 0x3F);
      if (CodePoint < 0x800) return reinterpret_cast<const char*>(Ptr);
      if (CodePoint >= 0xD800 && CodePoint <= 0xDFFF) return reinterpret_cast<const char*>(Ptr);
      break;
    case 4:
      CodePoint = ((Byte & 0x07) << 18) | ((Ptr[1] & 0x3F) << 12) | 
                  ((Ptr[2] & 0x3F) << 6) | (Ptr[3] & 0x3F);
      if (CodePoint < 0x10000) return reinterpret_cast<const char*>(Ptr);
      if (CodePoint > 0x10FFFF) return reinterpret_cast<const char*>(Ptr);
      break;
    }
    
    Ptr += NumBytes;
  }
  
  return End;
}

//===----------------------------------------------------------------------===//
// SSE2 implementation (x86/x86_64)
//===----------------------------------------------------------------------===//

#if defined(__SSE2__)
#include <emmintrin.h>

bool UTF8Validator::validateSSE2(const char *Ptr, size_t Length) {
  // For small strings, use scalar
  if (Length < 16) {
    return validateScalar(Ptr, Length);
  }
  
  const char *End = Ptr + Length;
  const char *AlignedEnd = Ptr + (Length & ~15); // Process 16 bytes at a time
  
  // Process chunks of 16 bytes
  while (Ptr < AlignedEnd) {
    __m128i Chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(Ptr));
    
    // Check for ASCII fast path: all bytes < 0x80
    __m128i Mask = _mm_cmplt_epi8(Chunk, _mm_set1_epi8(0x80));
    int AllASCII = _mm_movemask_epi8(Mask);
    
    if (AllASCII == 0xFFFF) {
      // All bytes are ASCII, skip
      Ptr += 16;
      continue;
    }
    
    // Has multi-byte sequences, validate with scalar
    // (Full SIMD validation is complex, scalar is safer for correctness)
    for (int i = 0; i < 16 && Ptr < End; i++) {
      unsigned char Byte = static_cast<unsigned char>(*Ptr);
      
      if ((Byte & 0x80) == 0) {
        Ptr++;
        continue;
      }
      
      unsigned NumBytes = getExpectedByteCount(Byte);
      if (NumBytes == 0 || NumBytes == 1) {
        return false;
      }
      
      if (Ptr + NumBytes > End) {
        return false;
      }
      
      for (unsigned j = 1; j < NumBytes; j++) {
        if (!isContinuationByte(static_cast<unsigned char>(Ptr[j]))) {
          return false;
        }
      }
      
      Ptr += NumBytes;
    }
  }
  
  // Handle remaining bytes with scalar
  return validateScalar(Ptr, End - Ptr);
}

const char* UTF8Validator::findInvalidSSE2(const char *Start, const char *End) {
  size_t Length = End - Start;
  
  if (Length < 16) {
    return findInvalidScalar(Start, End);
  }
  
  const char *AlignedEnd = Start + (Length & ~15);
  
  while (Start < AlignedEnd) {
    __m128i Chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(Start));
    __m128i Mask = _mm_cmplt_epi8(Chunk, _mm_set1_epi8(0x80));
    int AllASCII = _mm_movemask_epi8(Mask);
    
    if (AllASCII == 0xFFFF) {
      Start += 16;
      continue;
    }
    
    // Check each byte
    for (int i = 0; i < 16 && Start < End; ) {
      unsigned char Byte = static_cast<unsigned char>(*Start);
      
      if ((Byte & 0x80) == 0) {
        Start++;
        i++;
        continue;
      }
      
      unsigned NumBytes = getExpectedByteCount(Byte);
      if (NumBytes == 0 || NumBytes == 1) {
        return Start;
      }
      
      if (Start + NumBytes > End) {
        return Start;
      }
      
      for (unsigned j = 1; j < NumBytes; j++) {
        if (!isContinuationByte(static_cast<unsigned char>(Start[j]))) {
          return Start;
        }
      }
      
      Start += NumBytes;
      i += NumBytes;
    }
  }
  
  return findInvalidScalar(Start, End);
}

#endif // __SSE2__

//===----------------------------------------------------------------------===//
// NEON implementation (ARM/AArch64)
//===----------------------------------------------------------------------===//

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>

bool UTF8Validator::validateNEON(const char *Ptr, size_t Length) {
  // For small strings, use scalar
  if (Length < 16) {
    return validateScalar(Ptr, Length);
  }
  
  const char *End = Ptr + Length;
  const char *AlignedEnd = Ptr + (Length & ~15);
  
  uint8x16_t Threshold = vdupq_n_u8(0x80);
  
  while (Ptr < AlignedEnd) {
    uint8x16_t Chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(Ptr));
    
    // Check if all bytes are ASCII (< 0x80)
    uint8x16_t Cmp = vcltq_u8(Chunk, Threshold);
    
    // Check if all 16 bytes are ASCII
    if (vminvq_u8(Cmp) == 0xFF) {
      Ptr += 16;
      continue;
    }
    
    // Has multi-byte, validate with scalar
    for (int i = 0; i < 16 && Ptr < End; ) {
      unsigned char Byte = static_cast<unsigned char>(*Ptr);
      
      if ((Byte & 0x80) == 0) {
        Ptr++;
        i++;
        continue;
      }
      
      unsigned NumBytes = getExpectedByteCount(Byte);
      if (NumBytes == 0 || NumBytes == 1) {
        return false;
      }
      
      if (Ptr + NumBytes > End) {
        return false;
      }
      
      for (unsigned j = 1; j < NumBytes; j++) {
        if (!isContinuationByte(static_cast<unsigned char>(Ptr[j]))) {
          return false;
        }
      }
      
      Ptr += NumBytes;
      i += NumBytes;
    }
  }
  
  return validateScalar(Ptr, End - Ptr);
}

const char* UTF8Validator::findInvalidNEON(const char *Start, const char *End) {
  size_t Length = End - Start;
  
  if (Length < 16) {
    return findInvalidScalar(Start, End);
  }
  
  const char *AlignedEnd = Start + (Length & ~15);
  uint8x16_t Threshold = vdupq_n_u8(0x80);
  
  while (Start < AlignedEnd) {
    uint8x16_t Chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(Start));
    uint8x16_t Cmp = vcltq_u8(Chunk, Threshold);
    
    if (vminvq_u8(Cmp) == 0xFF) {
      Start += 16;
      continue;
    }
    
    for (int i = 0; i < 16 && Start < End; ) {
      unsigned char Byte = static_cast<unsigned char>(*Start);
      
      if ((Byte & 0x80) == 0) {
        Start++;
        i++;
        continue;
      }
      
      unsigned NumBytes = getExpectedByteCount(Byte);
      if (NumBytes == 0 || NumBytes == 1) {
        return Start;
      }
      
      if (Start + NumBytes > End) {
        return Start;
      }
      
      for (unsigned j = 1; j < NumBytes; j++) {
        if (!isContinuationByte(static_cast<unsigned char>(Start[j]))) {
          return Start;
        }
      }
      
      Start += NumBytes;
      i += NumBytes;
    }
  }
  
  return findInvalidScalar(Start, End);
}

#endif // __ARM_NEON

} // namespace blocktype
