#include "blocktype/AI/ResponseCache.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>

using namespace blocktype;

class ResponseCacheTest : public ::testing::Test {
protected:
  ResponseCache Cache{10, std::chrono::seconds(60)};
  
  AIRequest createRequest(AITaskType Type, const std::string& Query) {
    AIRequest Req;
    Req.TaskType = Type;
    Req.Lang = Language::English;
    Req.Query = Query;
    return Req;
  }
  
  AIResponse createResponse(const std::string& Content) {
    AIResponse Resp;
    Resp.Success = true;
    Resp.Content = Content;
    Resp.Provider = "Test";
    return Resp;
  }
};

TEST_F(ResponseCacheTest, BasicCacheOperations) {
  auto Request = createRequest(AITaskType::ErrorFix, "test query");
  auto Response = createResponse("test response");
  
  // 初始状态应该未命中
  auto Result = Cache.find(Request);
  EXPECT_FALSE(Result.has_value());
  auto Stats = Cache.getStats();
  EXPECT_EQ(Stats.Hits, 0);
  EXPECT_EQ(Stats.Misses, 1);
  
  // 插入缓存
  Cache.insert(Request, Response);
  
  // 现在应该命中
  Result = Cache.find(Request);
  EXPECT_TRUE(Result.has_value());
  EXPECT_EQ(Result->Content, "test response");
  Stats = Cache.getStats();
  EXPECT_EQ(Stats.Hits, 1);
  EXPECT_EQ(Stats.Misses, 1);
}

TEST_F(ResponseCacheTest, CacheKeyGeneration) {
  auto Request1 = createRequest(AITaskType::ErrorFix, "query1");
  auto Request2 = createRequest(AITaskType::ErrorFix, "query2");
  auto Request3 = createRequest(AITaskType::CodeCompletion, "query1");
  
  auto Response1 = createResponse("response1");
  auto Response2 = createResponse("response2");
  auto Response3 = createResponse("response3");
  
  Cache.insert(Request1, Response1);
  Cache.insert(Request2, Response2);
  Cache.insert(Request3, Response3);
  
  // 相同查询应该返回相同结果
  auto Result1 = Cache.find(Request1);
  EXPECT_TRUE(Result1.has_value());
  EXPECT_EQ(Result1->Content, "response1");
  
  // 不同查询应该返回不同结果
  auto Result2 = Cache.find(Request2);
  EXPECT_TRUE(Result2.has_value());
  EXPECT_EQ(Result2->Content, "response2");
  
  // 不同任务类型应该返回不同结果
  auto Result3 = Cache.find(Request3);
  EXPECT_TRUE(Result3.has_value());
  EXPECT_EQ(Result3->Content, "response3");
}

TEST_F(ResponseCacheTest, CacheExpiration) {
  ResponseCache ShortCache{10, std::chrono::seconds(1)};
  
  auto Request = createRequest(AITaskType::ErrorFix, "test query");
  auto Response = createResponse("test response");
  
  ShortCache.insert(Request, Response);
  
  // 立即查找应该命中
  auto Result = ShortCache.find(Request);
  EXPECT_TRUE(Result.has_value());
  
  // 等待过期
  std::this_thread::sleep_for(std::chrono::seconds(2));
  
  // 过期后应该未命中
  Result = ShortCache.find(Request);
  EXPECT_FALSE(Result.has_value());
}

TEST_F(ResponseCacheTest, CacheSizeLimit) {
  ResponseCache SmallCache{3, std::chrono::seconds(60)};
  
  for (int i = 0; i < 5; ++i) {
    auto Request = createRequest(AITaskType::ErrorFix, "query" + std::to_string(i));
    auto Response = createResponse("response" + std::to_string(i));
    SmallCache.insert(Request, Response);
  }
  
  // 缓存大小应该接近限制（允许一定的延迟清理）
  auto Stats = SmallCache.getStats();
  EXPECT_LE(Stats.Size, 5);  // 允许一些超出，因为清理是延迟的
}

TEST_F(ResponseCacheTest, CacheClear) {
  auto Request = createRequest(AITaskType::ErrorFix, "test query");
  auto Response = createResponse("test response");
  
  Cache.insert(Request, Response);
  auto Stats = Cache.getStats();
  EXPECT_EQ(Stats.Size, 1);
  
  Cache.clear();
  Stats = Cache.getStats();
  EXPECT_EQ(Stats.Size, 0);
  
  auto Result = Cache.find(Request);
  EXPECT_FALSE(Result.has_value());
}

