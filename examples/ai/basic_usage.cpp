/**
 * BlockType AI 基础使用示例
 * 
 * 本示例演示如何使用 BlockType AI 模块的基本功能
 */

#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/OpenAIProvider.h"
#include "blocktype/AI/Providers/ClaudeProvider.h"
#include "blocktype/AI/Providers/LocalProvider.h"
#include "blocktype/AI/Providers/QwenProvider.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>

using namespace blocktype;

int main() {
  // 1. 创建配置
  AIConfig Config;
  Config.EnableCache = true;
  Config.MaxCostPerDay = 5.0;
  Config.TimeoutMs = 30000;
  
  // 2. 创建编排器
  AIOrchestrator Orchestrator(Config);
  
  // 3. 注册提供者（根据环境变量）
  if (const char* Key = std::getenv("OPENAI_API_KEY")) {
    Orchestrator.registerProvider(std::make_unique<OpenAIProvider>(Key, "gpt-4"));
    std::cout << "✓ OpenAI provider registered\n";
  }
  
  if (const char* Key = std::getenv("ANTHROPIC_API_KEY")) {
    Orchestrator.registerProvider(std::make_unique<ClaudeProvider>(Key));
    std::cout << "✓ Claude provider registered\n";
  }
  
  if (const char* Key = std::getenv("QWEN_API_KEY")) {
    Orchestrator.registerProvider(std::make_unique<QwenProvider>(Key));
    std::cout << "✓ Qwen provider registered\n";
  }
  
  // 始终注册本地提供者作为回退
  Orchestrator.registerProvider(std::make_unique<LocalProvider>());
  std::cout << "✓ Local provider registered\n";
  
  // 4. 创建请求
  AIRequest Request;
  Request.TaskType = AITaskType::ErrorFix;
  Request.Lang = Language::English;
  Request.Query = "Explain and fix the error in this code";
  Request.Context = R"(
int main() {
  std::cout << "Hello World"  // Missing semicolon
  return 0
}
)";
  Request.SourceFile = "example.cpp";
  Request.Line = 3;
  
  // 5. 发送请求
  std::cout << "\nSending request to AI...\n";
  auto Result = Orchestrator.sendRequest(Request);
  
  // 6. 处理响应
  if (Result) {
    std::cout << "\n=== AI Response ===\n";
    std::cout << "Provider: " << Result->Provider << "\n";
    std::cout << "Tokens: " << Result->TokensUsed << "\n";
    std::cout << "Latency: " << Result->LatencyMs << "ms\n";
    std::cout << "\nContent:\n" << Result->Content << "\n";
    
    if (!Result->Suggestions.empty()) {
      std::cout << "\nSuggestions:\n";
      for (const auto& Suggestion : Result->Suggestions) {
        std::cout << "  - " << Suggestion << "\n";
      }
    }
  } else {
    llvm::handleAllErrors(
      Result.takeError(),
      [](const llvm::StringError& E) {
        std::cerr << "Error: " << E.getMessage() << "\n";
      }
    );
    return 1;
  }
  
  return 0;
}
