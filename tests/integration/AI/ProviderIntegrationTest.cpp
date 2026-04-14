#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/LocalProvider.h"
#include "blocktype/AI/ResponseCache.h"
#include "blocktype/AI/CostTracker.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

using namespace blocktype;

// Mock Provider 用于测试
class MockProvider : public AIInterface {
  std::string ModelName;
  bool Available;
  
public:
  MockProvider(llvm::StringRef Model, bool Avail = true)
    : ModelName(Model.str()), Available(Avail) {}
  
  llvm::Expected<AIResponse> sendRequest(const AIRequest& Request) override {
    AIResponse Response;
    Response.Success = true;
    Response.Content = "Mock response for: " + Request.Query;
    Response.Provider = "Mock";
    Response.TokensUsed = 100;
    Response.LatencyMs = 50;
    return Response;
  }
  
  void sendRequestAsync(
    const AIRequest& Request,
    std::function<void(llvm::Expected<AIResponse>)> Callback
  ) override {
    auto Result = sendRequest(Request);
    Callback(std::move(Result));
  }
  
  bool isAvailable() const override { return Available; }
  
  AIProvider getProvider() const override {
    return AIProvider::Local;  // Mock as Local
  }
  
  std::string getModelName() const override { return ModelName; }
  
  std::vector<AITaskType> getSupportedTasks() const override {
    return {
      AITaskType::ErrorFix,
      AITaskType::CodeCompletion,
      AITaskType::PerformanceAdvice
    };
  }
};

class ProviderIntegrationTest : public ::testing::Test {
protected:
  AIConfig Config;
  
  AIRequest createRequest(AITaskType Type, const std::string& Query) {
    AIRequest Req;
    Req.TaskType = Type;
    Req.Lang = Language::English;
    Req.Query = Query;
    Req.Context = "int main() { return 0; }";
    return Req;
  }
};

// 测试完整的 AI 流程
TEST_F(ProviderIntegrationTest, FullWorkflow) {
  // 创建编排器
  AIOrchestrator Orchestrator(Config);
  
  // 注册 Mock Provider
  Orchestrator.registerProvider(std::make_unique<MockProvider>("test-model"));
  
  // 创建请求
  auto Request = createRequest(AITaskType::ErrorFix, "Fix this error");
  
  // 发送请求
  auto Result = Orchestrator.sendRequest(Request);
  
  ASSERT_TRUE(static_cast<bool>(Result));
  EXPECT_TRUE(Result->Success);
  EXPECT_EQ(Result->Provider, "Mock");
  EXPECT_EQ(Result->TokensUsed, 100);
}

// 测试 Provider 选择
TEST_F(ProviderIntegrationTest, ProviderSelection) {
  AIOrchestrator Orchestrator(Config);
  
  // 注册多个 Provider
  Orchestrator.registerProvider(std::make_unique<MockProvider>("model-1"));
  Orchestrator.registerProvider(std::make_unique<MockProvider>("model-2"));
  
  // 选择 Provider
  auto Provider = Orchestrator.selectProvider(AITaskType::ErrorFix, Language::English);
  
  ASSERT_NE(Provider, nullptr);
  EXPECT_EQ(Provider->getProvider(), AIProvider::Local);
}

// 测试缓存集成
TEST_F(ProviderIntegrationTest, CacheIntegration) {
  ResponseCache Cache{10, std::chrono::seconds(60)};
  
  auto Request = createRequest(AITaskType::ErrorFix, "test query");
  
  // 第一次查询 - 未命中
  auto Result = Cache.find(Request);
  EXPECT_FALSE(Result.has_value());
  
  // 插入缓存
  AIResponse Response;
  Response.Success = true;
  Response.Content = "Cached response";
  Cache.insert(Request, Response);
  
  // 第二次查询 - 命中
  Result = Cache.find(Request);
  EXPECT_TRUE(Result.has_value());
  EXPECT_EQ(Result->Content, "Cached response");
}