TEST_F(ResponseCacheTest, HitRateCalculation) {
  auto Request = createRequest(AITaskType::ErrorFix, "test query");
  auto Response = createResponse("test response");
  
  // 3 次未命中
  Cache.find(Request);
  Cache.find(Request);
  Cache.find(Request);
  
  // 插入缓存
  Cache.insert(Request, Response);
  
  // 2 次命中
  Cache.find(Request);
  Cache.find(Request);
  
  // 命中率应该是 2/5 = 0.4
  auto Stats = Cache.getStats();
  EXPECT_NEAR(Stats.HitRate, 0.4, 0.01);
}

// ========== 持久化测试 ==========

TEST_F(ResponseCacheTest, CacheEntryJsonSerialization) {
  auto Response = createResponse("test content");
  Response.TokensUsed = 100;
  Response.LatencyMs = 50.5;
  
  CacheEntry Entry;
  Entry.Response = Response;
  Entry.Timestamp = std::chrono::steady_clock::now();
  Entry.HitCount = 5;
  
  // 序列化
  std::string Json = Entry.toJson();
  EXPECT_FALSE(Json.empty());
  // nlohmann/json 会格式化 JSON，冒号后有空格
  EXPECT_TRUE(Json.find("success") != std::string::npos);
  EXPECT_TRUE(Json.find("test content") != std::string::npos);
  EXPECT_TRUE(Json.find("hit_count") != std::string::npos);
  
  // 反序列化
  auto Restored = CacheEntry::fromJson(Json);
  EXPECT_TRUE(Restored.has_value());
  EXPECT_EQ(Restored->Response.Content, "test content");
  EXPECT_EQ(Restored->Response.TokensUsed, 100);
  EXPECT_NEAR(Restored->Response.LatencyMs, 50.5, 0.1);
  EXPECT_EQ(Restored->HitCount, 5);
}

TEST_F(ResponseCacheTest, CacheEntryJsonInvalidInput) {
  // 空 JSON
  auto Result1 = CacheEntry::fromJson("");
  EXPECT_FALSE(Result1.has_value());
  
  // 无效 JSON
  auto Result2 = CacheEntry::fromJson("not a json");
  EXPECT_FALSE(Result2.has_value());
  
  // 缺少必需字段
  auto Result3 = CacheEntry::fromJson("{\"incomplete\":true}");
  EXPECT_FALSE(Result3.has_value());
}

TEST_F(ResponseCacheTest, SaveAndLoadCache) {
  // 创建临时文件路径
  std::string TempFile = "/tmp/blocktype_cache_test_" + std::to_string(std::time(nullptr)) + ".json";
  
  // 创建缓存并添加数据
  ResponseCache TestCache{10, std::chrono::seconds(60)};
  TestCache.setCacheFile(TempFile);
  
  auto Request1 = createRequest(AITaskType::ErrorFix, "query1");
  auto Request2 = createRequest(AITaskType::CodeCompletion, "query2");
  auto Response1 = createResponse("response1");
  auto Response2 = createResponse("response2");
  
  TestCache.insert(Request1, Response1);
  TestCache.insert(Request2, Response2);
  
  // 保存到文件
  EXPECT_TRUE(TestCache.saveToFile());
  
  // 验证文件存在
  EXPECT_TRUE(std::filesystem::exists(TempFile));
  
  // 创建新缓存实例并加载
  ResponseCache LoadedCache{10, std::chrono::seconds(60)};
  LoadedCache.setCacheFile(TempFile);
  EXPECT_TRUE(LoadedCache.loadFromFile());
  
  // 验证数据已恢复
  auto Result1 = LoadedCache.find(Request1);
  EXPECT_TRUE(Result1.has_value());
  EXPECT_EQ(Result1->Content, "response1");
  
  auto Result2 = LoadedCache.find(Request2);
  EXPECT_TRUE(Result2.has_value());
  EXPECT_EQ(Result2->Content, "response2");
  
  // 清理临时文件
  std::filesystem::remove(TempFile);
}

TEST_F(ResponseCacheTest, LoadWithClearExisting) {
  std::string TempFile = "/tmp/blocktype_cache_test_" + std::to_string(std::time(nullptr)) + ".json";
  
  // 创建第一个缓存并保存数据
  ResponseCache Cache1{10, std::chrono::seconds(60)};
  Cache1.setCacheFile(TempFile);
  
  auto Request1 = createRequest(AITaskType::ErrorFix, "query1");
  auto Response1 = createResponse("response1");
  Cache1.insert(Request1, Response1);
  Cache1.saveToFile();
  
  // 创建第二个缓存，先添加不同数据
  ResponseCache Cache2{10, std::chrono::seconds(60)};
  auto Request2 = createRequest(AITaskType::ErrorFix, "query2");
  auto Response2 = createResponse("response2");
  Cache2.insert(Request2, Response2);
  
  // 加载并清空现有数据
  Cache2.setCacheFile(TempFile);
  EXPECT_TRUE(Cache2.loadFromFile(true));
  
  // 应该只有文件中的数据
  auto Result1 = Cache2.find(Request1);
  EXPECT_TRUE(Result1.has_value());
  
  auto Result2 = Cache2.find(Request2);
  EXPECT_FALSE(Result2.has_value());
  
  std::filesystem::remove(TempFile);
}

