//===--- HeaderSearch.cpp - Header Search Implementation -------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Lex/HeaderSearch.h"
#include "blocktype/Basic/FileEntry.h"
#include "blocktype/Basic/FileManager.h"
#include <sys/stat.h>

namespace blocktype {

HeaderSearch::HeaderSearch(FileManager &FM) : FileMgr(FM) {}

void HeaderSearch::addSearchPath(StringRef Path, bool IsSystem, bool IsFramework) {
  SearchPaths.push_back({Path.str(), IsSystem, IsFramework});
}

const FileEntry *HeaderSearch::lookupHeader(StringRef Filename, bool IsAngled,
                                            StringRef IncludeDir) {
  // Absolute path - search directly
  if (isAbsolutePath(Filename)) {
    return FileMgr.getFile(Filename);
  }

  // For "..." style includes, first search in including directory
  if (!IsAngled && !IncludeDir.empty()) {
    std::string FullPath = joinPath(IncludeDir, Filename);
    const FileEntry *FE = FileMgr.getFile(FullPath);
    if (FE) {
      return FE;
    }
  }

  // Search in search paths
  for (const auto &SP : SearchPaths) {
    // For <...> style, skip non-system paths if we want system headers
    // For "..." style, search all paths
    if (IsAngled && !SP.IsSystem) {
      continue;
    }

    const FileEntry *FE = nullptr;
    if (SP.IsFramework) {
      FE = searchFramework(SP.Path, Filename);
    } else {
      FE = searchInPath(SP.Path, Filename);
    }

    if (FE) {
      return FE;
    }
  }

  return nullptr;
}

bool HeaderSearch::headerExists(StringRef Filename, bool IsAngled) {
  return lookupHeader(Filename, IsAngled) != nullptr;
}

const FileEntry *HeaderSearch::lookupHeaderNext(StringRef Filename, StringRef CurrentFile) {
  // Find the search path where the current file was found
  size_t CurrentPathIndex = 0;
  bool FoundCurrent = false;

  for (size_t i = 0; i < SearchPaths.size(); ++i) {
    std::string FullPath = joinPath(SearchPaths[i].Path, CurrentFile);
    if (FileMgr.getFile(FullPath)) {
      CurrentPathIndex = i;
      FoundCurrent = true;
      break;
    }
  }

  // If current file not found in search paths, start from beginning
  if (!FoundCurrent) {
    CurrentPathIndex = 0;
  } else {
    // Start from next path after current
    CurrentPathIndex++;
  }

  // Search starting from next path
  for (size_t i = CurrentPathIndex; i < SearchPaths.size(); ++i) {
    const auto &SP = SearchPaths[i];
    const FileEntry *FE = nullptr;
    if (SP.IsFramework) {
      FE = searchFramework(SP.Path, Filename);
    } else {
      FE = searchInPath(SP.Path, Filename);
    }
    if (FE) {
      return FE;
    }
  }

  return nullptr;
}

void HeaderSearch::markIncluded(StringRef Filename) {
  IncludedFiles.insert(Filename.str());
}

bool HeaderSearch::wasIncluded(StringRef Filename) const {
  return IncludedFiles.find(Filename.str()) != IncludedFiles.end();
}

void HeaderSearch::markHasIncludeGuard(StringRef Filename) {
  IncludeGuardFiles.insert(Filename.str());
}

bool HeaderSearch::hasIncludeGuard(StringRef Filename) const {
  return IncludeGuardFiles.find(Filename.str()) != IncludeGuardFiles.end();
}

std::string HeaderSearch::getCanonicalPath(StringRef Filename) {
  // Simplified canonical path - just return the filename
  return Filename.str();
}

bool HeaderSearch::isAbsolutePath(StringRef Path) {
  if (Path.empty()) {
    return false;
  }
  return Path[0] == '/';
}

std::string HeaderSearch::joinPath(StringRef Dir, StringRef Filename) {
  if (Dir.empty()) {
    return Filename.str();
  }
  if (Dir.back() == '/') {
    return Dir.str() + Filename.str();
  }
  return Dir.str() + "/" + Filename.str();
}

const FileEntry *HeaderSearch::searchInPath(StringRef Path, StringRef Filename) {
  std::string FullPath = joinPath(Path, Filename);
  return FileMgr.getFile(FullPath);
}

const FileEntry *HeaderSearch::searchFramework(StringRef Path, StringRef Filename) {
  // Framework search: FrameworkName.framework/Headers/HeaderName
  // Simplified implementation
  std::string FrameworkPath = Path.str() + "/" + Filename.str() + ".framework/Headers/" + Filename.str();
  return FileMgr.getFile(FrameworkPath);
}

} // namespace blocktype