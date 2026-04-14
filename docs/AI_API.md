# BlockType AI API 文档

## 概述

BlockType AI 模块提供了一套完整的 API，用于集成 AI 功能到编译器中。

## 核心接口

### AIInterface

AI 提供者的抽象接口。

```cpp
namespace blocktype {

class AIInterface {
public:
  virtual ~AIInterface() = default;
  
  /// 发送同步请求
  virtual llvm::Expected<AIResponse> sendRequest(const AIRequest& Request) = 0;
  
  /// 发送异步请求
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
  
  /// 获取支持的任务类型
  virtual std::vector<AITaskType> getSupportedTasks() const = 0;
};

} // namespace blocktype
```

### AIRequest

AI 请求结构体。

```cpp
struct AIRequest {
  AITaskType TaskType;           // 任务类型
  Language Lang;                 // 语言
  std::string Context;           // 上下文代码
  std::string Query;             // 查询内容
  std::string SourceFile;        // 源文件路径
  unsigned Line = 0;             // 行号
  unsigned Column = 0;           // 列号
  std::vector<std::string> AdditionalContext;  // 额外上下文
};
```

### AIResponse

AI 响应结构体。

```cpp
struct AIResponse {
  bool Success;                  // 是否成功
  std::string Content;           // 响应内容
  std::string Provider;          // 提供者名称
  unsigned TokensUsed = 0;       // 使用的 token 数
  double LatencyMs = 0;          // 延迟（毫秒）
  std::string ErrorMessage;      // 错误信息
  std::vector<std::string> Suggestions;  // 建议列表
};
```

### AIConfig

AI 配置结构体。

```cpp
struct AIConfig {
  AIProvider DefaultProvider = AIProvider::OpenAI;
  Language DefaultLanguage = Language::Auto;
  bool EnableCache = true;
  unsigned MaxCacheSize = 1000;
  unsigned TimeoutMs = 30000;
  unsigned MaxRetries = 3;
  bool EnableCostTracking = true;
  double MaxCostPerDay = 10.0;
  
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
```

## 枚举类型

### AIProvider

AI 提供者类型。

```cpp
enum class AIProvider {
  OpenAI,      // GPT-4 用于复杂推理
  Claude,      // Claude 3.5 用于代码生成
  Local,       // Ollama 用于离线场景
  Qwen,        // 通义千问用于中文优化
};
```

### AITaskType

AI 任务类型。

```cpp
enum class AITaskType {
  ErrorFix,           // 错误修复建议
  CodeCompletion,     // 代码补全
  PerformanceAdvice,  // 性能优化建议
  SecurityCheck,      // 安全检查
  Explanation,        // 错误解释
  Translation,        // 中英文翻译
};
```

### Language

语言类型。

```cpp
enum class Language {
  Chinese,
  English,
  Auto,  // 自动检测
};
```

## AI 编排器

### AIOrchestrator

管理多个 AI 提供者的编排器。

```cpp
class AIOrchestrator {
public:
  /// 构造函数
  AIOrchestrator(const AIConfig& Cfg);
  
  /// 注册 AI 提供者
  void registerProvider(std::unique_ptr<AIInterface> Provider);
  
  /// 根据任务类型选择最佳提供者
  AIInterface* selectProvider(AITaskType Type, Language Lang);
  
  /// 发送请求（自动选择提供者）
  llvm::Expected<AIResponse> sendRequest(const AIRequest& Request);
  
  /// 异步发送请求
  void sendRequestAsync(
    const AIRequest& Request,
    std::function<void(llvm::Expected<AIResponse>)> Callback
  );
  
  /// 获取所有提供者状态
  std::vector<std::pair<AIProvider, bool>> getProvidersStatus() const;
  
  /// 设置配置
  void setConfig(const AIConfig& Cfg);
  
  /// 获取配置
  const AIConfig& getConfig() const;
  
  /// 获取提供者数量
  size_t getProviderCount() const;
};
```