// 测试成本追踪集成
TEST_F(ProviderIntegrationTest, CostTrackingIntegration) {
  CostTracker Tracker{5.0};
  
  // 记录成本
  CostRecord Record;
  Record.Provider = AIProvider::OpenAI;
  Record.TaskType = AITaskType::ErrorFix;
  Record.TokensUsed = 1000;
  Record.Cost = 0.03;
  Record.Timestamp = std::chrono::steady_clock::now();
  
  Tracker.record(Record);
  
  // 检查成本
  EXPECT_NEAR(Tracker.getTodayCost(), 0.03, 0.001);
  EXPECT_FALSE(Tracker.isOverLimit());
  
  // 获取报告
  auto Report = Tracker.getReport();
  EXPECT_EQ(Report.TotalRequests, 1);
}

// 测试异步请求
TEST_F(ProviderIntegrationTest, AsyncRequest) {
  AIOrchestrator Orchestrator(Config);
  Orchestrator.registerProvider(std::make_unique<MockProvider>("async-model"));
  
  auto Request = createRequest(AITaskType::CodeCompletion, "Complete this code");
  
  bool CallbackCalled = false;
  AIResponse ReceivedResponse;
  
  Orchestrator.sendRequestAsync(Request, [&CallbackCalled, &ReceivedResponse](llvm::Expected<AIResponse> Result) {
    if (Result) {
      CallbackCalled = true;
      ReceivedResponse = *Result;
    }
  });
  
  // 等待异步完成（简单测试，实际应该用条件变量）
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  EXPECT_TRUE(CallbackCalled);
  EXPECT_TRUE(ReceivedResponse.Success);
}

// 测试 Provider 状态
TEST_F(ProviderIntegrationTest, ProviderStatus) {
  AIOrchestrator Orchestrator(Config);
  
  Orchestrator.registerProvider(std::make_unique<MockProvider>("available", true));
  Orchestrator.registerProvider(std::make_unique<MockProvider>("unavailable", false));
  
  auto Status = Orchestrator.getProvidersStatus();
  
  EXPECT_EQ(Status.size(), 2);
  
  // 检查至少有一个可用的 Provider
  bool HasAvailable = false;
  for (const auto& [Provider, Available] : Status) {
    if (Available) {
      HasAvailable = true;
      break;
    }
  }
  EXPECT_TRUE(HasAvailable);
}

// 测试配置管理
TEST_F(ProviderIntegrationTest, ConfigurationManagement) {
  AIOrchestrator Orchestrator(Config);
  
  // 获取配置
  auto& CurrentConfig = Orchestrator.getConfig();
  EXPECT_EQ(CurrentConfig.MaxCostPerDay, 10.0);
  
  // 设置新配置
  AIConfig NewConfig;
  NewConfig.MaxCostPerDay = 20.0;
  NewConfig.EnableCache = false;
  
  Orchestrator.setConfig(NewConfig);
  
  // 验证配置更新
  auto& UpdatedConfig = Orchestrator.getConfig();
  EXPECT_EQ(UpdatedConfig.MaxCostPerDay, 20.0);
  EXPECT_FALSE(UpdatedConfig.EnableCache);
}

// 测试本地 Provider（Ollama）
TEST_F(ProviderIntegrationTest, LocalProviderBasic) {
  LocalProvider Provider("http://localhost:11434", "codellama");
  
  EXPECT_EQ(Provider.getProvider(), AIProvider::Local);
  EXPECT_EQ(Provider.getModelName(), "codellama");
  EXPECT_TRUE(Provider.isAvailable());
  
  auto Tasks = Provider.getSupportedTasks();
  EXPECT_EQ(Tasks.size(), 6);
}

// 测试错误处理
TEST_F(ProviderIntegrationTest, ErrorHandling) {
  AIOrchestrator Orchestrator(Config);
  
  // 不注册任何 Provider
  auto Request = createRequest(AITaskType::ErrorFix, "test");
  
  // 应该返回 nullptr
  auto Provider = Orchestrator.selectProvider(AITaskType::ErrorFix, Language::English);
  EXPECT_EQ(Provider, nullptr);
}