# BlockType AI 功能使用指南

## 概述

BlockType 集成了强大的 AI 辅助功能，支持多种 AI 提供者，帮助开发者提高编译和代码质量。

## 快速开始

### 1. 启用 AI 辅助

```bash
# 基本用法
blocktype --ai-assist source.cpp

# 指定 AI 提供者
blocktype --ai-assist --ai-provider=openai source.cpp

# 使用本地模型
blocktype --ai-assist --ai-provider=local --ollama-endpoint=http://localhost:11434 source.cpp
```

### 2. 配置 API 密钥

#### 环境变量方式

```bash
# OpenAI
export OPENAI_API_KEY="your-openai-api-key"

# Claude
export ANTHROPIC_API_KEY="your-anthropic-api-key"

# 通义千问
export QWEN_API_KEY="your-qwen-api-key"
```

#### 配置文件方式

创建 `~/.blocktype/ai_config.json`:

```json
{
  "default_provider": "openai",
  "enable_cache": true,
  "max_cost_per_day": 10.0,
  "providers": {
    "openai": {
      "api_key": "your-openai-api-key",
      "model": "gpt-4"
    },
    "claude": {
      "api_key": "your-anthropic-api-key",
      "model": "claude-3-5-sonnet-20241022"
    },
    "qwen": {
      "api_key": "your-qwen-api-key",
      "model": "qwen-plus"
    },
    "local": {
      "endpoint": "http://localhost:11434",
      "model": "codellama"
    }
  }
}
```

## AI 提供者选择

### OpenAI (GPT-4)

**优势**：
- 复杂推理能力强
- 代码理解深入
- 多语言支持好

**适用场景**：
- 复杂错误分析
- 架构设计建议
- 性能优化建议

**使用示例**：
```bash
blocktype --ai-assist --ai-provider=openai --ai-model=gpt-4 source.cpp
```

### Claude (Claude 3.5)

**优势**：
- 代码生成质量高
- 遵循最佳实践
- 代码风格一致

**适用场景**：
- 代码补全
- 重构建议
- 代码审查

**使用示例**：
```bash
blocktype --ai-assist --ai-provider=claude --ai-model=claude-3-5-sonnet-20241022 source.cpp
```

### 通义千问 (Qwen)

**优势**：
- 中文理解能力强
- 中文错误提示友好
- 本地化支持好

**适用场景**：
- 中文代码注释
- 中文错误解释
- 中文文档生成

**使用示例**：
```bash
blocktype --ai-assist --ai-provider=qwen --ai-model=qwen-plus source.cpp
```

### 本地模型 (Ollama)

**优势**：
- 完全离线运行
- 数据隐私保护
- 无 API 成本

**适用场景**：
- 离线开发环境
- 敏感项目
- 成本控制

**使用示例**：
```bash
# 启动 Ollama 服务
ollama serve

# 拉取模型
ollama pull codellama

# 使用本地模型
blocktype --ai-assist --ai-provider=local --ai-model=codellama source.cpp
```

## AI 功能特性

### 1. 错误修复建议

AI 会分析编译错误并提供修复建议：

```cpp
// 错误代码
int main() {
  std::cout << "Hello"  // 缺少分号
}
```

AI 建议：
```
错误：第 2 行缺少分号
建议：在 "Hello" 后添加分号
修复：std::cout << "Hello";
```

### 2. 代码补全

AI 可以根据上下文补全代码：

```cpp
// 输入
std::vector<int> nums = {1, 2, 3, 4, 5};
// AI 补全排序代码
std::sort(nums.begin(), nums.end());
```

### 3. 性能优化建议

AI 会分析代码性能瓶颈：

```cpp
// 原始代码
std::string result;
for (int i = 0; i < 1000; ++i) {
  result += std::to_string(i);
}

// AI 优化建议
std::ostringstream oss;
for (int i = 0; i < 1000; ++i) {
  oss << i;
}
std::string result = oss.str();
```

### 4. 安全检查

AI 会识别潜在的安全问题：

