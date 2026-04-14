#pragma once

#include "../AIInterface.h"

namespace blocktype {

class OpenAIProvider : public AIInterface {
  std::string APIKey;
  std::string Model;  // gpt-4, gpt-4-turbo, gpt-3.5-turbo
  std::string APIEndpoint = "https://api.openai.com/v1/chat/completions";
  unsigned TimeoutMs = 30000;

public:
  OpenAIProvider(llvm::StringRef Key, llvm::StringRef Model = "gpt-4");

  llvm::Expected<AIResponse> sendRequest(const AIRequest& Request) override;

  llvm::Expected<AIResponse> sendStreamingRequest(
    const AIRequest& Request,
    StreamCallback Callback
  ) override;

  void sendRequestAsync(
    const AIRequest& Request,
    std::function<void(llvm::Expected<AIResponse>)> Callback
  ) override;

  bool isAvailable() const override;

  AIProvider getProvider() const override {
    return AIProvider::OpenAI;
  }

  std::string getModelName() const override {
    return Model;
  }

  std::vector<AITaskType> getSupportedTasks() const override;

  void setTimeout(unsigned Ms) { TimeoutMs = Ms; }

private:
  std::string buildPrompt(const AIRequest& Request);
  std::string buildStreamingPrompt(const AIRequest& Request);
  llvm::Expected<AIResponse> parseResponse(const std::string& JSON);
  llvm::Expected<AIResponse> parseStreamingChunk(const std::string& JSON);
  llvm::Expected<std::string> sendHTTPRequest(const std::string& Body);
};

} // namespace blocktype
