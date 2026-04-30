#include <gtest/gtest.h>
#include <filesystem>
#include "blocktype/IR/IRCache.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"

using namespace blocktype;
using namespace blocktype::cache;

static std::string makeTempDir() {
  auto D = std::filesystem::temp_directory_path() / ("btcache_" + std::to_string(::getpid()));
  std::filesystem::create_directories(D);
  return D.string();
}
static void rmDir(const std::string& D) { std::error_code EC; std::filesystem::remove_all(D,EC); }

// V1: CacheKey 计算
TEST(IRCacheTest, CacheKeyCompute) {
  CacheOptions Opts; Opts.TargetTriple = "x86_64-linux-gnu";
  auto K = CacheKey::compute("int main(){}", Opts);
  EXPECT_NE(K.CombinedHash, 0ull);
  EXPECT_NE(K.SourceHash, 0ull);
}

// V2: CacheKey 确定性
TEST(IRCacheTest, CacheKeyDeterministic) {
  CacheOptions O;
  EXPECT_EQ(CacheKey::compute("src", O).CombinedHash, CacheKey::compute("src", O).CombinedHash);
}

// V3: 不同源码不同 key
TEST(IRCacheTest, CacheKeyDiffersOnSource) {
  CacheOptions O;
  EXPECT_NE(CacheKey::compute("a", O).CombinedHash, CacheKey::compute("b", O).CombinedHash);
}

// V4: 不同选项不同 key
TEST(IRCacheTest, CacheKeyDiffersOnOptions) {
  CacheOptions O1; O1.TargetTriple = "x86_64-linux";
  CacheOptions O2; O2.TargetTriple = "aarch64-macos";
  EXPECT_NE(CacheKey::compute("s", O1).CombinedHash, CacheKey::compute("s", O2).CombinedHash);
}

// V5: toHex 格式
TEST(IRCacheTest, CacheKeyToHex) {
  CacheOptions O;
  auto H = CacheKey::compute("x", O).toHex();
  EXPECT_EQ(H.size(), 16u);
}

// V6: toPathComponents 格式
TEST(IRCacheTest, CacheKeyPathComponents) {
  CacheOptions O;
  auto P = CacheKey::compute("x", O).toPathComponents();
  EXPECT_EQ(P.size(), 17u);
  EXPECT_EQ(P[2], '/');
}

// V7: LocalDiskCache 存取
TEST(IRCacheTest, LocalDiskCacheStoreLookup) {
  std::string D = makeTempDir();
  LocalDiskCache LDC(D, 1ull<<30);
  CacheOptions O;
  auto K = CacheKey::compute("int main(){}", O);
  CacheEntry E; E.Key = K; E.IRData = {1,2,3}; E.IRSize = 3;
  ASSERT_TRUE(LDC.store(K, E));
  auto F = LDC.lookup(K);
  ASSERT_TRUE(F.has_value());
  EXPECT_EQ(F->IRData.size(), 3u);
  EXPECT_EQ(F->IRData[0], 1);
  rmDir(D);
}

// V8: LocalDiskCache miss
TEST(IRCacheTest, LocalDiskCacheMiss) {
  std::string D = makeTempDir();
  LocalDiskCache LDC(D, 1<<20);
  CacheOptions O;
  EXPECT_FALSE(LDC.lookup(CacheKey::compute("nope",O)).has_value());
  EXPECT_EQ(LDC.getStats().Misses, 1u);
  rmDir(D);
}

// V9: LocalDiskCache 统计
TEST(IRCacheTest, LocalDiskCacheStats) {
  std::string D = makeTempDir();
  LocalDiskCache LDC(D, 1<<20);
  CacheOptions O; auto K = CacheKey::compute("stats",O);
  CacheEntry E; E.Key = K; E.ObjectData = {0xAA}; E.ObjectSize = 1;
  LDC.store(K, E);
  LDC.lookup(K); LDC.lookup(K);
  auto S = LDC.getStats();
  EXPECT_EQ(S.Hits, 2u);
  EXPECT_GT(S.TotalSize, 0u);
  rmDir(D);
}

