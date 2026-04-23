# BlockType 编译器架构

## 概述

BlockType 是一个现代化的 C++26 编译器，具有以下特点：

- **双语支持**: 原生支持中文和英文编程
- **AI 驱动**: 深度集成 AI 能力，提供智能诊断和优化建议
- **模块化设计**: 清晰的模块划分，便于维护和扩展
- **LLVM 后端**: 使用 LLVM 生成高质量的目标代码

## 编译流程

```
源代码 (中文/英文)
    ↓
┌─────────────────┐
│   Lexer         │ 词法分析器
│  (双语支持)      │
└─────────────────┘
    ↓ Tokens
┌─────────────────┐
│   Preprocessor  │ 预处理器
└─────────────────┘
    ↓ Tokens
┌─────────────────┐
│   Parser        │ 语法分析器
└─────────────────┘
    ↓ AST
┌─────────────────┐
│   Sema          │ 语义分析
│  (类型检查)      │
└─────────────────┘
    ↓ Typed AST
┌─────────────────┐
│   IRGen         │ IR 生成
└─────────────────┘
    ↓ LLVM IR
┌─────────────────┐
│   LLVM Backend  │ LLVM 后端
└─────────────────┘
    ↓
目标代码
```

## 模块划分

### 1. Basic 模块

**职责**: 提供编译器基础设施

**主要组件**:
- `SourceLocation`: 源代码位置表示
- `SourceManager`: 源代码管理
- `DiagnosticsEngine`: 诊断引擎
- `FileManager`: 文件管理器
- `Language`: 语言配置（中/英文）
- `Unicode`: Unicode 工具类
- `Translation`: 翻译管理器

**关键设计**:
```cpp
namespace blocktype {

// 源位置使用紧凑的 32-bit ID 表示
class SourceLocation {
  unsigned ID;
public:
  bool isValid() const;
  unsigned getID() const;
};

// 诊断引擎支持双语消息
class DiagnosticsEngine {
  LanguageManager LangMgr;
  TranslationManager TransMgr;
public:
  void report(SourceLocation Loc, DiagID ID);
};

} // namespace blocktype
```

### 2. Lex 模块

**职责**: 词法分析和预处理

**主要组件**:
- `Lexer`: 词法分析器
- `Preprocessor`: 预处理器
- `Token`: Token 定义
- `KeywordTable`: 关键字表（双语）

**关键设计**:
```cpp
namespace blocktype {

// Token 类型
enum class TokenKind {
  // 标准关键字
  kw_if, kw_else, kw_while,
  // 中文关键字
  kw_如果, kw_否则, kw_循环,
  // ...
};

// Lexer 支持双语标识符
class Lexer {
  Language Lang;
public:
  bool isIdentifierStart(uint32_t CP);
  bool isIdentifierContinue(uint32_t CP);
};

} // namespace blocktype
```

### 3. Parse 模块

**职责**: 语法分析

**主要组件**:
- `Parser`: 语法分析器
- `ParsingDecl`: 声明解析
- `ParsingExpr`: 表达式解析
- `ParsingStmt`: 语句解析

**关键设计**:
```cpp
namespace blocktype {

class Parser {
  Preprocessor &PP;
  DiagnosticsEngine &Diags;
public:
  // 递归下降解析器
  DeclResult parseDeclaration();
  ExprResult parseExpression();
  StmtResult parseStatement();
};

} // namespace blocktype
```

### 4. AST 模块

**职责**: 抽象语法树定义

**主要组件**:
- `Decl`: 声明节点
- `Expr`: 表达式节点
- `Stmt`: 语句节点
- `Type`: 类型表示

**关键设计**:
```cpp
namespace blocktype {

// AST 节点基类
class ASTNode {
  SourceLocation Loc;
public:
  SourceLocation getLocation() const;
};

// 表达式节点
class Expr : public ASTNode {
  QualType Ty;
public:
  QualType getType() const;
};

// 声明节点
class Decl : public ASTNode {
  IdentifierInfo *Name;
public:
  StringRef getName() const;
};

} // namespace blocktype
```

