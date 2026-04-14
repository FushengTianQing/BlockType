/**
 * BlockType AI 高级功能示例
 * 
 * 本示例演示缓存、成本追踪和异步请求等高级功能
 */

#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/OpenAIProvider.h"
#include "blocktype/AI/ResponseCache.h"
#include "blocktype/AI/CostTracker.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace blocktype;

void demonstrateCache() {
  std::cout << "\n=== Cache Demo ===\n";
  
  // 创建缓存（最大 100 条，TTL 1 小时）
  ResponseCache Cache{100, std::chrono::hours(1)};
  
  AIRequest Request;
  Request.TaskType = AITaskType::ErrorFix;
  Request.Query = "test query";
  
  // 第一次查找 - 未命中
  auto Result = Cache.find(Request);
  std::cout << "First lookup: " << (Result ? "Hit" : "Miss") << "\n";
  
  // 插入缓存
  AIResponse Response;
  Response.Success = true;
  Response.Content = "Cached response";
  Cache.insert(Request, Response);
  
  // 第二次查找 - 命中
  Result = Cache.find(Request);
  std::cout << "Second lookup: " << (Result ? "Hit" : "Miss") << "\n";
  
  if (Result) {
    std::cout << "Content: " << Result->Content << "\n";
  }
  
  // 查看缓存统计
  auto Stats = Cache.getStats();
  std::cout << "Cache stats:\n";
  std::cout << "  Size: " << Stats.Size << "\n";
  std::cout << "  Hits: " << Stats.Hits << "\n";
  std::cout << "  Misses: " << Stats.Misses << "\n";
  std::cout << "  Hit rate: " << (Stats.HitRate * 100) << "%\n";
}

void demonstrateCostTracking() {
  std::cout << "\n=== Cost Tracking Demo ===\n";
  
  // 创建成本追踪器（每日限制 $5）
  CostTracker Tracker{5.0};
  
  // 记录几次使用
  CostRecord Record1;
  Record1.Provider = AIProvider::OpenAI;
  Record1.TaskType = AITaskType::ErrorFix;
  Record1.TokensUsed = 1000;
  Record1.Cost = 0.03;
  Tracker.record(Record1);
  
  CostRecord Record2;
  Record2.Provider = AIProvider::Claude;
  Record2.TaskType = AITaskType::CodeCompletion;
  Record2.TokensUsed = 2000;
  Record2.Cost = 0.06;
  Tracker.record(Record2);
  
  // 查看成本
  std::cout << "Today's cost: $" << Tracker.getTodayCost() << "\n";
  std::cout << "Over limit: " << (Tracker.isOverLimit() ? "Yes" : "No") << "\n";
  
  // 获取详细报告
  auto Report = Tracker.getReport();
  std::cout << "Total requests: " << Report.TotalRequests << "\n";
  std::cout << "Cost by provider:\n";
  for (const auto& [Provider, Cost] : Report.CostByProvider) {
    std::cout << "  " << static_cast<int>(Provider) << ": $" << Cost << "\n";
  }
}

void demonstrateAsyncRequest() {
  std::cout << "\n=== Async Request Demo ===\n";
  
  AIConfig Config;
  AIOrchestrator Orchestrator(Config);
  
  // 注册本地提供者（用于演示）
  Orchestrator.registerProvider(std::make_unique<LocalProvider>());
  
  AIRequest Request;
  Request.TaskType = AITaskType::Explanation;
  Request.Query = "Explain async requests";
  
  std::cout << "Sending async request...\n";
  
  bool Completed = false;
  Orchestrator.sendRequestAsync(Request, [&Completed](llvm::Expected<AIResponse> Result) {
    if (Result) {
      std::cout << "Async response received: " << Result->Content.substr(0, 50) << "...\n";
    } else {
      std::cout << "Async request failed\n";
    }
    Completed = true;
  });
  
  // 等待异步完成
  int WaitCount = 0;
  while (!Completed && WaitCount < 100) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ++WaitCount;
  }
  
  std::cout << "Async request " << (Completed ? "completed" : "timed out") << "\n";
}

void demonstrateProviderSelection() {
  std::cout << "\n=== Provider Selection Demo ===\n";
  
  AIConfig Config;
  AIOrchestrator Orchestrator(Config);
  
  // 注册多个提供者
  Orchestrator.registerProvider(std::make_unique<LocalProvider>());
  
  // 选择提供者
  auto Provider = Orchestrator.selectProvider(AITaskType::ErrorFix, Language::Chinese);
  if (Provider) {
    std::cout << "Selected provider for Chinese error fix: " 
              << Provider->getModelName() << "\n";
  }
  
  Provider = Orchestrator.selectProvider(AITaskType::CodeCompletion, Language::English);
  if (Provider) {
    std::cout << "Selected provider for English code completion: " 
              << Provider->getModelName() << "\n";
  }
  
  // 查看所有提供者状态
  auto Status = Orchestrator.getProvidersStatus();
  std::cout << "Provider status:\n";
  for (const auto& [Prov, Available] : Status) {
    std::cout << "  Provider " << static_cast<int>(Prov) 
              << ": " << (Available ? "Available" : "Unavailable") << "\n";
  }
}

int main() {
  std::cout << "BlockType AI Advanced Features Demo\n";
  std::cout << "===================================\n";
  
  demonstrateCache();
  demonstrateCostTracking();
  demonstrateAsyncRequest();
  demonstrateProviderSelection();
  
  std::cout << "\nDemo completed!\n";
  return 0;
}