// V10: CacheEntry JSON 序列化/反序列化
TEST(IRCacheTest, CacheEntryJSON) {
  CacheEntry E; E.Key.CombinedHash = 0xdeadbeef12345678ULL;
  E.IRVersion = {1,2,3}; E.Timestamp = 9999; E.IRSize = 100; E.ObjectSize = 200;
  auto J = E.toJSON();
  EXPECT_NE(J.find("deadbeef12345678"), std::string::npos);
  EXPECT_NE(J.find("\"ir_version_major\": 1"), std::string::npos);
  auto P = CacheEntry::fromJSON(J);
  ASSERT_TRUE(P.has_value());
  EXPECT_EQ(P->Key.CombinedHash, 0xdeadbeef12345678ULL);
  EXPECT_EQ(P->IRVersion.Major, 1u);
  EXPECT_EQ(P->Timestamp, 9999ull);
}

// V11: CompilationCacheManager enable/disable
TEST(IRCacheTest, ManagerEnableDisable) {
  CompilationCacheManager Mgr;
  EXPECT_FALSE(Mgr.isEnabled());
  std::string D = makeTempDir();
  Mgr.enable(D, 1<<20);
  EXPECT_TRUE(Mgr.isEnabled());
  Mgr.disable();
  EXPECT_FALSE(Mgr.isEnabled());
  rmDir(D);
}

// V12: CompilationCacheManager storeObject/lookupObject
TEST(IRCacheTest, ManagerObjectStoreLookup) {
  std::string D = makeTempDir();
  CompilationCacheManager Mgr;
  Mgr.enable(D, 1<<20);
  CacheOptions O; auto K = CacheKey::compute("obj_test", O);
  std::vector<uint8_t> Data = {1,2,3,4,5};
  ASSERT_TRUE(Mgr.storeObject(K, ir::ArrayRef<uint8_t>(Data)));
  auto Found = Mgr.lookupObject(K);
  ASSERT_TRUE(Found.has_value());
  EXPECT_EQ(Found->size(), 5u);
  Mgr.disable();
  rmDir(D);
}

// V13: CompilationCacheManager lookupIR with storeIR round-trip
TEST(IRCacheTest, ManagerLookupIRRoundTrip) {
  std::string D = makeTempDir();
  CompilationCacheManager Mgr;
  Mgr.enable(D, 1<<20);
  CacheOptions O; auto K = CacheKey::compute("ir_test", O);
  ir::IRTypeContext Ctx;
  ir::IRModule M("test_module", Ctx);

  // Store
  ASSERT_TRUE(Mgr.storeIR(K, M));

  // Lookup
  auto Found = Mgr.lookupIR(K, Ctx);
  ASSERT_TRUE(Found.has_value());
  EXPECT_NE(Found->get(), nullptr);
  EXPECT_EQ((*Found)->getName(), "test_module");
  Mgr.disable();
  rmDir(D);
}

// V14: CompilationCacheManager storeIR round-trip
TEST(IRCacheTest, ManagerStoreIRRoundTrip) {
  std::string D = makeTempDir();
  CompilationCacheManager Mgr;
  Mgr.enable(D, 1<<20);
  CacheOptions O; auto K = CacheKey::compute("ir_store", O);
  ir::IRTypeContext Ctx;
  ir::IRModule M("store_test", Ctx);

  // Store and verify
  ASSERT_TRUE(Mgr.storeIR(K, M));

  // Read back and verify name matches
  auto Found = Mgr.lookupIR(K, Ctx);
  ASSERT_TRUE(Found.has_value());
  EXPECT_EQ((*Found)->getName(), "store_test");
  Mgr.disable();
  rmDir(D);
}

// V15: CacheOptions 默认版本
TEST(IRCacheTest, CacheOptionsDefaultVersion) {
  CacheOptions O;
  EXPECT_EQ(O.Version.Major, ir::IRFormatVersion::Current().Major);
  EXPECT_EQ(O.Version.Minor, ir::IRFormatVersion::Current().Minor);
}
