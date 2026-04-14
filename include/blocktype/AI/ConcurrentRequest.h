#pragma once

#include "AIInterface.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Error.h"
#include <vector>
#include <future>
#include <memory>
#include <mutex>
#include <atomic>

namespace blocktype {

/// 并发请求结果
struct ConcurrentResult {
  unsigned RequestId;
  AIResponse Response;
  bool Success;
  std::string ErrorMessage;
};

/// 并发请求配置
struct ConcurrentConfig {
  unsigned MaxConcurrentRequests = 5;  // 最大并发数
  unsigned TimeoutMs = 60000;          // 总超时时间
  bool FailFast = false;               // 快速失败模式
};

/// 并发请求管理器
class ConcurrentRequestManager {
  ConcurrentConfig Config;
  std::atomic<unsigned> ActiveRequests{0};
  mutable std::mutex Mutex;

public:
  ConcurrentRequestManager(const ConcurrentConfig& Cfg = ConcurrentConfig{})
    : Config(Cfg) {}

  /// 并发发送多个请求
  /// @param Provider AI 提供者
  /// @param Requests 请求列表
  /// @return 所有请求的结果
  std::vector<ConcurrentResult> sendConcurrent(
    AIInterface& Provider,
    const std::vector<AIRequest>& Requests
  );

  /// 并发发送请求（带进度回调）
  /// @param Provider AI 提供者
  /// @param Requests 请求列表
  /// @param ProgressCallback 进度回调（完成数量，总数）
  /// @return 所有请求的结果
  std::vector<ConcurrentResult> sendConcurrentWithProgress(
    AIInterface& Provider,
    const std::vector<AIRequest>& Requests,
    std::function<void(unsigned, unsigned)> ProgressCallback
  );

  /// 获取当前活跃请求数
  unsigned getActiveRequestCount() const { return ActiveRequests.load(); }

  /// 获取配置
  const ConcurrentConfig& getConfig() const { return Config; }

  /// 设置配置
  void setConfig(const ConcurrentConfig& Cfg) { Config = Cfg; }

  /// 等待所有请求完成
  void waitForAll();

private:
  /// 发送单个请求（内部）
  std::future<ConcurrentResult> sendSingle(
    AIInterface& Provider,
    const AIRequest& Request,
    unsigned RequestId
  );
};

/// 批处理错误处理策略
enum class BatchErrorStrategy {
  Continue,      // 继续执行剩余请求
  StopOnFailure, // 遇到失败立即停止
  RetryFailed    // 重试失败的请求
};

/// 批处理配置
struct BatchConfig {
  ConcurrentConfig Concurrent;
  BatchErrorStrategy ErrorStrategy = BatchErrorStrategy::Continue;
  unsigned MaxRetries = 3;              // 最大重试次数
  std::chrono::milliseconds RetryDelay{1000};  // 重试延迟
  bool EnableResultCallback = false;    // 启用单个结果回调
};

/// 请求批处理器
class RequestBatch {
  std::vector<AIRequest> Requests;
  std::vector<ConcurrentResult> Results;
  BatchConfig Config;
  std::atomic<bool> Cancelled{false};

public:
  RequestBatch(const BatchConfig& Cfg = BatchConfig{})
    : Config(Cfg) {}

  /// 添加请求
  void addRequest(const AIRequest& Request) {
    Requests.push_back(Request);
  }

  /// 批量添加请求
  void addRequests(const std::vector<AIRequest>& NewRequests) {
    Requests.insert(Requests.end(), NewRequests.begin(), NewRequests.end());
  }

  /// 清空请求
  void clear() {
    Requests.clear();
    Results.clear();
    Cancelled = false;
  }

  /// 获取请求数量
  unsigned size() const { return Requests.size(); }

  /// 取消批处理
  void cancel() { Cancelled = true; }

  /// 是否已取消
  bool isCancelled() const { return Cancelled.load(); }

  /// 执行批处理
  /// @param Provider AI 提供者
  /// @return 是否全部成功
  bool execute(AIInterface& Provider);

  /// 执行批处理（带进度）
  bool executeWithProgress(
    AIInterface& Provider,
    std::function<void(unsigned, unsigned)> ProgressCallback
  );

  /// 执行批处理（带结果回调）
  bool executeWithCallback(
    AIInterface& Provider,
    std::function<void(const ConcurrentResult&)> ResultCallback
  );

  /// 执行批处理（完整版）
  bool executeFull(
    AIInterface& Provider,
    std::function<void(unsigned, unsigned)> ProgressCallback,
    std::function<void(const ConcurrentResult&)> ResultCallback
  );

  /// 重试失败的请求
  bool retryFailed(AIInterface& Provider);

  /// 获取结果
  const std::vector<ConcurrentResult>& getResults() const { return Results; }

  /// 获取成功的请求
  std::vector<ConcurrentResult> getSuccessfulResults() const;

  /// 获取失败的请求
  std::vector<ConcurrentResult> getFailedResults() const;

  /// 获取成功率
  double getSuccessRate() const;

  /// 获取配置
  const BatchConfig& getConfig() const { return Config; }

  /// 设置配置
  void setConfig(const BatchConfig& Cfg) { Config = Cfg; }

private:
  /// 执行单个请求并处理重试
  ConcurrentResult executeWithRetry(
    AIInterface& Provider,
    const AIRequest& Request,
    unsigned RequestId
  );
};

} // namespace blocktype