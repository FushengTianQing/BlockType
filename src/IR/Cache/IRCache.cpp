#include "blocktype/IR/IRCache.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRSerializer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace blocktype {
namespace cache {

// ============================================================
// BLAKE3-inspired hash (简化实现，后续可替换为完整 BLAKE3)
// 当前使用 FNV-1a 作为基础，接口预留 BLAKE3 升级路径
// ============================================================

static uint64_t fnv1a64(const void* Data, size_t Len) {
  auto* B = static_cast<const uint8_t*>(Data);
  uint64_t H = 14695981039346656037ULL;
  for (size_t i = 0; i < Len; ++i) { H ^= B[i]; H *= 1099511628211ULL; }
  return H;
}

static uint64_t hashStr(ir::StringRef S) { return fnv1a64(S.data(), S.size()); }

static uint64_t hashU64(uint64_t V) { return fnv1a64(&V, sizeof(V)); }

/// BLAKE3 兼容哈希接口（当前使用 FNV-1a 实现）
/// 后续可替换为完整 BLAKE3 库实现
static uint64_t computeBLAKE3Hash(ir::StringRef Data) {
  return fnv1a64(Data.data(), Data.size());
}

static uint64_t combineHash(uint64_t A, uint64_t B) {
  return A ^ (B * 1099511628211ULL + 0x9e3779b97f4a7c15ULL);
}

// ============================================================
// CacheKey
// ============================================================

CacheKey CacheKey::compute(ir::StringRef Source, const CacheOptions& Opts) {
  CacheKey K;
  // 1. 源码哈希（BLAKE3）
  K.SourceHash = computeBLAKE3Hash(Source);

  // 2. 编译选项哈希
  std::string OptsStr;
  OptsStr += Opts.TargetTriple;
  OptsStr += Opts.DataLayout;
  OptsStr += std::to_string(Opts.FeatureFlags);
  K.OptionsHash = computeBLAKE3Hash(ir::StringRef(OptsStr));

  // 3. 版本哈希
  K.VersionHash = computeBLAKE3Hash(ir::StringRef(Opts.Version.toString()));

  // 4. 目标三元组哈希
  K.TargetTripleHash = computeBLAKE3Hash(ir::StringRef(Opts.TargetTriple));

  // 5. 依赖哈希（远期：包含所有 #include 文件的哈希）
  K.DependencyHash = 0;  // 当前 stub

  // 6. 组合哈希
  std::string Combined;
  Combined += std::to_string(K.SourceHash);
  Combined += std::to_string(K.OptionsHash);
  Combined += std::to_string(K.VersionHash);
  Combined += std::to_string(K.TargetTripleHash);
  Combined += std::to_string(K.DependencyHash);
  K.CombinedHash = computeBLAKE3Hash(ir::StringRef(Combined));

  return K;
}

std::string CacheKey::toHex() const {
  char Buf[17];
  std::snprintf(Buf, sizeof(Buf), "%016llx", (unsigned long long)CombinedHash);
  return Buf;
}

std::string CacheKey::toPathComponents() const {
  std::string H = toHex();
  return H.substr(0, 2) + "/" + H.substr(2);
}

// ============================================================
// CacheEntry JSON
// ============================================================

std::string CacheEntry::toJSON() const {
  std::string S;
  S += "{\n  \"combined_hash\": \""; S += Key.toHex(); S += "\",\n";
  S += "  \"ir_version_major\": "; S += std::to_string(IRVersion.Major); S += ",\n";
  S += "  \"ir_version_minor\": "; S += std::to_string(IRVersion.Minor); S += ",\n";
  S += "  \"ir_version_patch\": "; S += std::to_string(IRVersion.Patch); S += ",\n";
  S += "  \"timestamp\": "; S += std::to_string(Timestamp); S += ",\n";
  S += "  \"ir_size\": "; S += std::to_string(IRSize); S += ",\n";
  S += "  \"object_size\": "; S += std::to_string(ObjectSize); S += "\n}\n";
  return S;
}

static std::string extractJSONValue(const std::string& JSON, const std::string& Key) {
  auto Pos = JSON.find(Key);
  if (Pos == std::string::npos) return "";
  Pos = JSON.find(':', Pos + Key.size());
  if (Pos == std::string::npos) return "";
  ++Pos;
  while (Pos < JSON.size() && (JSON[Pos]==' '||JSON[Pos]=='\t')) ++Pos;
  if (Pos < JSON.size() && JSON[Pos] == '"') {
    auto End = JSON.find('"', ++Pos);
    return JSON.substr(Pos, End - Pos);
  }
  auto End = JSON.find_first_of(",}\n", Pos);
  if (End == std::string::npos) End = JSON.size();
  auto Val = JSON.substr(Pos, End - Pos);
  while (!Val.empty() && (Val.back()==' '||Val.back()=='\t')) Val.pop_back();
  return Val;
}

std::optional<CacheEntry> CacheEntry::fromJSON(const std::string& JSON) {
  CacheEntry E;
  auto H = extractJSONValue(JSON, "\"combined_hash\"");
  if (!H.empty()) E.Key.CombinedHash = std::stoull(H, nullptr, 16);
  auto M = extractJSONValue(JSON, "\"ir_version_major\"");
  if (!M.empty()) E.IRVersion.Major = (uint16_t)std::stoul(M);
  auto N = extractJSONValue(JSON, "\"ir_version_minor\"");
  if (!N.empty()) E.IRVersion.Minor = (uint16_t)std::stoul(N);
  auto P = extractJSONValue(JSON, "\"ir_version_patch\"");
  if (!P.empty()) E.IRVersion.Patch = (uint16_t)std::stoul(P);
  auto T = extractJSONValue(JSON, "\"timestamp\"");
  if (!T.empty()) E.Timestamp = std::stoull(T);
  auto I = extractJSONValue(JSON, "\"ir_size\"");
  if (!I.empty()) E.IRSize = std::stoull(I);
  auto O = extractJSONValue(JSON, "\"object_size\"");
  if (!O.empty()) E.ObjectSize = std::stoull(O);
  return E;
}

// ============================================================
// LocalDiskCache
// ============================================================

class LocalDiskCache::Impl {
public:
  std::string CacheDir;
  size_t MaxSize;
  mutable size_t Hits = 0, Misses = 0;
  size_t Evictions = 0;

