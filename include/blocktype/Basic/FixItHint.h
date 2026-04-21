#pragma once

#include "blocktype/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace blocktype {

/// Fix-It hint: describes how to fix a source code issue
///
/// A Fix-It hint can be one of three kinds:
/// 1. Insertion: Insert new code at a specific location
/// 2. Removal: Remove code in a specific range
/// 3. Replacement: Replace code in a specific range with new code
///
/// Example usage:
/// ```cpp
/// // Insert a semicolon
/// auto Hint = FixItHint::CreateInsertion(Loc, ";");
///
/// // Remove a token
/// auto Hint = FixItHint::CreateRemoval(Range);
///
/// // Replace 'retrun' with 'return'
/// auto Hint = FixItHint::CreateReplacement(Range, "return");
/// ```
class FixItHint {
public:
  /// The kind of Fix-It operation
  enum class Kind {
    Insert,   ///< Insert code at a specific location
    Remove,   ///< Remove code in a specific range
    Replace   ///< Replace code in a specific range with new code
  };

private:
  Kind K;
  SourceLocation InsertionLoc;  ///< Location for insertion
  SourceRange RemoveRange;      ///< Range to remove/replace
  std::string CodeToInsert;     ///< Code to insert/replace with

  FixItHint(Kind K, SourceLocation Loc, SourceRange Range, 
            llvm::StringRef Code)
    : K(K), InsertionLoc(Loc), RemoveRange(Range), CodeToInsert(Code) {}

public:
  //===--------------------------------------------------------------------===//
  // Factory methods
  //===--------------------------------------------------------------------===//

  /// Create an insertion hint: insert Code at Loc
  static FixItHint CreateInsertion(SourceLocation Loc, llvm::StringRef Code) {
    return FixItHint(Kind::Insert, Loc, SourceRange(), Code);
  }

  /// Create a removal hint: remove the code in Range
  static FixItHint CreateRemoval(SourceRange Range) {
    return FixItHint(Kind::Remove, SourceLocation(), Range, "");
  }

  /// Create a replacement hint: replace the code in Range with Code
  static FixItHint CreateReplacement(SourceRange Range, llvm::StringRef Code) {
    return FixItHint(Kind::Replace, SourceLocation(), Range, Code);
  }

  //===--------------------------------------------------------------------===//
  // Accessors
  //===--------------------------------------------------------------------===//

  /// Get the kind of this Fix-It
  Kind getKind() const { return K; }

  /// Check if this is an insertion
  bool isInsert() const { return K == Kind::Insert; }

  /// Check if this is a removal
  bool isRemove() const { return K == Kind::Remove; }

  /// Check if this is a replacement
  bool isReplace() const { return K == Kind::Replace; }

  /// Get the insertion location (only valid for Insert kind)
  SourceLocation getInsertionLoc() const {
    assert(isInsert() && "Not an insertion hint");
    return InsertionLoc;
  }

  /// Get the removal/replacement range (only valid for Remove/Replace kinds)
  SourceRange getRemoveRange() const {
    assert((isRemove() || isReplace()) && "Not a removal or replacement hint");
    return RemoveRange;
  }

  /// Get the code to insert/replace with
  llvm::StringRef getCodeToInsert() const {
    assert((isInsert() || isReplace()) && "Not an insertion or replacement hint");
    return CodeToInsert;
  }

  /// Get the start location of the affected range
  SourceLocation getStartLoc() const {
    if (isInsert())
      return InsertionLoc;
    return RemoveRange.getBegin();
  }

  /// Get the end location of the affected range
  SourceLocation getEndLoc() const {
    if (isInsert())
      return InsertionLoc;
    return RemoveRange.getEnd();
  }

  /// Check if this hint affects the given location
  bool affectsLocation(SourceLocation Loc) const {
    if (isInsert())
      return Loc == InsertionLoc;
    // Check if Loc is within the range [Begin, End]
    return Loc.getRawEncoding() >= RemoveRange.getBegin().getRawEncoding() && 
           Loc.getRawEncoding() <= RemoveRange.getEnd().getRawEncoding();
  }

  //===--------------------------------------------------------------------===//
  // Debugging
  //===--------------------------------------------------------------------===//

  /// Get a string representation for debugging
  std::string toString() const {
    switch (K) {
    case Kind::Insert:
      return "Insert '" + CodeToInsert + "' at " + 
             std::to_string(InsertionLoc.getRawEncoding());
    case Kind::Remove:
      return "Remove range [" + 
             std::to_string(RemoveRange.getBegin().getRawEncoding()) + ", " +
             std::to_string(RemoveRange.getEnd().getRawEncoding()) + ")";
    case Kind::Replace:
      return "Replace range [" + 
             std::to_string(RemoveRange.getBegin().getRawEncoding()) + ", " +
             std::to_string(RemoveRange.getEnd().getRawEncoding()) + 
             "] with '" + CodeToInsert + "'";
    }
    return "Unknown Fix-It kind";
  }
};

} // namespace blocktype
