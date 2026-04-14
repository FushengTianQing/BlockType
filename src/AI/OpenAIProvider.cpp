#include "blocktype/AI/Providers/OpenAIProvider.h"
#include "blocktype/AI/HTTPClient.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

namespace blocktype {

using json = nlohmann::json;

OpenAIProvider::OpenAIProvider(llvm::StringRef Key, llvm::StringRef Model)
  : APIKey(Key.str()), Model(Model.str()) {
}

std::string OpenAIProvider::buildPrompt(const AIRequest& Request) {
  json Prompt;

  // 构建系统提示
  std::string SystemPrompt;
  switch (Request.TaskType) {
    case AITaskType::ErrorFix:
      SystemPrompt = "You are an expert C++ compiler assistant. Analyze the error and provide a fix.";
      break;
    case AITaskType::CodeCompletion:
      SystemPrompt = "You are an expert C++ programmer. Complete the code following best practices.";
      break;
    case AITaskType::PerformanceAdvice:
      SystemPrompt = "You are a C++ performance optimization expert. Analyze code and suggest improvements.";
      break;
    case AITaskType::SecurityCheck:
      SystemPrompt = "You are a C++ security expert. Identify potential security vulnerabilities.";
      break;
    case AITaskType::Explanation:
      SystemPrompt = "You are a C++ educator. Explain the concept clearly and concisely.";
      break;
    case AITaskType::Translation:
      SystemPrompt = "You are a bilingual C++ expert. Translate between Chinese and English.";
      break;
  }

  // 构建用户消息
  std::string UserMessage;
  if (!Request.SourceFile.empty()) {
    UserMessage += "File: " + Request.SourceFile;
    if (Request.Line > 0) {
      UserMessage += ":" + std::to_string(Request.Line);
      if (Request.Column > 0) {
        UserMessage += ":" + std::to_string(Request.Column);
      }
    }
    UserMessage += "\n\n";
  }

  if (!Request.Context.empty()) {
    UserMessage += "Context:\n```\n" + Request.Context + "\n```\n\n";
  }

  UserMessage += Request.Query;

  // 构建 OpenAI 消息格式
  Prompt["messages"] = json::array({
    {{"role", "system"}, {"content", SystemPrompt}},
    {{"role", "user"}, {"content", UserMessage}}
  });

  Prompt["model"] = Model;
  Prompt["temperature"] = 0.7;
  Prompt["max_tokens"] = 2000;

  return Prompt.dump();
}

std::string OpenAIProvider::buildStreamingPrompt(const AIRequest& Request) {
  json Prompt = json::parse(buildPrompt(Request));
  Prompt["stream"] = true;  // 启用流式输出
  return Prompt.dump();
}

llvm::Expected<AIResponse> OpenAIProvider::parseResponse(const std::string& JSON) {
  AIResponse Response;
  Response.Success = false;
  Response.Provider = "OpenAI";

  try {
    auto Data = json::parse(JSON);

    if (Data.contains("error")) {
      Response.ErrorMessage = Data["error"]["message"].get<std::string>();
      return Response;
    }

    if (Data.contains("choices") && !Data["choices"].empty()) {
      Response.Success = true;
      Response.Content = Data["choices"][0]["message"]["content"].get<std::string>();

      if (Data.contains("usage")) {
        Response.TokensUsed =
          Data["usage"]["total_tokens"].get<unsigned>();
      }
    } else {
      Response.ErrorMessage = "No response from OpenAI";
    }
  } catch (const std::exception& E) {
    Response.ErrorMessage = std::string("JSON parse error: ") + E.what();
  }

  return Response;
}

llvm::Expected<AIResponse> OpenAIProvider::parseStreamingChunk(const std::string& JSON) {
  AIResponse Response;
  Response.Success = false;
  Response.Provider = "OpenAI";

  try {
    auto Data = json::parse(JSON);

    if (Data.contains("error")) {
      Response.ErrorMessage = Data["error"]["message"].get<std::string>();
      return Response;
    }

    if (Data.contains("choices") && !Data["choices"].empty()) {
      auto& Choice = Data["choices"][0];

      // 检查是否有内容
      if (Choice.contains("delta") && Choice["delta"].contains("content")) {
        Response.Success = true;
        Response.Content = Choice["delta"]["content"].get<std::string>();
      }

      // 检查是否完成
      if (Choice.contains("finish_reason") && !Choice["finish_reason"].is_null()) {
        Response.Success = true;
      }
    }
  } catch (const std::exception& E) {
    Response.ErrorMessage = std::string("JSON parse error: ") + E.what();
  }

  return Response;
}

llvm::Expected<std::string> OpenAIProvider::sendHTTPRequest(const std::string& Body) {
  std::map<std::string, std::string> Headers = {
    {"Content-Type", "application/json"},
    {"Authorization", "Bearer " + APIKey}
  };
  
  auto HTTPResponse = HTTPClient::post(APIEndpoint, Body, Headers, TimeoutMs);
  if (!HTTPResponse) {
    return HTTPResponse.takeError();
  }
  
  if (!HTTPResponse->Success) {
    return llvm::make_error<llvm::StringError>(
      HTTPResponse->ErrorMessage,
      std::make_error_code(std::errc::protocol_error)
    );
  }
  
  return HTTPResponse->Body;
}

llvm::Expected<AIResponse> OpenAIProvider::sendRequest(const AIRequest& Request) {
  auto StartTime = std::chrono::high_resolution_clock::now();

  // 构建请求体
  std::string Body = buildPrompt(Request);

  // 发送 HTTP 请求
  auto HTTPResult = sendHTTPRequest(Body);
  if (!HTTPResult) {
    return HTTPResult.takeError();
  }

  // 解析响应
  auto Response = parseResponse(*HTTPResult);
  if (!Response) {
    return Response.takeError();
  }

  // 计算延迟
  auto EndTime = std::chrono::high_resolution_clock::now();
  auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(EndTime - StartTime);
  Response->LatencyMs = Duration.count();

  return Response;
}

llvm::Expected<AIResponse> OpenAIProvider::sendStreamingRequest(
  const AIRequest& Request,
  StreamCallback Callback
) {
  auto StartTime = std::chrono::high_resolution_clock::now();

  AIResponse FinalResponse;
  FinalResponse.Success = false;
  FinalResponse.Provider = "OpenAI";

  std::string FullContent;

  // 构建流式请求体
  std::string Body = buildStreamingPrompt(Request);

  // 设置请求头
  std::map<std::string, std::string> Headers = {
    {"Content-Type", "application/json"},
    {"Authorization", "Bearer " + APIKey}
  };

  // SSE 回调函数
  auto SSECallback = [&](llvm::StringRef Data, bool Done) {
    if (Done) {
      return;
    }

    // 解析流式响应块
    auto ChunkResponse = parseStreamingChunk(Data.str());
    if (ChunkResponse && ChunkResponse->Success) {
      FullContent += ChunkResponse->Content;

      // 调用用户回调
      if (Callback) {
        Callback(ChunkResponse->Content, false);
      }
    }
  };

  // 发送 SSE 请求
  auto HTTPResponse = HTTPClient::postSSE(APIEndpoint, Body, Headers, SSECallback, TimeoutMs);
  if (!HTTPResponse) {
    return HTTPResponse.takeError();
  }

  if (!HTTPResponse->Success) {
    return llvm::make_error<llvm::StringError>(
      HTTPResponse->ErrorMessage,
      std::make_error_code(std::errc::protocol_error)
    );
  }

  // 构建最终响应
  FinalResponse.Success = true;
  FinalResponse.Content = FullContent;

  // 计算延迟
  auto EndTime = std::chrono::high_resolution_clock::now();
  auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(EndTime - StartTime);
  FinalResponse.LatencyMs = Duration.count();

  // 通知流结束
  if (Callback) {
    Callback("", true);
  }

  return FinalResponse;
}

void OpenAIProvider::sendRequestAsync(
  const AIRequest& Request,
  std::function<void(llvm::Expected<AIResponse>)> Callback
) {
  std::thread([this, Request, Callback]() {
    auto Result = sendRequest(Request);
    Callback(std::move(Result));
  }).detach();
}

bool OpenAIProvider::isAvailable() const {
  return !APIKey.empty();
}

std::vector<AITaskType> OpenAIProvider::getSupportedTasks() const {
  return {
    AITaskType::ErrorFix,
    AITaskType::CodeCompletion,
    AITaskType::PerformanceAdvice,
    AITaskType::SecurityCheck,
    AITaskType::Explanation,
    AITaskType::Translation
  };
}

} // namespace blocktype