### 5. Sema 模块

**职责**: 语义分析

**主要组件**:
- `Sema`: 语义分析器
- `Scope`: 作用域管理
- `TypeChecker`: 类型检查
- `OverloadResolver`: 重载解析

**关键设计**:
```cpp
namespace blocktype {

class Sema {
  ASTContext &Context;
  DiagnosticsEngine &Diags;
public:
  // 类型检查
  QualType checkType(Expr *E);
  
  // 名称查找
  Decl* lookupName(IdentifierInfo *Name, Scope *S);
  
  // 重载解析
  FunctionDecl* resolveOverload(
    ArrayRef<FunctionDecl*> Candidates,
    ArrayRef<Expr*> Args
  );
};

} // namespace blocktype
```

### 6. CodeGen 模块

**职责**: LLVM IR 生成

**主要组件**:
- `CodeGenModule`: 模块级代码生成
- `CodeGenFunction`: 函数级代码生成
- `CodeGenTypes`: 类型映射
- `CodeGenExpr`: 表达式代码生成

**关键设计**:
```cpp
namespace blocktype {

class CodeGenModule {
  llvm::Module &TheModule;
  llvm::LLVMContext &VMContext;
public:
  llvm::Function* getFunction(FunctionDecl *FD);
  llvm::GlobalVariable* getGlobalVar(VarDecl *VD);
};

class CodeGenFunction {
  CodeGenModule &CGM;
  llvm::Function *CurFn;
public:
  llvm::Value* emitExpr(Expr *E);
  void emitStmt(Stmt *S);
};

} // namespace blocktype
```

### 7. AI 模块

**职责**: AI 功能集成

**主要组件**:
- `AIInterface`: AI 接口抽象
- `AIOrchestrator`: 多模型编排
- `OpenAIProvider`: OpenAI 适配器
- `ClaudeProvider`: Claude 适配器
- `LocalProvider`: 本地模型适配器

**关键设计**:
```cpp
namespace blocktype {

class AIInterface {
public:
  virtual Response complete(const Request &Req) = 0;
  virtual ~AIInterface() = default;
};

class AIOrchestrator {
  std::map<std::string, std::unique_ptr<AIInterface>> Providers;
public:
  Response route(const Request &Req);
  void registerProvider(std::string Name, 
                       std::unique_ptr<AIInterface> Provider);
};

} // namespace blocktype
```

### 8. Visualization 模块

**职责**: 可视化和调试支持

**主要组件**:
- `ASTVisualizer`: AST 可视化
- `IRVisualizer`: IR 可视化
- `TraceProbes`: 追踪探针

**关键设计**:
```cpp
namespace blocktype {

class ASTVisualizer {
public:
  void dumpDOT(TranslationUnitDecl *TU, raw_ostream &OS);
  void dumpJSON(TranslationUnitDecl *TU, raw_ostream &OS);
};

class TraceProbes {
public:
  void traceLex(const Token &T);
  void traceParse(const ASTNode *N);
  void traceSema(const ASTNode *N);
};

} // namespace blocktype
```

## 数据流

### 1. 源代码 → Tokens

```
源代码字符串
    ↓ FileManager
MemoryBuffer
    ↓ Lexer
Token 流
```

### 2. Tokens → AST

```
Token 流
    ↓ Parser
AST 节点
    ↓ Sema
类型化 AST
```

### 3. AST → LLVM IR

```
类型化 AST
    ↓ CodeGenModule
LLVM Module
    ↓ CodeGenFunction
LLVM Function
    ↓ CodeGenExpr
LLVM Value
```

## 双语支持架构

### 关键字映射

```cpp
// 关键字映射表
static const StringMap<TokenKind> ChineseKeywords = {
  {"如果", kw_if},
  {"否则", kw_else},
  {"循环", kw_while},
  {"返回", kw_return},
  // ...
};
```

### 标识符支持