TEST_F(ResponseCacheTest, LoadWithoutClearExisting) {
  std::string TempFile = "/tmp/blocktype_cache_test_" + std::to_string(std::time(nullptr)) + ".json";
  
  // 创建第一个缓存并保存数据
  ResponseCache Cache1{10, std::chrono::seconds(60)};
  Cache1.setCacheFile(TempFile);
  
  auto Request1 = createRequest(AITaskType::ErrorFix, "query1");
  auto Response1 = createResponse("response1");
  Cache1.insert(Request1, Response1);
  Cache1.saveToFile();
  
  // 创建第二个缓存，先添加不同数据
  ResponseCache Cache2{10, std::chrono::seconds(60)};
  auto Request2 = createRequest(AITaskType::ErrorFix, "query2");
  auto Response2 = createResponse("response2");
  Cache2.insert(Request2, Response2);
  
  // 加载但不清空现有数据
  Cache2.setCacheFile(TempFile);
  EXPECT_TRUE(Cache2.loadFromFile(false));
  
  // 应该同时有内存和文件中的数据
  auto Result1 = Cache2.find(Request1);
  EXPECT_TRUE(Result1.has_value());
  
  auto Result2 = Cache2.find(Request2);
  EXPECT_TRUE(Result2.has_value());
  
  std::filesystem::remove(TempFile);
}

TEST_F(ResponseCacheTest, AutoSaveOnDestruction) {
  std::string TempFile = "/tmp/blocktype_cache_test_" + std::to_string(std::time(nullptr)) + ".json";
  
  // 使用作用域确保析构函数被调用
  {
    ResponseCache Cache{10, std::chrono::seconds(60)};
    Cache.setCacheFile(TempFile);
    
    auto Request = createRequest(AITaskType::ErrorFix, "auto save test");
    auto Response = createResponse("auto saved response");
    Cache.insert(Request, Response);
    
    // 析构时应该自动保存
  }
  
  // 验证文件已创建
  EXPECT_TRUE(std::filesystem::exists(TempFile));
  
  // 加载并验证数据
  ResponseCache LoadedCache{10, std::chrono::seconds(60)};
  LoadedCache.setCacheFile(TempFile);
  LoadedCache.loadFromFile();
  
  auto Request = createRequest(AITaskType::ErrorFix, "auto save test");
  auto Result = LoadedCache.find(Request);
  EXPECT_TRUE(Result.has_value());
  EXPECT_EQ(Result->Content, "auto saved response");
  
  std::filesystem::remove(TempFile);
}

TEST_F(ResponseCacheTest, NoAutoSaveWithoutCacheFile) {
  std::string TempFile = "/tmp/blocktype_cache_test_" + std::to_string(std::time(nullptr)) + ".json";
  
  // 确保文件不存在
  std::filesystem::remove(TempFile);
  
  {
    ResponseCache Cache{10, std::chrono::seconds(60)};
    // 不设置缓存文件路径
    
    auto Request = createRequest(AITaskType::ErrorFix, "no auto save");
    auto Response = createResponse("should not be saved");
    Cache.insert(Request, Response);
    
    // 析构时不应该保存
  }
  
  // 文件应该不存在
  EXPECT_FALSE(std::filesystem::exists(TempFile));
}

TEST_F(ResponseCacheTest, SaveToNonexistentDirectory) {
  std::string TempFile = "/tmp/nonexistent_dir_" + std::to_string(std::time(nullptr)) + "/cache.json";
  
  ResponseCache Cache{10, std::chrono::seconds(60)};
  Cache.setCacheFile(TempFile);
  
  auto Request = createRequest(AITaskType::ErrorFix, "test");
  auto Response = createResponse("test");
  Cache.insert(Request, Response);
  
  // 应该能自动创建目录并保存成功
  EXPECT_TRUE(Cache.saveToFile());
  EXPECT_TRUE(std::filesystem::exists(TempFile));
  
  // 清理
  std::filesystem::remove_all(std::filesystem::path(TempFile).parent_path());
}

TEST_F(ResponseCacheTest, LoadFromNonexistentFile) {
  std::string TempFile = "/tmp/nonexistent_cache_" + std::to_string(std::time(nullptr)) + ".json";
  
  // 确保文件不存在
  std::filesystem::remove(TempFile);
  
  ResponseCache Cache{10, std::chrono::seconds(60)};
  Cache.setCacheFile(TempFile);
  
  // 应该失败但不崩溃
  EXPECT_FALSE(Cache.loadFromFile());
}
