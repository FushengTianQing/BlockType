#pragma once

#include <cstdint>

namespace blocktype {

/// SourceLocation - Represents a precise location in source code.
///
/// The location is encoded as a 64-bit value:
/// - High 16 bits: File ID (supports up to 65536 files)
/// - Low 48 bits: Byte offset (supports files up to 256TB)
///
/// This encoding allows O(1) file lookup and efficient position calculation.
class SourceLocation {
  uint64_t ID = 0;

  static constexpr unsigned FileIDBits = 16;
  static constexpr unsigned OffsetBits = 48;
  static constexpr uint64_t MaxOffset = (1ULL << OffsetBits) - 1;
  static constexpr uint64_t FileIDMask = ((1ULL << FileIDBits) - 1) << OffsetBits;
  static constexpr uint64_t OffsetMask = (1ULL << OffsetBits) - 1;

public:
  SourceLocation() = default;

  /// Creates a SourceLocation from a raw encoded value.
  explicit SourceLocation(uint64_t id) : ID(id) {}

  /// Creates a SourceLocation from file ID and offset.
  static SourceLocation getFileLoc(unsigned FileID, unsigned Offset) {
    return SourceLocation((static_cast<uint64_t>(FileID) << OffsetBits) | Offset);
  }

  bool isValid() const { return ID != 0; }
  bool isInvalid() const { return ID == 0; }

  /// Returns the raw encoded value.
  uint64_t getRawEncoding() const { return ID; }

  /// Returns the file ID component.
  unsigned getFileID() const {
    return static_cast<unsigned>((ID & FileIDMask) >> OffsetBits);
  }

  /// Returns the byte offset component.
  unsigned getOffset() const {
    return static_cast<unsigned>(ID & OffsetMask);
  }

  /// Returns a location advanced by the given offset.
  SourceLocation getLocWithOffset(int Offset) const {
    if (isInvalid()) return *this;
    return SourceLocation(ID + Offset);
  }

  bool operator==(const SourceLocation &RHS) const { return ID == RHS.ID; }
  bool operator!=(const SourceLocation &RHS) const { return ID != RHS.ID; }
  bool operator<(const SourceLocation &RHS) const { return ID < RHS.ID; }
};

class SourceRange {
  SourceLocation Begin, End;
public:
  SourceRange() = default;
  SourceRange(SourceLocation B, SourceLocation E) : Begin(B), End(E) {}
  SourceRange(SourceLocation Loc) : Begin(Loc), End(Loc) {}
  
  SourceLocation getBegin() const { return Begin; }
  SourceLocation getEnd() const { return End; }
  bool isValid() const { return Begin.isValid() && End.isValid(); }
  bool isInvalid() const { return !isValid(); }
};

} // namespace blocktype