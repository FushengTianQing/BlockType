#pragma once

#include "AIInterface.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <mutex>

namespace blocktype {

class AIOrchestrator {
  llvm::SmallVector<std::unique_ptr<AIInterface>, 4> Providers;
  AIConfig Config;
  mutable std::mutex Mutex;
  
public:
  AIOrchestrator(const AIConfig& Cfg) : Config(Cfg) {}
  
  /// 注册 AI 提供者
  void registerProvider(std::unique_ptr<AIInterface> Provider);
  
  /// 根据任务类型选择最佳提供者
  AIInterface* selectProvider(AITaskType Type, Language Lang);
  
  /// 发送请求（自动选择提供者）
  llvm::Expected<AIResponse> sendRequest(const AIRequest& Request);
  
  /// 异步发送请求
  void sendRequestAsync(
    const AIRequest& Request,
    std::function<void(llvm::Expected<AIResponse>)> Callback
  );
  
  /// 获取所有提供者状态
  std::vector<std::pair<AIProvider, bool>> getProvidersStatus() const;
  
  /// 设置配置
  void setConfig(const AIConfig& Cfg) { 
    std::lock_guard<std::mutex> Lock(Mutex);
    Config = Cfg; 
  }
  
  /// 获取配置
  const AIConfig& getConfig() const { return Config; }
  
  /// 获取提供者数量
  size_t getProviderCount() const { return Providers.size(); }
};

} // namespace blocktype
