#include "blocktype/AI/ConcurrentRequest.h"
#include <algorithm>
#include <thread>

namespace blocktype {

std::future<ConcurrentResult> ConcurrentRequestManager::sendSingle(
  AIInterface& Provider,
  const AIRequest& Request,
  unsigned RequestId
) {
  return std::async(std::launch::async, [&Provider, Request, RequestId, this]() {
    ConcurrentResult Result;
    Result.RequestId = RequestId;
    Result.Success = false;

    ActiveRequests++;

    auto Response = Provider.sendRequest(Request);
    if (Response) {
      Result.Response = *Response;
      Result.Success = Response->Success;
      if (!Result.Success) {
        Result.ErrorMessage = Response->ErrorMessage;
      }
    } else {
      auto Err = Response.takeError();
      Result.ErrorMessage = toString(std::move(Err));
    }

    ActiveRequests--;
    return Result;
  });
}

std::vector<ConcurrentResult> ConcurrentRequestManager::sendConcurrent(
  AIInterface& Provider,
  const std::vector<AIRequest>& Requests
) {
  return sendConcurrentWithProgress(Provider, Requests, nullptr);
}

std::vector<ConcurrentResult> ConcurrentRequestManager::sendConcurrentWithProgress(
  AIInterface& Provider,
  const std::vector<AIRequest>& Requests,
  std::function<void(unsigned, unsigned)> ProgressCallback
) {
  std::vector<ConcurrentResult> Results;
  Results.reserve(Requests.size());

  if (Requests.empty()) {
    return Results;
  }

  std::vector<std::future<ConcurrentResult>> Futures;
  Futures.reserve(Requests.size());

  unsigned CompletedCount = 0;
  unsigned TotalCount = Requests.size();

  // 分批启动请求（控制并发数）
  for (unsigned i = 0; i < Requests.size(); ++i) {
    // 等待有空闲槽位
    while (ActiveRequests.load() >= Config.MaxConcurrentRequests) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    Futures.push_back(sendSingle(Provider, Requests[i], i));

    // 快速失败模式：检查是否有失败
    if (Config.FailFast) {
      for (auto& F : Futures) {
        if (F.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
          auto Result = F.get();
          if (!Result.Success) {
            // 取消其他请求
            Results.push_back(Result);
            return Results;
          }
          Results.push_back(Result);
          CompletedCount++;
          if (ProgressCallback) {
            ProgressCallback(CompletedCount, TotalCount);
          }
        }
      }
    }
  }

  // 收集所有结果
  for (auto& Future : Futures) {
    auto Result = Future.get();
    Results.push_back(Result);
    CompletedCount++;

    if (ProgressCallback) {
      ProgressCallback(CompletedCount, TotalCount);
    }
  }

  return Results;
}

void ConcurrentRequestManager::waitForAll() {
  while (ActiveRequests.load() > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

// RequestBatch 实现

ConcurrentResult RequestBatch::executeWithRetry(
  AIInterface& Provider,
  const AIRequest& Request,
  unsigned RequestId
) {
  ConcurrentResult Result;
  Result.RequestId = RequestId;
  Result.Success = false;

  unsigned RetryCount = 0;

  do {
    // 检查是否已取消
    if (Cancelled.load()) {
      Result.ErrorMessage = "Batch cancelled";
      return Result;
    }

    auto Response = Provider.sendRequest(Request);
    if (Response) {
      Result.Response = *Response;
      Result.Success = Response->Success;
      if (!Result.Success) {
        Result.ErrorMessage = Response->ErrorMessage;
      }
    } else {
      auto Err = Response.takeError();
      Result.ErrorMessage = toString(std::move(Err));
    }

    if (Result.Success || RetryCount >= Config.MaxRetries) {
      break;
    }

    // 等待重试延迟
    std::this_thread::sleep_for(Config.RetryDelay);
    RetryCount++;
  } while (RetryCount <= Config.MaxRetries && !Cancelled.load());

  return Result;
}

bool RequestBatch::execute(AIInterface& Provider) {
  return executeWithProgress(Provider, nullptr);
}

bool RequestBatch::executeWithProgress(
  AIInterface& Provider,
  std::function<void(unsigned, unsigned)> ProgressCallback
) {
  return executeFull(Provider, ProgressCallback, nullptr);
}

bool RequestBatch::executeWithCallback(
  AIInterface& Provider,
  std::function<void(const ConcurrentResult&)> ResultCallback
) {
  return executeFull(Provider, nullptr, ResultCallback);
}

bool RequestBatch::executeFull(
  AIInterface& Provider,
  std::function<void(unsigned, unsigned)> ProgressCallback,
  std::function<void(const ConcurrentResult&)> ResultCallback
) {
  Results.clear();
  Cancelled = false;

  if (Requests.empty()) {
    return true;
  }

  ConcurrentRequestManager Manager(Config.Concurrent);
  std::vector<std::future<ConcurrentResult>> Futures;
  Futures.reserve(Requests.size());

  unsigned CompletedCount = 0;
  unsigned TotalCount = Requests.size();

  // 启动所有请求
  for (unsigned i = 0; i < Requests.size(); ++i) {
    // 检查是否已取消
    if (Cancelled.load()) {
      break;
    }

    // 等待有空闲槽位
    while (Manager.getActiveRequestCount() >= Config.Concurrent.MaxConcurrentRequests) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 启动请求
    Futures.push_back(std::async(std::launch::async,
      [this, &Provider, &Request = Requests[i], i]() {
        return executeWithRetry(Provider, Request, i);
      }
    ));

    // 根据错误策略处理
    if (Config.ErrorStrategy == BatchErrorStrategy::StopOnFailure) {
      // 检查已完成的请求
      for (auto& F : Futures) {
        if (F.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
          auto Result = F.get();
          if (!Result.Success) {
            Cancelled = true;
            Results.push_back(Result);
            return false;
          }
          Results.push_back(Result);
          CompletedCount++;
          if (ResultCallback) {
            ResultCallback(Result);
          }
          if (ProgressCallback) {
            ProgressCallback(CompletedCount, TotalCount);
          }
        }
      }
    }
  }

  // 收集所有结果
  for (auto& Future : Futures) {
    auto Result = Future.get();
    Results.push_back(Result);
    CompletedCount++;

    if (ResultCallback) {
      ResultCallback(Result);
    }
    if (ProgressCallback) {
      ProgressCallback(CompletedCount, TotalCount);
    }
  }

  // 检查是否全部成功
  for (const auto& Result : Results) {
    if (!Result.Success) {
      return false;
    }
  }

  return true;
}

bool RequestBatch::retryFailed(AIInterface& Provider) {
  auto FailedResults = getFailedResults();
  if (FailedResults.empty()) {
    return true;
  }

  // 重新构建失败请求列表
  std::vector<AIRequest> FailedRequests;
  for (const auto& Result : FailedResults) {
    if (Result.RequestId < Requests.size()) {
      FailedRequests.push_back(Requests[Result.RequestId]);
    }
  }

  // 创建新的批处理并执行
  RequestBatch RetryBatch(Config);
  RetryBatch.addRequests(FailedRequests);

  bool Success = RetryBatch.execute(Provider);

  // 合并结果
  auto RetryResults = RetryBatch.getResults();
  for (const auto& Result : RetryResults) {
    // 更新原结果
    if (Result.RequestId < Results.size()) {
      Results[Result.RequestId] = Result;
    }
  }

  return Success;
}

std::vector<ConcurrentResult> RequestBatch::getSuccessfulResults() const {
  std::vector<ConcurrentResult> Successful;
  for (const auto& Result : Results) {
    if (Result.Success) {
      Successful.push_back(Result);
    }
  }
  return Successful;
}

std::vector<ConcurrentResult> RequestBatch::getFailedResults() const {
  std::vector<ConcurrentResult> Failed;
  for (const auto& Result : Results) {
    if (!Result.Success) {
      Failed.push_back(Result);
    }
  }
  return Failed;
}

double RequestBatch::getSuccessRate() const {
  if (Results.empty()) {
    return 0.0;
  }

  unsigned SuccessCount = 0;
  for (const auto& Result : Results) {
    if (Result.Success) {
      SuccessCount++;
    }
  }

  return static_cast<double>(SuccessCount) / Results.size();
}

} // namespace blocktype
