#pragma once

#include <cstdint>
#include "llvm/ADT/StringRef.h"

namespace blocktype {

/// Unicode property lookup with optimized caching
///
/// This class provides fast lookup of Unicode properties using a
/// three-tier caching strategy:
/// 1. L1: ASCII + common CJK (small array, ~1KB)
/// 2. L2: BMP characters (two-level page table)
/// 3. L3: Supplementary planes (rare, full lookup)
class UnicodePropertyCache {
public:
  /// Property flags for a Unicode code point
  struct Properties {
    uint8_t Flags;
    
    // Flag bits
    static constexpr uint8_t ID_Start    = 0x01;  // UAX #31 ID_Start
    static constexpr uint8_t ID_Continue = 0x02;  // UAX #31 ID_Continue
    static constexpr uint8_t XID_Start   = 0x04;  // UAX #31 XID_Start
    static constexpr uint8_t XID_Continue= 0x08;  // UAX #31 XID_Continue
    static constexpr uint8_t Pattern_White = 0x10; // Pattern_White_Space
    static constexpr uint8_t Pattern_Syntax = 0x20; // Pattern_Syntax
    static constexpr uint8_t CJK         = 0x40;  // CJK character
    static constexpr uint8_t Chinese     = 0x80;  // Chinese character
    
    bool isIDStart() const { return Flags & ID_Start; }
    bool isIDContinue() const { return Flags & ID_Continue; }
    bool isCJK() const { return Flags & CJK; }
    bool isChinese() const { return Flags & Chinese; }
  };
  
  /// Get properties for a code point
  static const Properties* lookup(uint32_t CP);
  
  /// Check if code point is ID_Start (UAX #31)
  static bool isIDStart(uint32_t CP) {
    const Properties* P = lookup(CP);
    return P && P->isIDStart();
  }
  
  /// Check if code point is ID_Continue (UAX #31)
  static bool isIDContinue(uint32_t CP) {
    const Properties* P = lookup(CP);
    return P && P->isIDContinue();
  }
  
  /// Check if code point is CJK
  static bool isCJK(uint32_t CP) {
    const Properties* P = lookup(CP);
    return P && P->isCJK();
  }
  
  /// Check if code point is Chinese
  static bool isChinese(uint32_t CP) {
    const Properties* P = lookup(CP);
    return P && P->isChinese();
  }

private:
  //===------------------------------------------------------------------===//
  // Three-tier cache implementation
  //===------------------------------------------------------------------===//
  
  /// L1 cache: ASCII + common CJK (0x0000-0x007F + common CJK)
  /// Size: ~1KB, covers 95% of cases
  static const Properties L1Cache[256];
  
  /// L2 cache: BMP page table (0x0000-0xFFFF)
  /// Two-level page table: 256 pages of 256 entries each
  /// Size: ~64KB for full BMP, but we only allocate used pages
  struct L2Page {
    Properties Entries[256];
  };
  static const L2Page* L2Pages[256];
  
  /// L3: Supplementary planes (0x10000-0x10FFFF)
  /// Rare, use full lookup
  static const Properties* lookupL3(uint32_t CP);
  
  /// Initialize L2 page on demand
  static const L2Page* getL2Page(uint8_t PageIndex);
};

} // namespace blocktype
