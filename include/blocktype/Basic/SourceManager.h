//===--- SourceManager.h - Source Manager ----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SourceManager class which manages source files and
// source locations.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include "blocktype/Basic/SourceLocation.h"
#include <memory>
#include <vector>
#include <string>
#include <map>

namespace blocktype {

class FileInfo {
  std::string Filename;
  StringRef Content;
  unsigned FileID;
  std::vector<unsigned> LineOffsets;

public:
  FileInfo(StringRef Name, StringRef Data, unsigned ID);

  StringRef getFilename() const { return Filename; }
  StringRef getContent() const { return Content; }
  unsigned getFileID() const { return FileID; }
  unsigned getLineCount() const { return static_cast<unsigned>(LineOffsets.size()); }

  /// Returns the line and column for a given offset.
  std::pair<unsigned, unsigned> getLineAndColumn(unsigned Offset) const;

  /// Returns the offset of the start of a given line.
  unsigned getLineOffset(unsigned Line) const;
};

/// SourceManager - Manages source files and provides source location services.
class SourceManager {
  std::vector<std::unique_ptr<FileInfo>> Files;

  // D15: Macro expansion location tracking
  struct MacroExpansionInfo {
    SourceLocation SpellingLoc;   // Location in the macro definition
    SourceLocation ExpansionLoc;  // Location where macro was expanded
    StringRef MacroName;
  };
  std::map<SourceLocation, MacroExpansionInfo> MacroExpansionMap;

public:
  SourceManager() = default;
  ~SourceManager() = default;

  // Non-copyable
  SourceManager(const SourceManager &) = delete;
  SourceManager &operator=(const SourceManager &) = delete;

  /// Creates a main file entry and returns its location.
  SourceLocation createMainFileID(StringRef Filename, StringRef Content);

  /// Creates a file entry and returns its location.
  SourceLocation createFileID(StringRef Filename, StringRef Content);

  /// Returns the file info for a given source location.
  const FileInfo *getFileInfo(SourceLocation Loc) const;

  /// Returns the file info for a given file ID.
  const FileInfo *getFileInfo(unsigned FileID) const;

  /// Returns the line and column for a source location.
  std::pair<unsigned, unsigned> getLineAndColumn(SourceLocation Loc) const;

  /// Returns the source text at a source location.
  StringRef getCharacterData(SourceLocation Loc) const;

  /// Returns the source text for a range.
  StringRef getCharacterData(SourceLocation Start, SourceLocation End) const;

  /// Prints a source location (e.g., "test.cpp:10:5").
  void printLocation(raw_ostream &OS, SourceLocation Loc) const;

  /// Returns a string representation of a source location.
  std::string getLocationString(SourceLocation Loc) const;

  /// Returns the number of files managed.
  unsigned getNumFiles() const { return static_cast<unsigned>(Files.size()); }

  /// Clears all file entries.
  void clear() { Files.clear(); }

  //===--------------------------------------------------------------------===//
  // Macro Expansion Location Tracking (D15, E2)
  //===--------------------------------------------------------------------===//

  /// Records a macro expansion.
  void addMacroExpansion(SourceLocation Loc, SourceLocation SpellingLoc,
                         SourceLocation ExpansionLoc, StringRef MacroName);

  /// Returns the spelling location for a macro expansion location.
  /// If the location is not inside a macro expansion, returns the input.
  SourceLocation getSpellingLoc(SourceLocation Loc) const;

  /// Returns the expansion location for a macro expansion location.
  /// If the location is not inside a macro expansion, returns the input.
  SourceLocation getExpansionLoc(SourceLocation Loc) const;

  /// Returns true if the location is inside a macro expansion.
  bool isMacroArgExpansion(SourceLocation Loc) const;

  /// Returns the macro name for a macro expansion location.
  StringRef getMacroName(SourceLocation Loc) const;
};

} // namespace blocktype
