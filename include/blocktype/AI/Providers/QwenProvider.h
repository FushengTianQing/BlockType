#pragma once

#include "../AIInterface.h"

namespace blocktype {

class QwenProvider : public AIInterface {
  std::string APIKey;
  std::string Model;  // qwen-turbo, qwen-plus, qwen-max
  std::string APIEndpoint = "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";
  unsigned TimeoutMs = 30000;
  
public:
  QwenProvider(llvm::StringRef Key, llvm::StringRef Model = "qwen-plus");
  
  llvm::Expected<AIResponse> sendRequest(const AIRequest& Request) override;
  
  void sendRequestAsync(
    const AIRequest& Request,
    std::function<void(llvm::Expected<AIResponse>)> Callback
  ) override;
  
  bool isAvailable() const override;
  
  AIProvider getProvider() const override { 
    return AIProvider::Qwen; 
  }
  
  std::string getModelName() const override { 
    return Model; 
  }
  
  std::vector<AITaskType> getSupportedTasks() const override;
  
  void setTimeout(unsigned Ms) { TimeoutMs = Ms; }
  
private:
  std::string buildPrompt(const AIRequest& Request);
  llvm::Expected<AIResponse> parseResponse(const std::string& JSON);
  llvm::Expected<std::string> sendHTTPRequest(const std::string& Body);
};

} // namespace blocktype
