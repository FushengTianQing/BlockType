#include "blocktype/IR/QueryCacheSerializer.h"
#include "blocktype/IR/QueryContext.h"
#include "blocktype/IR/IRSerializer.h"
#include "blocktype/IR/IRFormatVersion.h"
#include "blocktype/IR/IRModule.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace blocktype {
namespace ir {

QueryCacheSerializer::QueryCacheSerializer(StringRef Dir)
    : CacheDir(Dir) {}

bool QueryCacheSerializer::save(const QueryContext& QC, StringRef ModuleName) {
  // Build cache file path: CacheDir/ModuleName.querycache
  std::string Path = CacheDir.str() + "/" + ModuleName.str() + ".querycache";

  // Ensure cache directory exists
  {
    std::string Dir(CacheDir.str());
    size_t Pos = 0;
    while ((Pos = Dir.find('/', Pos + 1)) != std::string::npos) {
      std::string Sub = Dir.substr(0, Pos);
      mkdir(Sub.c_str(), 0755);
    }
    mkdir(Dir.c_str(), 0755);
  }

  // Get the target module from QueryContext
  const IRModule* M = QC.getTargetModule();

  // Write version header + serialized data
  std::ofstream Out(Path, std::ios::binary);
  if (!Out.is_open())
    return false;

  // Write IRFormatVersion header
  IRFormatVersion FileVersion = IRFormatVersion::Current();
  uint16_t Major = FileVersion.Major;
  uint16_t Minor = FileVersion.Minor;
  uint16_t Patch = FileVersion.Patch;
  Out.write(reinterpret_cast<const char*>(&Major), sizeof(Major));
  Out.write(reinterpret_cast<const char*>(&Minor), sizeof(Minor));
  Out.write(reinterpret_cast<const char*>(&Patch), sizeof(Patch));

  // Write cache size marker
  size_t CacheSize = QC.getCacheSize();
  Out.write(reinterpret_cast<const char*>(&CacheSize), sizeof(CacheSize));

  // Serialize the target module using IRWriter::writeBitcode()
  if (M) {
    // Use IRWriter::writeBitcode() to serialize to a string buffer
    std::string BitcodeData;
    raw_string_ostream BOS(BitcodeData);
    bool WriteOK = IRWriter::writeBitcode(*M, BOS);
    if (!WriteOK) {
      Out.close();
      invalidate(ModuleName);
      return false;
    }

    // Write bitcode data length + data
    uint64_t DataLen = BitcodeData.size();
    Out.write(reinterpret_cast<const char*>(&DataLen), sizeof(DataLen));
    Out.write(BitcodeData.data(), static_cast<std::streamsize>(DataLen));
  } else {
    // No module to serialize — write zero-length marker
    uint64_t DataLen = 0;
    Out.write(reinterpret_cast<const char*>(&DataLen), sizeof(DataLen));
  }

  Out.close();
  return Out.good();
}

bool QueryCacheSerializer::load(QueryContext& QC, StringRef ModuleName) {
  std::string Path = CacheDir.str() + "/" + ModuleName.str() + ".querycache";

  std::ifstream In(Path, std::ios::binary);
  if (!In.is_open())
    return false;

  // Read IRFormatVersion header and check compatibility
  uint16_t Major, Minor, Patch;
  In.read(reinterpret_cast<char*>(&Major), sizeof(Major));
  In.read(reinterpret_cast<char*>(&Minor), sizeof(Minor));
  In.read(reinterpret_cast<char*>(&Patch), sizeof(Patch));
  if (!In.good()) {
    In.close();
    invalidate(ModuleName);
    return false;
  }

  IRFormatVersion FileVersion{Major, Minor, Patch};
  if (!FileVersion.isCompatibleWith(IRFormatVersion::Current())) {
    In.close();
    invalidate(ModuleName);
    return false;
  }

  // Read cache size marker
  size_t CacheSize = 0;
  In.read(reinterpret_cast<char*>(&CacheSize), sizeof(CacheSize));
  if (!In.good()) {
    In.close();
    invalidate(ModuleName);
    return false;
  }

  // Read bitcode data length
  uint64_t DataLen = 0;
  In.read(reinterpret_cast<char*>(&DataLen), sizeof(DataLen));
  if (!In.good()) {
    In.close();
    invalidate(ModuleName);
    return false;
  }

  // Deserialize using IRReader::parseBitcode() if there is data
  if (DataLen > 0) {
    std::vector<char> BitcodeBuf(static_cast<size_t>(DataLen));
    In.read(BitcodeBuf.data(), static_cast<std::streamsize>(DataLen));
    if (!In.good()) {
      In.close();
      invalidate(ModuleName);
      return false;
    }

    In.close();

    // Use IRReader::parseBitcode() to deserialize the module
    StringRef BitcodeData(BitcodeBuf.data(), static_cast<size_t>(DataLen));
    SerializationDiagnostic Diag;
    auto RestoredModule = IRReader::parseBitcode(BitcodeData, QC.getTypeContext(), &Diag);
    if (!RestoredModule) {
      // Corrupted file — graceful degradation: delete and return false
      invalidate(ModuleName);
      return false;
    }

    // Set the restored module on the QueryContext
    QC.setTargetModule(RestoredModule.release());
  } else {
    In.close();
  }

  return true;
}

bool QueryCacheSerializer::isValid(StringRef ModuleName) const {
  std::string Path = CacheDir.str() + "/" + ModuleName.str() + ".querycache";

  std::ifstream In(Path, std::ios::binary);
  if (!In.is_open())
    return false;

  // Read and check version
  uint16_t Major, Minor, Patch;
  In.read(reinterpret_cast<char*>(&Major), sizeof(Major));
  In.read(reinterpret_cast<char*>(&Minor), sizeof(Minor));
  In.read(reinterpret_cast<char*>(&Patch), sizeof(Patch));
  In.close();

  if (!In.good())
    return false;

  IRFormatVersion FileVersion{Major, Minor, Patch};
  return FileVersion.isCompatibleWith(IRFormatVersion::Current());
}

bool QueryCacheSerializer::invalidate(StringRef ModuleName) {
  std::string Path = CacheDir.str() + "/" + ModuleName.str() + ".querycache";
  return std::remove(Path.c_str()) == 0;
}

} // namespace ir
} // namespace blocktype
