//===--- HeaderSearch.h - Header Search Interface --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the HeaderSearch class which handles header file
// searching for #include directives.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include <vector>
#include <string>
#include <set>

namespace blocktype {

class FileManager;
class FileEntry;

/// HeaderSearch - Manages header file search paths and inclusion.
class HeaderSearch {
  FileManager &FileMgr;

  struct SearchPath {
    std::string Path;
    bool IsSystem;
    bool IsFramework;
  };
  std::vector<SearchPath> SearchPaths;

  std::set<std::string> IncludedFiles;
  std::set<std::string> IncludeGuardFiles;

public:
  explicit HeaderSearch(FileManager &FM);
  ~HeaderSearch() = default;

  HeaderSearch(const HeaderSearch &) = delete;
  HeaderSearch &operator=(const HeaderSearch &) = delete;

  void addSearchPath(StringRef Path, bool IsSystem = false, bool IsFramework = false);
  void addSystemSearchPath(StringRef Path) { addSearchPath(Path, true); }
  void addUserSearchPath(StringRef Path) { addSearchPath(Path, false); }
  void clearSearchPaths() { SearchPaths.clear(); }
  const std::vector<SearchPath> &getSearchPaths() const { return SearchPaths; }

  const FileEntry *lookupHeader(StringRef Filename, bool IsAngled,
                                StringRef IncludeDir = StringRef());

  /// Looks up a header starting from the next search path after the current file.
  /// Used for #include_next (GNU extension).
  const FileEntry *lookupHeaderNext(StringRef Filename, StringRef CurrentFile);

  bool headerExists(StringRef Filename, bool IsAngled);

  void markIncluded(StringRef Filename);
  bool wasIncluded(StringRef Filename) const;
  void clearIncludedFiles() { IncludedFiles.clear(); }

  void markHasIncludeGuard(StringRef Filename);
  bool hasIncludeGuard(StringRef Filename) const;

  std::string getCanonicalPath(StringRef Filename);
  static bool isAbsolutePath(StringRef Path);
  static std::string joinPath(StringRef Dir, StringRef Filename);

private:
  const FileEntry *searchInPath(StringRef Path, StringRef Filename);
  const FileEntry *searchFramework(StringRef Path, StringRef Filename);
};

} // namespace blocktype