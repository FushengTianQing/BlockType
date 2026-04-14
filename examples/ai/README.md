# BlockType AI 示例代码

本目录包含 BlockType AI 功能的使用示例。

## 示例列表

### 1. basic_usage.cpp

基础使用示例，演示：
- 创建 AI 配置
- 注册 AI 提供者
- 发送同步请求
- 处理响应

**编译**：
```bash
cd build
clang++ -std=c++23 ../examples/ai/basic_usage.cpp \
  -I../include \
  -I/opt/homebrew/opt/llvm@18/include \
  -L./src/AI \
  -L./src/Basic \
  -lblocktype-ai \
  -lblocktype-basic \
  $(llvm-config --libs core support) \
  -lcurl \
  -lnlohmann_json \
  -o basic_usage
```

**运行**：
```bash
# 设置 API 密钥
export OPENAI_API_KEY="your-key"
export ANTHROPIC_API_KEY="your-key"
export QWEN_API_KEY="your-key"

# 运行示例
./basic_usage
```

### 2. advanced_features.cpp

高级功能示例，演示：
- 缓存机制
- 成本追踪
- 异步请求
- 提供者选择策略

**编译**：
```bash
cd build
clang++ -std=c++23 ../examples/ai/advanced_features.cpp \
  -I../include \
  -I/opt/homebrew/opt/llvm@18/include \
  -L./src/AI \
  -L./src/Basic \
  -lblocktype-ai \
  -lblocktype-basic \
  $(llvm-config --libs core support) \
  -lcurl \
  -lnlohmann_json \
  -lpthread \
  -o advanced_features
```

**运行**：
```bash
./advanced_features
```

## 使用场景

### 场景 1：错误修复

```cpp
AIRequest Request;
Request.TaskType = AITaskType::ErrorFix;
Request.Query = "Fix the compilation error";
Request.Context = "int main() { return 0 }";  // 缺少分号
Request.Line = 1;
```

### 场景 2：代码补全

```cpp
AIRequest Request;
Request.TaskType = AITaskType::CodeCompletion;
Request.Query = "Complete this function";
Request.Context = "void sort(std::vector<int>& nums) {";
```

### 场景 3：性能优化

```cpp
AIRequest Request;
Request.TaskType = AITaskType::PerformanceAdvice;
Request.Query = "Optimize this loop";
Request.Context = R"(
for (int i = 0; i < 1000; ++i) {
  result += std::to_string(i);
}
)";
```

### 场景 4：安全检查

```cpp
AIRequest Request;
Request.TaskType = AITaskType::SecurityCheck;
Request.Query = "Check for security vulnerabilities";
Request.Context = R"(
char buffer[10];
strcpy(buffer, user_input);
)";
```

### 场景 5：代码解释

```cpp
AIRequest Request;
Request.TaskType = AITaskType::Explanation;
Request.Query = "Explain this code";
Request.Context = R"(
auto result = std::accumulate(vec.begin(), vec.end(), 0,
  [](int a, int b) { return a + b * b; });
)";
```

### 场景 6：中英文翻译

```cpp
AIRequest Request;
Request.TaskType = AITaskType::Translation;
Request.Lang = Language::Chinese;
Request.Query = "Translate to Chinese";
Request.Context = "// This function calculates the sum of squares";
```

## 最佳实践

1. **合理设置超时**：根据网络状况和任务复杂度调整
2. **启用缓存**：减少重复请求，节省成本
3. **使用异步请求**：避免阻塞主线程
4. **成本控制**：设置每日限制，监控使用情况
5. **选择合适的提供者**：根据任务类型和语言选择

## 故障排除

### API 密钥问题

```cpp
if (!std::getenv("OPENAI_API_KEY")) {
  std::cerr << "Warning: OPENAI_API_KEY not set\n";
}
```

### 网络连接问题

```cpp
auto Result = Provider->sendRequest(Request);
if (!Result) {
  llvm::handleAllErrors(Result.takeError(), [](const llvm::StringError& E) {
    std::cerr << "Network error: " << E.getMessage() << "\n";
  });
}
```

### 成本超限问题

```cpp
if (Tracker.isOverLimit()) {
  std::cerr << "Daily cost limit exceeded, switching to local provider\n";
  // 使用本地模型作为回退
}
```

---

**更多示例正在开发中...**