#pragma once

#include "blocktype/Basic/FileEntry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <map>

namespace blocktype {

class FileManager {
  std::map<std::string, std::unique_ptr<FileEntry>> FileCache;
  unsigned NextUID = 0;
public:
  const FileEntry* getFile(llvm::StringRef Path);
  std::unique_ptr<llvm::MemoryBuffer> getBuffer(llvm::StringRef Path);
  bool exists(llvm::StringRef Path) const;
};

} // namespace blocktype