#include "blocktype/AI/Providers/ClaudeProvider.h"
#include "blocktype/AI/HTTPClient.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

namespace blocktype {

using json = nlohmann::json;

ClaudeProvider::ClaudeProvider(llvm::StringRef Key, llvm::StringRef Model)
  : APIKey(Key.str()), Model(Model.str()) {
}

std::string ClaudeProvider::buildPrompt(const AIRequest& Request) {
  json Prompt;
  
  // 构建系统提示
  std::string SystemPrompt;
  switch (Request.TaskType) {
    case AITaskType::ErrorFix:
      SystemPrompt = "You are an expert C++ compiler assistant. Analyze the error and provide a fix.";
      break;
    case AITaskType::CodeCompletion:
      SystemPrompt = "You are an expert C++ programmer. Complete the code following best practices and modern C++ standards.";
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
  
  // Claude API 格式
  Prompt["model"] = Model;
  Prompt["max_tokens"] = 4096;
  Prompt["system"] = SystemPrompt;
  Prompt["messages"] = json::array({
    {{"role", "user"}, {"content", UserMessage}}
  });
  
  return Prompt.dump();
}

llvm::Expected<AIResponse> ClaudeProvider::parseResponse(const std::string& JSON) {
  AIResponse Response;
  Response.Success = false;
  Response.Provider = "Claude";
  
  try {
    auto Data = json::parse(JSON);
    
    if (Data.contains("error")) {
      Response.ErrorMessage = Data["error"]["message"].get<std::string>();
      return Response;
    }
    
    if (Data.contains("content") && !Data["content"].empty()) {
      Response.Success = true;
      Response.Content = Data["content"][0]["text"].get<std::string>();
      
      if (Data.contains("usage")) {
        Response.TokensUsed = 
          Data["usage"]["input_tokens"].get<unsigned>() +
          Data["usage"]["output_tokens"].get<unsigned>();
      }
    } else {
      Response.ErrorMessage = "No response from Claude";
    }
  } catch (const std::exception& E) {
    Response.ErrorMessage = std::string("JSON parse error: ") + E.what();
  }
  
  return Response;
}

llvm::Expected<std::string> ClaudeProvider::sendHTTPRequest(const std::string& Body) {
  std::map<std::string, std::string> Headers = {
    {"Content-Type", "application/json"},
    {"x-api-key", APIKey},
    {"anthropic-version", "2023-06-01"}
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

llvm::Expected<AIResponse> ClaudeProvider::sendRequest(const AIRequest& Request) {
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

void ClaudeProvider::sendRequestAsync(
  const AIRequest& Request,
  std::function<void(llvm::Expected<AIResponse>)> Callback
) {
  std::thread([this, Request, Callback]() {
    auto Result = sendRequest(Request);
    Callback(std::move(Result));
  }).detach();
}

bool ClaudeProvider::isAvailable() const {
  return !APIKey.empty();
}

std::vector<AITaskType> ClaudeProvider::getSupportedTasks() const {
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