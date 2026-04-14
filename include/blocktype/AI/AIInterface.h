#pragma once

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <chrono>

namespace blocktype {

/// AI 提供者类型
enum class AIProvider {
  OpenAI,      // GPT-4 用于复杂推理
  Claude,      // Claude 3.5 用于代码生成
  Local,       // Ollama 用于离线场景
  Qwen,        // 通义千问用于中文优化
};

/// AI 任务类型
enum class AITaskType {
  ErrorFix,           // 错误修复建议
  CodeCompletion,     // 代码补全
  PerformanceAdvice,  // 性能优化建议
  SecurityCheck,      // 安全检查
  Explanation,        // 错误解释
  Translation,        // 中英文翻译
};

/// 语言类型
enum class Language {
  Chinese,
  English,
  Auto,  // 自动检测
};

} // namespace blocktype

/// DenseMapInfo 特化 - AIProvider
namespace llvm {
template<>
struct DenseMapInfo<blocktype::AIProvider> {
  static inline blocktype::AIProvider getEmptyKey() {
    return static_cast<blocktype::AIProvider>(-1);
  }
  static inline blocktype::AIProvider getTombstoneKey() {
    return static_cast<blocktype::AIProvider>(-2);
  }
  static unsigned getHashValue(blocktype::AIProvider Val) {
    return static_cast<unsigned>(Val);
  }
  static bool isEqual(blocktype::AIProvider LHS, blocktype::AIProvider RHS) {
    return LHS == RHS;
  }
};

/// DenseMapInfo 特化 - AITaskType
template<>
struct DenseMapInfo<blocktype::AITaskType> {
  static inline blocktype::AITaskType getEmptyKey() {
    return static_cast<blocktype::AITaskType>(-1);
  }
  static inline blocktype::AITaskType getTombstoneKey() {
    return static_cast<blocktype::AITaskType>(-2);
  }
  static unsigned getHashValue(blocktype::AITaskType Val) {
    return static_cast<unsigned>(Val);
  }
  static bool isEqual(blocktype::AITaskType LHS, blocktype::AITaskType RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

namespace blocktype {

/// AI 请求
struct AIRequest {
  AITaskType TaskType;
  Language Lang;
  std::string Context;        // 上下文代码
  std::string Query;          // 查询内容
  std::string SourceFile;     // 源文件路径
  unsigned Line = 0;          // 行号
  unsigned Column = 0;        // 列号
  std::vector<std::string> AdditionalContext;
};

/// AI 响应
struct AIResponse {
  bool Success;
  std::string Content;
  std::string Provider;       // 使用的提供者
  unsigned TokensUsed = 0;    // 使用的 token 数
  double LatencyMs = 0;       // 延迟（毫秒）
  std::string ErrorMessage;
  std::vector<std::string> Suggestions;  // 建议列表
};

/// 流式响应回调类型
/// @param Chunk 收到的文本片段
/// @param Done 是否完成
using StreamCallback = std::function<void(llvm::StringRef Chunk, bool Done)>;

/// AI 配置
struct AIConfig {
  AIProvider DefaultProvider = AIProvider::OpenAI;
  Language DefaultLanguage = Language::Auto;
  bool EnableCache = true;
  unsigned MaxCacheSize = 1000;  // 最大缓存条目数
  unsigned TimeoutMs = 30000;    // 超时时间（毫秒）
  unsigned MaxRetries = 3;       // 最大重试次数
  bool EnableCostTracking = true;
  double MaxCostPerDay = 10.0;   // 每日最大成本（美元）
  
  // API Keys
  std::string OpenAIKey;
  std::string ClaudeKey;
  std::string QwenKey;
  std::string OllamaEndpoint = "http://localhost:11434";
  
  /// 从环境变量加载配置
  static AIConfig fromEnvironment();
  
  /// 从配置文件加载
  static AIConfig fromFile(const std::string& Path);
};

/// AI 接口抽象类
class AIInterface {
public:
  virtual ~AIInterface() = default;
  
  /// 发送请求
  virtual llvm::Expected<AIResponse> sendRequest(const AIRequest& Request) = 0;
  
  /// 发送流式请求（SSE）
  /// @param Request 请求内容
  /// @param Callback 流式回调函数
  /// @return 最终响应（在流结束后）
  virtual llvm::Expected<AIResponse> sendStreamingRequest(
    const AIRequest& Request,
    StreamCallback Callback
  ) = 0;
  
  /// 异步发送请求
  virtual void sendRequestAsync(
    const AIRequest& Request,
    std::function<void(llvm::Expected<AIResponse>)> Callback
  ) = 0;
  
  /// 检查可用性
  virtual bool isAvailable() const = 0;
  
  /// 获取提供者类型
  virtual AIProvider getProvider() const = 0;
  
  /// 获取模型名称
  virtual std::string getModelName() const = 0;
  
  /// 支持的任务类型
  virtual std::vector<AITaskType> getSupportedTasks() const = 0;
};

} // namespace blocktype
