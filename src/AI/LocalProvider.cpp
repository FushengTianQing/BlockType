#include "blocktype/AI/Providers/LocalProvider.h"
#include "blocktype/AI/HTTPClient.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

namespace blocktype {

using json = nlohmann::json;

LocalProvider::LocalProvider(llvm::StringRef Endpoint, llvm::StringRef Model)
  : OllamaEndpoint(Endpoint.str()), Model(Model.str()) {
}

std::string LocalProvider::buildPrompt(const AIRequest& Request) {
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
  
  // Ollama API 格式
  Prompt["model"] = Model;
  Prompt["system"] = SystemPrompt;
  Prompt["prompt"] = UserMessage;
  Prompt["stream"] = false;
  Prompt["options"] = {
    {"temperature", 0.7},
    {"num_predict", 2000}
  };
  
  return Prompt.dump();
}

llvm::Expected<AIResponse> LocalProvider::parseResponse(const std::string& JSON) {
  AIResponse Response;
  Response.Success = false;
  Response.Provider = "Local";
  
  try {
    auto Data = json::parse(JSON);
    
    if (Data.contains("error")) {
      Response.ErrorMessage = Data["error"].get<std::string>();
      return Response;
    }
    
    if (Data.contains("response")) {
      Response.Success = true;
      Response.Content = Data["response"].get<std::string>();
      
      if (Data.contains("eval_count")) {
        Response.TokensUsed = 
          Data.value("prompt_eval_count", 0u) + 
          Data["eval_count"].get<unsigned>();
      }
    } else {
      Response.ErrorMessage = "No response from Ollama";
    }
  } catch (const std::exception& E) {
    Response.ErrorMessage = std::string("JSON parse error: ") + E.what();
  }
  
  return Response;
}

llvm::Expected<std::string> LocalProvider::sendHTTPRequest(const std::string& Body) {
  std::string Endpoint = OllamaEndpoint + "/api/generate";
  
  std::map<std::string, std::string> Headers = {
    {"Content-Type", "application/json"}
  };
  
  auto HTTPResponse = HTTPClient::post(Endpoint, Body, Headers, TimeoutMs);
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

llvm::Expected<AIResponse> LocalProvider::sendRequest(const AIRequest& Request) {
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

void LocalProvider::sendRequestAsync(
  const AIRequest& Request,
  std::function<void(llvm::Expected<AIResponse>)> Callback
) {
  std::thread([this, Request, Callback]() {
    auto Result = sendRequest(Request);
    Callback(std::move(Result));
  }).detach();
}

bool LocalProvider::isAvailable() const {
  // 检查 Ollama 服务是否运行
  // 这里简单返回 true，实际应该检查连接
  return !OllamaEndpoint.empty();
}

std::vector<AITaskType> LocalProvider::getSupportedTasks() const {
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