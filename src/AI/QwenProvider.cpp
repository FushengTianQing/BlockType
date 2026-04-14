#include "blocktype/AI/Providers/QwenProvider.h"
#include "blocktype/AI/HTTPClient.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

namespace blocktype {

using json = nlohmann::json;

QwenProvider::QwenProvider(llvm::StringRef Key, llvm::StringRef Model)
  : APIKey(Key.str()), Model(Model.str()) {
}

std::string QwenProvider::buildPrompt(const AIRequest& Request) {
  json Prompt;
  
  // 构建系统提示（针对中文优化）
  std::string SystemPrompt;
  switch (Request.TaskType) {
    case AITaskType::ErrorFix:
      SystemPrompt = "你是一个专业的C++编译器助手。分析错误并提供修复建议。";
      break;
    case AITaskType::CodeCompletion:
      SystemPrompt = "你是一个专业的C++程序员。按照最佳实践和现代C++标准完成代码。";
      break;
    case AITaskType::PerformanceAdvice:
      SystemPrompt = "你是一个C++性能优化专家。分析代码并提出改进建议。";
      break;
    case AITaskType::SecurityCheck:
      SystemPrompt = "你是一个C++安全专家。识别潜在的安全漏洞。";
      break;
    case AITaskType::Explanation:
      SystemPrompt = "你是一个C++教育者。清晰简洁地解释概念。";
      break;
    case AITaskType::Translation:
      SystemPrompt = "你是一个双语C++专家。在中英文之间进行翻译。";
      break;
  }
  
  // 构建用户消息
  std::string UserMessage;
  if (!Request.SourceFile.empty()) {
    UserMessage += "文件: " + Request.SourceFile;
    if (Request.Line > 0) {
      UserMessage += ":" + std::to_string(Request.Line);
      if (Request.Column > 0) {
        UserMessage += ":" + std::to_string(Request.Column);
      }
    }
    UserMessage += "\n\n";
  }
  
  if (!Request.Context.empty()) {
    UserMessage += "上下文:\n```\n" + Request.Context + "\n```\n\n";
  }
  
  UserMessage += Request.Query;
  
  // 通义千问 API 格式
  Prompt["model"] = Model;
  Prompt["input"] = {
    {"messages", json::array({
      {{"role", "system"}, {"content", SystemPrompt}},
      {{"role", "user"}, {"content", UserMessage}}
    })}
  };
  Prompt["parameters"] = {
    {"temperature", 0.7},
    {"max_tokens", 2000}
  };
  
  return Prompt.dump();
}

llvm::Expected<AIResponse> QwenProvider::parseResponse(const std::string& JSON) {
  AIResponse Response;
  Response.Success = false;
  Response.Provider = "Qwen";
  
  try {
    auto Data = json::parse(JSON);
    
    if (Data.contains("code") && Data["code"] != "Success") {
      Response.ErrorMessage = Data.value("message", "Unknown error");
      return Response;
    }
    
    if (Data.contains("output")) {
      auto& Output = Data["output"];
      if (Output.contains("text")) {
        Response.Success = true;
        Response.Content = Output["text"].get<std::string>();
        
        if (Output.contains("usage")) {
          Response.TokensUsed = 
            Output["usage"].value("total_tokens", 0u);
        }
      }
    }
    
    if (!Response.Success) {
      Response.ErrorMessage = "No response from Qwen";
    }
  } catch (const std::exception& E) {
    Response.ErrorMessage = std::string("JSON parse error: ") + E.what();
  }
  
  return Response;
}

llvm::Expected<std::string> QwenProvider::sendHTTPRequest(const std::string& Body) {
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

llvm::Expected<AIResponse> QwenProvider::sendRequest(const AIRequest& Request) {
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

void QwenProvider::sendRequestAsync(
  const AIRequest& Request,
  std::function<void(llvm::Expected<AIResponse>)> Callback
) {
  std::thread([this, Request, Callback]() {
    auto Result = sendRequest(Request);
    Callback(std::move(Result));
  }).detach();
}

bool QwenProvider::isAvailable() const {
  return !APIKey.empty();
}

std::vector<AITaskType> QwenProvider::getSupportedTasks() const {
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