#### 使用示例

```cpp
#include "blocktype/AI/AIOrchestrator.h"
#include "blocktype/AI/Providers/OpenAIProvider.h"

using namespace blocktype;

int main() {
  // 创建配置
  AIConfig Config;
  Config.MaxCostPerDay = 5.0;
  
  // 创建编排器
  AIOrchestrator Orchestrator(Config);
  
  // 注册提供者
  Orchestrator.registerProvider(
    std::make_unique<OpenAIProvider>("your-api-key", "gpt-4")
  );
  
  // 创建请求
  AIRequest Request;
  Request.TaskType = AITaskType::ErrorFix;
  Request.Lang = Language::English;
  Request.Query = "Fix this error";
  Request.Context = "int main() { return 0 }";
  
  // 发送请求
  auto Result = Orchestrator.sendRequest(Request);
  if (Result) {
    std::cout << Result->Content << std::endl;
  }
  
  return 0;
}
```

## AI 提供者

### OpenAIProvider

OpenAI GPT 模型提供者。

```cpp
class OpenAIProvider : public AIInterface {
public:
  /// 构造函数
  OpenAIProvider(llvm::StringRef Key, 
                 llvm::StringRef Model = "gpt-4");
  
  /// 设置超时
  void setTimeout(unsigned Ms);
};
```

#### 使用示例

```cpp
auto Provider = std::make_unique<OpenAIProvider>(
  std::getenv("OPENAI_API_KEY"),
  "gpt-4-turbo"
);
Provider->setTimeout(60000);  // 60 秒超时
```

### ClaudeProvider

Anthropic Claude 模型提供者。

```cpp
class ClaudeProvider : public AIInterface {
public:
  /// 构造函数
  ClaudeProvider(llvm::StringRef Key,
                 llvm::StringRef Model = "claude-3-5-sonnet-20241022");
  
  /// 设置超时
  void setTimeout(unsigned Ms);
};
```

### LocalProvider

Ollama 本地模型提供者。

```cpp
class LocalProvider : public AIInterface {
public:
  /// 构造函数
  LocalProvider(llvm::StringRef Endpoint = "http://localhost:11434",
                llvm::StringRef Model = "codellama");
  
  /// 设置模型
  void setModel(llvm::StringRef Model);
  
  /// 设置超时
  void setTimeout(unsigned Ms);
};
```

#### 使用示例

```cpp
auto Provider = std::make_unique<LocalProvider>(
  "http://localhost:11434",
  "llama3"
);
Provider->setTimeout(120000);  // 120 秒超时（本地模型可能更慢）
```

### QwenProvider

阿里云通义千问提供者。

```cpp
class QwenProvider : public AIInterface {
public:
  /// 构造函数
  QwenProvider(llvm::StringRef Key,
               llvm::StringRef Model = "qwen-plus");
  
  /// 设置超时
  void setTimeout(unsigned Ms);
};
```

## 缓存系统

### ResponseCache

响应缓存管理器。

```cpp
class ResponseCache {
public:
  /// 构造函数
  ResponseCache(unsigned MaxSize = 1000,
                std::chrono::seconds TTL = std::chrono::hours(24));
  
  /// 查找缓存
  std::optional<AIResponse> find(const AIRequest& Request);
  
  /// 插入缓存
  void insert(const AIRequest& Request, const AIResponse& Response);
  
  /// 清理过期缓存
  void cleanup();
  
  /// 清空缓存
  void clear();
  
  /// 获取缓存统计
  struct Stats {
    unsigned Size;
    unsigned Hits;
    unsigned Misses;
    double HitRate;
  };
  Stats getStats() const;
};
```

#### 使用示例

```cpp
ResponseCache Cache{100, std::chrono::hours(1)};

// 查找缓存
auto Cached = Cache.find(Request);
if (Cached) {
  std::cout << "Cache hit: " << Cached->Content << std::endl;
} else {
  // 发送请求
  auto Response = Provider->sendRequest(Request);
  if (Response) {
    Cache.insert(Request, *Response);
  }
}

// 查看缓存统计
auto Stats = Cache.getStats();
std::cout << "Hit rate: " << (Stats.HitRate * 100) << "%" << std::endl;
```

