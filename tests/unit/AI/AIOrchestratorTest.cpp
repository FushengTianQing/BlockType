#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/OpenAIProvider.h"
#include "blocktype/AI/Providers/ClaudeProvider.h"
#include "blocktype/AI/Providers/LocalProvider.h"
#include "blocktype/AI/Providers/QwenProvider.h"
#include <gtest/gtest.h>

using namespace blocktype;

class AIOrchestratorTest : public ::testing::Test {
protected:
  AIConfig Config;
  AIOrchestrator Orchestrator;
  
  AIOrchestratorTest() : Orchestrator(Config) {
    Config.EnableCache = false;  // 测试时禁用缓存
  }
  
  AIRequest createRequest(AITaskType Type, Language Lang, const std::string& Query) {
    AIRequest Req;
    Req.TaskType = Type;
    Req.Lang = Lang;
    Req.Query = Query;
    return Req;
  }
};

TEST_F(AIOrchestratorTest, RegisterProvider) {
  auto Provider = std::make_unique<LocalProvider>("http://localhost:11434", "codellama");
  Orchestrator.registerProvider(std::move(Provider));
  
  EXPECT_EQ(Orchestrator.getProviderCount(), 1);
}

TEST_F(AIOrchestratorTest, ProviderSelection) {
  // 注册所有提供者
  Orchestrator.registerProvider(std::make_unique<LocalProvider>("http://localhost:11434", "codellama"));
  
  // 中文任务应该选择 Local（因为没有注册 Qwen）
  auto Request = createRequest(AITaskType::ErrorFix, Language::Chinese, "测试查询");
  auto Provider = Orchestrator.selectProvider(AITaskType::ErrorFix, Language::Chinese);
  
  EXPECT_NE(Provider, nullptr);
  EXPECT_EQ(Provider->getProvider(), AIProvider::Local);
}

TEST_F(AIOrchestratorTest, ProviderAvailability) {
  auto Provider = std::make_unique<LocalProvider>("http://localhost:11434", "codellama");
  Orchestrator.registerProvider(std::move(Provider));
  
  EXPECT_EQ(Orchestrator.getProviderCount(), 1);
  
  auto Status = Orchestrator.getProvidersStatus();
  EXPECT_EQ(Status.size(), 1);
  EXPECT_EQ(Status[0].first, AIProvider::Local);
}

TEST_F(AIOrchestratorTest, FallbackProvider) {
  // 只注册本地提供者
  Orchestrator.registerProvider(std::make_unique<LocalProvider>("http://localhost:11434", "codellama"));
  
  // 任何任务都应该回退到本地提供者
  auto Request = createRequest(AITaskType::ErrorFix, Language::English, "test query");
  auto Provider = Orchestrator.selectProvider(AITaskType::ErrorFix, Language::English);
  
  EXPECT_NE(Provider, nullptr);
  EXPECT_EQ(Provider->getProvider(), AIProvider::Local);
}

TEST_F(AIOrchestratorTest, NoProviderAvailable) {
  // 没有注册任何提供者
  auto Provider = Orchestrator.selectProvider(AITaskType::ErrorFix, Language::English);
  
  EXPECT_EQ(Provider, nullptr);
}

TEST_F(AIOrchestratorTest, MultipleProviders) {
  // 注册多个提供者
  Orchestrator.registerProvider(std::make_unique<LocalProvider>("http://localhost:11434", "codellama"));
  Orchestrator.registerProvider(std::make_unique<OpenAIProvider>("test-key", "gpt-4"));
  
  EXPECT_EQ(Orchestrator.getProviderCount(), 2);
  
  auto Status = Orchestrator.getProvidersStatus();
  EXPECT_EQ(Status.size(), 2);
}

TEST_F(AIOrchestratorTest, GetProviderInfo) {
  Orchestrator.registerProvider(std::make_unique<LocalProvider>("http://localhost:11434", "codellama"));
  
  auto Provider = Orchestrator.selectProvider(AITaskType::ErrorFix, Language::English);
  
  ASSERT_NE(Provider, nullptr);
  EXPECT_EQ(Provider->getProvider(), AIProvider::Local);
  EXPECT_EQ(Provider->getModelName(), "codellama");
}
