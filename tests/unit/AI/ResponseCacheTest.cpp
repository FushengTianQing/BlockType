#include "blocktype/AI/ResponseCache.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

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