## 成本追踪

### CostTracker

成本追踪管理器。

```cpp
class CostTracker {
public:
  /// 构造函数
  CostTracker(double DailyLimit = 10.0);
  
  /// 记录使用
  void record(const CostRecord& Record);
  
  /// 检查是否超过限制
  bool isOverLimit() const;
  
  /// 获取今日成本
  double getTodayCost() const;
  
  /// 获取成本报告
  struct Report {
    double TotalCost;
    double TodayCost;
    unsigned TotalRequests;
    llvm::DenseMap<AIProvider, double> CostByProvider;
    llvm::DenseMap<AITaskType, double> CostByTask;
  };
  Report getReport() const;
  
  /// 设置每日限制
  void setDailyLimit(double Limit);
  
  /// 清空记录
  void clear();
};
```

#### 使用示例

```cpp
CostTracker Tracker{5.0};

// 记录成本
CostRecord Record;
Record.Provider = AIProvider::OpenAI;
Record.TaskType = AITaskType::ErrorFix;
Record.TokensUsed = 1000;
Record.Cost = 0.03;
Tracker.record(Record);

// 检查限制
if (Tracker.isOverLimit()) {
  std::cerr << "Daily cost limit exceeded!" << std::endl;
}

// 查看报告
auto Report = Tracker.getReport();
std::cout << "Today's cost: $" << Report.TodayCost << std::endl;
std::cout << "Total requests: " << Report.TotalRequests << std::endl;
```

## HTTP 客户端

### HTTPClient

HTTP 请求工具类。

```cpp
class HTTPClient {
public:
  /// 发送 HTTP POST 请求
  static llvm::Expected<HTTPResponse> post(
    llvm::StringRef URL,
    llvm::StringRef Body,
    const std::map<std::string, std::string>& Headers,
    unsigned TimeoutMs = 30000
  );
  
  /// 发送 HTTP GET 请求
  static llvm::Expected<HTTPResponse> get(
    llvm::StringRef URL,
    const std::map<std::string, std::string>& Headers,
    unsigned TimeoutMs = 30000
  );
  
  /// URL 编码
  static std::string urlEncode(llvm::StringRef Str);
};
```

#### 使用示例

```cpp
std::map<std::string, std::string> Headers = {
  {"Content-Type", "application/json"},
  {"Authorization", "Bearer " + APIKey}
};

std::string Body = R"({"model": "gpt-4", "messages": [...]})";

auto Response = HTTPClient::post(
  "https://api.openai.com/v1/chat/completions",
  Body,
  Headers,
  30000
);

if (Response && Response->Success) {
  std::cout << Response->Body << std::endl;
}
```

## 错误处理

所有 API 使用 `llvm::Expected<T>` 进行错误处理。

```cpp
auto Result = Provider->sendRequest(Request);
if (!Result) {
  // 处理错误
  llvm::handleAllErrors(
    Result.takeError(),
    [](const llvm::StringError& E) {
      std::cerr << "Error: " << E.getMessage() << std::endl;
    }
  );
  return;
}

// 使用结果
std::cout << Result->Content << std::endl;
```

## 线程安全

- `AIOrchestrator` - 线程安全，支持并发访问
- `ResponseCache` - 线程安全，内部使用互斥锁
- `CostTracker` - 线程安全，内部使用互斥锁
- `AIInterface` 实现类 - 非线程安全，外部需要同步

## 性能优化建议

1. **启用缓存**：减少重复请求，提高响应速度
2. **使用异步请求**：避免阻塞主线程
3. **合理设置超时**：根据网络状况调整
4. **选择合适的模型**：平衡成本和质量

---

**文档版本**: 1.0  
**最后更新**: 2026-04-14
