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

bool RequestBatch::execute(AIInterface& Provider) {
  return executeWithProgress(Provider, nullptr);
}

bool RequestBatch::executeWithProgress(
  AIInterface& Provider,
  std::function<void(unsigned, unsigned)> ProgressCallback
) {
  ConcurrentRequestManager Manager(Config);
  Results = Manager.sendConcurrentWithProgress(Provider, Requests, ProgressCallback);

  // 检查是否全部成功
  for (const auto& Result : Results) {
    if (!Result.Success) {
      return false;
    }
  }

  return true;
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