```cpp
// Unicode 标识符检查
bool Lexer::isIdentifierStart(uint32_t CP) {
  // ASCII 字母
  if (('a' <= CP && CP <= 'z') ||
      ('A' <= CP && CP <= 'Z') ||
      CP == '_')
    return true;
  
  // 中文字符
  if (Unicode::isCJK(CP))
    return true;
  
  // Unicode ID_Start
  return Unicode::isIDStart(CP);
}
```

### 诊断消息国际化

```yaml
# diagnostics/zh-CN.yaml
err_undeclared_var:
  message: "未声明的变量 '%0'"
  note: "请确保在使用前声明该变量"

err_type_mismatch:
  message: "类型不匹配：期望 '%0'，实际 '%1'"
  note: "请检查表达式类型"
```

## AI 集成架构

### 多模型编排

```
用户请求
    ↓
AIOrchestrator
    ├─→ OpenAI Provider (GPT-4)
    ├─→ Claude Provider (Claude-3)
    ├─→ Qwen Provider (通义千问)
    └─→ Local Provider (Ollama)
    ↓
智能路由 (基于成本、速度、能力)
    ↓
最佳模型响应
```

### AI 辅助诊断

```cpp
class AIDiagnosticsHelper {
  AIOrchestrator &AI;
public:
  // 智能错误解释
  std::string explainError(const Diagnostic &Diag);
  
  // 修复建议
  std::vector<FixIt> suggestFixes(const Diagnostic &Diag);
  
  // 代码改进建议
  std::vector<Suggestion> analyzeCode(const ASTNode *N);
};
```

## 性能优化策略

### 1. 内存管理

- 使用 LLVM 的 BumpPtrAllocator 进行 AST 节点分配
- 使用 SmallVector 避免小数组的堆分配
- 使用 StringRef 避免字符串拷贝

### 2. 缓存策略

- 文件缓存 (FileManager)
- AI 响应缓存 (AIOrchestrator)
- 类型缓存 (TypeCache)

### 3. 并行处理

- 多文件并行编译
- AI 请求并行处理
- 模板实例化并行化

## 测试策略

### 单元测试

- 使用 Google Test
- 每个模块独立测试
- 覆盖率目标: 80%+

### 回归测试

- 使用 LLVM Lit
- 端到端测试
- 兼容性测试

### 性能测试

- 编译速度基准
- 内存使用基准
- 生成代码质量基准

## 扩展点

### 1. 新语言支持

- 扩展 Language 枚举
- 添加关键字映射
- 添加诊断消息翻译

### 2. 新 AI 模型

- 实现 AIInterface 接口
- 注册到 AIOrchestrator
- 配置路由规则

### 3. 新目标平台

- 实现 TargetInfo 接口
- 添加 ABI 支持
- 配置工具链

## 依赖关系

```
blocktype
├── LLVM 18+ (必须)
│   ├── Core
│   ├── Support
│   ├── ORC JIT
│   └── Native
├── ICU (必须，用于 Unicode 支持)
├── libcurl (必须，用于 AI HTTP 请求)
├── nlohmann/json (必须，用于 JSON 处理)
└── Google Test (开发依赖)
```

## 构建配置

```cmake
# 主要构建选项
option(BLOCKTYPE_ENABLE_AI "Enable AI features" ON)
option(BLOCKTYPE_ENABLE_TESTS "Enable tests" ON)
option(BLOCKTYPE_ENABLE_VISUALIZATION "Enable visualization" ON)

# 目标平台
option(BLOCKTYPE_TARGET_LINUX "Build for Linux" ON)
option(BLOCKTYPE_TARGET_MACOS "Build for macOS" ON)
option(BLOCKTYPE_TARGET_WINDOWS "Build for Windows" OFF)
```

## 未来规划

1. **WebAssembly 支持**: 编译到 WASM
2. **IDE 集成**: LSP 服务器
3. **包管理器**: 集成包管理
4. **增量编译**: 支持增量构建
5. **分布式编译**: 支持分布式构建
