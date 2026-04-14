#pragma once

#include "../AIInterface.h"

namespace blocktype {

class ClaudeProvider : public AIInterface {
  std::string APIKey;
  std::string Model;  // claude-3-5-sonnet-20241022, claude-3-opus-20240229
  std::string APIEndpoint = "https://api.anthropic.com/v1/messages";
  unsigned TimeoutMs = 30000;

public:
  ClaudeProvider(llvm::StringRef Key, llvm::StringRef Model = "claude-3-5-sonnet-20241022");

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
    return AIProvider::Claude;
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
