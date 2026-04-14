#pragma once

#include "../AIInterface.h"

namespace blocktype {

class LocalProvider : public AIInterface {
  std::string OllamaEndpoint;  // http://localhost:11434
  std::string Model;           // llama3, codellama, qwen2
  unsigned TimeoutMs = 60000;  // 本地模型可能更慢
  
public:
  LocalProvider(llvm::StringRef Endpoint = "http://localhost:11434",
                llvm::StringRef Model = "codellama");
  
  llvm::Expected<AIResponse> sendRequest(const AIRequest& Request) override;
  
  void sendRequestAsync(
    const AIRequest& Request,
    std::function<void(llvm::Expected<AIResponse>)> Callback
  ) override;
  
  bool isAvailable() const override;
  
  AIProvider getProvider() const override { 
    return AIProvider::Local; 
  }
  
  std::string getModelName() const override { 
    return Model; 
  }
  
  std::vector<AITaskType> getSupportedTasks() const override;
  
  void setModel(llvm::StringRef M) { Model = M.str(); }
  void setTimeout(unsigned Ms) { TimeoutMs = Ms; }
  
private:
  std::string buildPrompt(const AIRequest& Request);
  llvm::Expected<AIResponse> parseResponse(const std::string& JSON);
  llvm::Expected<std::string> sendHTTPRequest(const std::string& Body);
};

} // namespace blocktype