```cpp
// 不安全代码
char buffer[10];
strcpy(buffer, user_input);  // 缓冲区溢出风险

// AI 建议
strncpy(buffer, user_input, sizeof(buffer) - 1);
buffer[sizeof(buffer) - 1] = '\0';
```

### 5. 代码解释

AI 可以解释复杂代码：

```cpp
// 复杂代码
auto result = std::accumulate(vec.begin(), vec.end(), 0, 
  [](int a, int b) { return a + b * b; });

// AI 解释
// 计算 vector 中所有元素的平方和
// 使用 lambda 表达式：a 是累加器，b 是当前元素
// 结果 = 1² + 2² + 3² + ...
```

## 成本控制

### 设置每日成本限制

```bash
# 设置每日最大成本为 $5
blocktype --ai-assist --ai-cost-limit=5.0 source.cpp
```

### 查看成本报告

```bash
# 查看今日成本
blocktype --ai-assist --show-cost

# 输出示例
今日成本: $2.35
总请求数: 45
缓存命中率: 68%
```

### 启用缓存

```bash
# 启用缓存（默认启用）
blocktype --ai-assist --ai-cache source.cpp

# 禁用缓存
blocktype --ai-assist --ai-cache=false source.cpp
```

## 最佳实践

### 1. 选择合适的提供者

- **中文项目** → 优先使用 Qwen
- **代码生成** → 优先使用 Claude
- **复杂分析** → 优先使用 OpenAI
- **离线环境** → 使用 Local (Ollama)

### 2. 优化成本

- 启用缓存减少重复请求
- 设置合理的成本限制
- 使用本地模型处理简单任务

### 3. 提高准确性

- 提供完整的上下文代码
- 明确指定任务类型
- 使用合适的模型

### 4. 保护隐私

- 敏感代码使用本地模型
- 不要在代码中硬编码密钥
- 定期清理缓存

## 故障排除

### 1. API 密钥无效

```
错误：OpenAI API key not set or invalid
解决：检查环境变量 OPENAI_API_KEY 是否正确设置
```

### 2. 网络连接失败

```
错误：Failed to connect to API endpoint
解决：检查网络连接，或使用本地模型
```

### 3. 成本超限

```
错误：Daily cost limit exceeded
解决：等待明天重置，或提高成本限制
```

### 4. Ollama 服务未运行

```
错误：Failed to connect to Ollama endpoint
解决：启动 Ollama 服务：ollama serve
```

## 高级配置

### 自定义模型参数

```json
{
  "providers": {
    "openai": {
      "model": "gpt-4",
      "temperature": 0.7,
      "max_tokens": 2000
    }
  }
}
```

### 多提供者策略

```json
{
  "routing": {
    "chinese_tasks": "qwen",
    "code_generation": "claude",
    "complex_reasoning": "openai",
    "fallback": "local"
  }
}
```

## 示例场景

### 场景 1：修复编译错误

```bash
# 编译并获取 AI 建议
blocktype --ai-assist --ai-provider=openai buggy_code.cpp

# AI 输出
错误分析：
- 第 15 行：类型不匹配
- 第 23 行：未声明的变量

修复建议：
1. 将 int 改为 double
2. 添加变量声明：int count = 0;
```

### 场景 2：性能优化

```bash
# 分析性能瓶颈
blocktype --ai-assist --ai-task=performance slow_code.cpp

# AI 输出
性能问题：
- 第 10-20 行：循环中的字符串拼接
- 第 30 行：不必要的拷贝

优化建议：
1. 使用 std::ostringstream 替代字符串拼接
2. 使用 const 引用避免拷贝
```

### 场景 3：安全审计

```bash
# 安全检查
blocktype --ai-assist --ai-task=security sensitive_code.cpp

# AI 输出
安全问题：
- 第 5 行：潜在的缓冲区溢出
- 第 12 行：未验证的用户输入

修复建议：
1. 使用 strncpy 替代 strcpy
2. 添加输入验证逻辑
```

## 总结

BlockType 的 AI 功能可以显著提高开发效率和代码质量。通过合理配置和使用，可以在保证成本控制的同时，获得最佳的辅助效果。

---

**文档版本**: 1.0  
**最后更新**: 2026-04-14
