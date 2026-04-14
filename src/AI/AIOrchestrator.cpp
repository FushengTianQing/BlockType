#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/OpenAIProvider.h"
#include "blocktype/AI/Providers/ClaudeProvider.h"
#include "blocktype/AI/Providers/LocalProvider.h"
#include "blocktype/AI/Providers/QwenProvider.h"
#include "llvm/Support/raw_ostream.h"

namespace blocktype {

void AIOrchestrator::registerProvider(std::unique_ptr<AIInterface> Provider) {
  std::lock_guard<std::mutex> Lock(Mutex);
  Providers.push_back(std::move(Provider));
}

AIInterface* AIOrchestrator::selectProvider(AITaskType Type, Language Lang) {
  std::lock_guard<std::mutex> Lock(Mutex);
  
  // 根据任务类型和语言选择最佳提供者
  // 中文任务优先使用 Qwen
  if (Lang == Language::Chinese) {
    for (auto& P : Providers) {
      if (P->getProvider() == AIProvider::Qwen && P->isAvailable())
        return P.get();
    }
  }
  
  // 代码生成和错误修复优先使用 Claude
  if (Type == AITaskType::CodeCompletion || Type == AITaskType::ErrorFix) {
    for (auto& P : Providers) {
      if (P->getProvider() == AIProvider::Claude && P->isAvailable())
        return P.get();
    }
  }
  
  // 复杂推理优先使用 OpenAI
  if (Type == AITaskType::PerformanceAdvice || Type == AITaskType::SecurityCheck) {
    for (auto& P : Providers) {
      if (P->getProvider() == AIProvider::OpenAI && P->isAvailable())
        return P.get();
    }
  }
  
  // 默认使用配置的默认提供者
  for (auto& P : Providers) {
    if (P->getProvider() == Config.DefaultProvider && P->isAvailable())
      return P.get();
  }
  
  // 回退到任何可用的提供者
  for (auto& P : Providers) {
    if (P->isAvailable())
      return P.get();
  }
  
  return nullptr;
}

llvm::Expected<AIResponse> AIOrchestrator::sendRequest(const AIRequest& Request) {
  auto* Provider = selectProvider(Request.TaskType, Request.Lang);
  
  if (!Provider) {
    return llvm::make_error<llvm::StringError>(
      "No AI provider available",
      std::make_error_code(std::errc::resource_unavailable_try_again)
    );
  }
  
  return Provider->sendRequest(Request);
}

void AIOrchestrator::sendRequestAsync(
  const AIRequest& Request,
  std::function<void(llvm::Expected<AIResponse>)> Callback
) {
  auto* Provider = selectProvider(Request.TaskType, Request.Lang);
  
  if (!Provider) {
    Callback(llvm::make_error<llvm::StringError>(
      "No AI provider available",
      std::make_error_code(std::errc::resource_unavailable_try_again)
    ));
    return;
  }
  
  Provider->sendRequestAsync(Request, Callback);
}

std::vector<std::pair<AIProvider, bool>> AIOrchestrator::getProvidersStatus() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  
  std::vector<std::pair<AIProvider, bool>> Status;
  for (const auto& P : Providers) {
    Status.push_back({P->getProvider(), P->isAvailable()});
  }
  
  return Status;
}

} // namespace blocktype