  Impl(ir::StringRef D, size_t M) : CacheDir(D.str()), MaxSize(M) {}
  std::string entryDir(const CacheKey& K) const { return CacheDir + "/" + K.toPathComponents(); }

  static bool ensureDir(const std::string& P) {
    std::error_code EC; std::filesystem::create_directories(P, EC); return !EC;
  }
  static bool readFile(const std::string& P, std::vector<uint8_t>& Out) {
    std::ifstream F(P, std::ios::binary|std::ios::ate);
    if (!F) return false;
    auto Sz = F.tellg(); if (Sz <= 0) return false;
    F.seekg(0); Out.resize((size_t)Sz);
    F.read((char*)Out.data(), Sz); return F.good();
  }
  static bool writeFile(const std::string& P, const uint8_t* D, size_t L) {
    std::ofstream F(P, std::ios::binary);
    if (!F) return false;
    F.write((const char*)D, (std::streamsize)L); return F.good();
  }
  size_t computeTotalSize() const {
    size_t T = 0; std::error_code EC;
    for (auto& E : std::filesystem::recursive_directory_iterator(CacheDir, EC))
      if (E.is_regular_file()) T += (size_t)E.file_size();
    return T;
  }
};

LocalDiskCache::LocalDiskCache(ir::StringRef Dir, size_t Max)
    : Pimpl(new Impl(Dir, Max)) { Impl::ensureDir(Pimpl->CacheDir); }
LocalDiskCache::~LocalDiskCache() { delete Pimpl; }

std::optional<CacheEntry> LocalDiskCache::lookup(const CacheKey& Key) {
  std::string Dir = Pimpl->entryDir(Key);
  std::vector<uint8_t> Meta;
  if (!Impl::readFile(Dir + "/meta.json", Meta)) { ++Pimpl->Misses; return std::nullopt; }
  auto Entry = CacheEntry::fromJSON(std::string(Meta.begin(), Meta.end()));
  if (!Entry) { ++Pimpl->Misses; return std::nullopt; }
  Entry->Key = Key;
  Impl::readFile(Dir + "/ir.btir", Entry->IRData);
  Impl::readFile(Dir + "/obj.o", Entry->ObjectData);
  ++Pimpl->Hits;
  return Entry;
}

bool LocalDiskCache::store(const CacheKey& Key, const CacheEntry& Entry) {
  std::string Dir = Pimpl->entryDir(Key);
  if (!Impl::ensureDir(Dir)) return false;
  if (!Entry.IRData.empty())
    if (!Impl::writeFile(Dir + "/ir.btir", Entry.IRData.data(), Entry.IRData.size())) return false;
  if (!Entry.ObjectData.empty())
    if (!Impl::writeFile(Dir + "/obj.o", Entry.ObjectData.data(), Entry.ObjectData.size())) return false;
  auto M = Entry.toJSON();
  if (!Impl::writeFile(Dir + "/meta.json", (const uint8_t*)M.data(), M.size())) return false;
  evictIfNeeded();
  return true;
}

CacheStorage::Stats LocalDiskCache::getStats() const {
  return {Pimpl->Hits, Pimpl->Misses, Pimpl->Evictions, Pimpl->computeTotalSize()};
}

void LocalDiskCache::evictIfNeeded() {
  if (Pimpl->computeTotalSize() <= Pimpl->MaxSize) return;
  std::vector<std::pair<uint64_t,std::string>> Entries;
  std::error_code EC;
  for (auto& D1 : std::filesystem::directory_iterator(Pimpl->CacheDir, EC)) {
    if (!D1.is_directory()) continue;
    for (auto& D2 : std::filesystem::directory_iterator(D1.path(), EC)) {
      if (!D2.is_directory()) continue;
      std::vector<uint8_t> M;
      if (Impl::readFile(D2.path().string()+"/meta.json", M)) {
        auto E = CacheEntry::fromJSON(std::string(M.begin(),M.end()));
        if (E) Entries.emplace_back(E->Timestamp, D2.path().string());
      }
    }
  }
  std::sort(Entries.begin(), Entries.end());
  for (auto& [Ts, Path] : Entries) {
    (void)Ts;
    if (Pimpl->computeTotalSize() <= Pimpl->MaxSize * 9 / 10) break;
    std::error_code RmEC; std::filesystem::remove_all(Path, RmEC);
    ++Pimpl->Evictions;
  }
}

ir::StringRef LocalDiskCache::getCacheDir() const { return Pimpl->CacheDir; }
size_t LocalDiskCache::getMaxSize() const { return Pimpl->MaxSize; }

// ============================================================
// RemoteCache — stub
// ============================================================

RemoteCache::RemoteCache(ir::StringRef Ep, ir::StringRef Bk)
    : Endpoint_(Ep.str()), Bucket_(Bk.str()) {}
std::optional<CacheEntry> RemoteCache::lookup(const CacheKey&) { return std::nullopt; }
bool RemoteCache::store(const CacheKey&, const CacheEntry&) { return false; }
CacheStorage::Stats RemoteCache::getStats() const { return {}; }

// ============================================================
// CompilationCacheManager
// ============================================================

CompilationCacheManager::CompilationCacheManager() = default;
CompilationCacheManager::~CompilationCacheManager() = default;

void CompilationCacheManager::enable(ir::StringRef D, size_t M) {
  Storage_ = std::make_unique<LocalDiskCache>(D, M); Enabled_ = true;
}
void CompilationCacheManager::disable() { Storage_.reset(); Enabled_ = false; }
bool CompilationCacheManager::isEnabled() const { return Enabled_; }

std::optional<std::unique_ptr<ir::IRModule>>
CompilationCacheManager::lookupIR(const CacheKey& Key, ir::IRTypeContext& Ctx) {
  if (!Enabled_ || !Storage_) return std::nullopt;
  auto E = Storage_->lookup(Key);
  if (!E || E->IRData.empty()) return std::nullopt;

  // Deserialize IR from cached bitcode data
  ir::SerializationDiagnostic Diag;
  auto M = ir::IRReader::parseBitcode(
      ir::StringRef(reinterpret_cast<const char*>(E->IRData.data()), E->IRData.size()),
      Ctx, &Diag);
  if (!M) return std::nullopt;
  return M;
}

std::optional<std::vector<uint8_t>>
CompilationCacheManager::lookupObject(const CacheKey& Key) {
  if (!Enabled_ || !Storage_) return std::nullopt;
  auto E = Storage_->lookup(Key);
  if (!E || E->ObjectData.empty()) return std::nullopt;
  return std::move(E->ObjectData);
}

bool CompilationCacheManager::storeIR(const CacheKey& Key, const ir::IRModule& M) {
  if (!Enabled_ || !Storage_) return false;

  // Serialize IR to bitcode
  std::string Bitcode;
  ir::raw_string_ostream OS(Bitcode);
  if (!ir::IRWriter::writeBitcode(M, OS)) return false;

  CacheEntry E;
  E.Key = Key;
  E.IRData.assign(Bitcode.begin(), Bitcode.end());
  E.IRSize = Bitcode.size();
  E.IRVersion = ir::IRFormatVersion::Current();
  E.Timestamp = (uint64_t)std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return Storage_->store(Key, E);
}

bool CompilationCacheManager::storeObject(const CacheKey& Key, ir::ArrayRef<uint8_t> Data) {
  if (!Enabled_ || !Storage_) return false;
  CacheEntry E; E.Key = Key;
  E.ObjectData.assign(Data.begin(), Data.end());
  E.ObjectSize = Data.size();
  E.Timestamp = (uint64_t)std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return Storage_->store(Key, E);
}

} // namespace cache
} // namespace blocktype
