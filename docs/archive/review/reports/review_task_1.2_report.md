# Task 1.2: 细化Parser流程 - 完成报告

**Task ID**: 1.2  
**任务名称**: 细化Parser流程  
**执行时间**: 2026-04-19 16:00-16:30  
**状态**: ✅ DONE

---

## 📋 执行结果

### 核心发现

从 `Parser::parseTranslationUnit()` 到各种具体声明类型的解析，形成了一个完整的**声明分发树**。

---

## 🔗 Parser调用链详解

```mermaid
graph TD
    A[parseTranslationUnit] --> B{循环直到EOF}
    B --> C{Token类型?}
    
    C -->|#| D[skipPreprocessingDirective<br/>跳过预处理指令]
    C -->|其他| E[parseDeclaration]
    
    E --> F{声明类型识别}
    
    F -->|module| G[parseModuleDeclaration]
    F -->|import| H[parseImportDeclaration]
    F -->|export| I[parseExportDeclaration]
    F -->|template| J[parseTemplateDeclaration]
    F -->|namespace/inline namespace| K[parseNamespaceDeclaration]
    F -->|using| L[parseUsingDirective/Alias/Declaration]
    F -->|class| M[parseClassDeclaration]
    F -->|struct| N[parseStructDeclaration]
    F -->|enum| O[parseEnumDeclaration]
    F -->|union| P[parseUnionDeclaration]
    F -->|typedef| Q[parseTypedefDeclaration]
    F -->|static_assert| R[parseStaticAssertDeclaration]
    F -->|asm| S[parseAsmDeclaration]
    F -->|[[]]| T[parseAttributeSpecifier]
    F -->|extern "C/C++"| U[parseLinkageSpecDeclaration]
    
    F -->|默认路径| V[parseDeclSpecifierSeq]
    V --> W[parseDeclarator]
    W --> X{Declarator类型?}
    
    X -->|函数| Y[buildFunctionDecl]
    X -->|变量| Z[buildVarDecl]
    
    D --> B
    G --> B
    H --> B
    I --> B
    J --> B
    K --> B
    L --> B
    M --> B
    N --> B
    O --> B
    P --> B
    Q --> B
    R --> B
    S --> B
    T --> B
    U --> B
    Y --> B
    Z --> B
```

---

## 📝 关键代码位置

### 1. parseTranslationUnit() - 主循环
**文件**: `src/Parse/Parser.cpp`  
**行号**: L33-63

```cpp
TranslationUnitDecl *Parser::parseTranslationUnit() {
  TranslationUnitDecl *TU = Actions.ActOnTranslationUnitDecl(StartLoc);
  
  while (!Tok.is(TokenKind::eof)) {
    // 跳过预处理指令
    if (Tok.is(TokenKind::hash)) {
      skipPreprocessingDirective();
      continue;
    }
    
    // 解析声明
    Decl *D = parseDeclaration();
    if (D) {
      // Sema已自动注册到CurContext
    } else {
      // 错误恢复
      if (!skipUntilNextDeclaration()) {
        break;
      }
    }
  }
  
  return TU;
}
```

**关键点**:
- 循环直到EOF
- 跳过 `#include`, `#define` 等预处理指令
- 每个声明通过 `parseDeclaration()` 分发
- 错误恢复机制：跳过到下一个声明边界

---

### 2. parseDeclaration() - 声明分发器
**文件**: `src/Parse/ParseDecl.cpp`  
**行号**: L79-340

#### 2.1 特殊声明类型（L82-285）

按关键字识别，直接调用对应的解析函数：

| 关键字 | 函数 | 行号 | 说明 |
|--------|------|------|------|
| `module` | parseModuleDeclaration | L83 | C++20模块 |
| `import` | parseImportDeclaration | L88 | C++20导入 |
| `export` | parseExportDeclaration | L100 | C++20导出 |
| `template` | parseTemplateDeclaration | L106 | 模板声明 |
| `namespace` | parseNamespaceDeclaration | L113 | 命名空间 |
| `inline namespace` | parseNamespaceDeclaration | L119 | 内联命名空间 |
| `using namespace` | parseUsingDirective | L129 | using指令 |
| `using X =` | ActOnTypeAliasDecl | L167 | 类型别名 |
| `using A::B` | ActOnUsingDecl | L184 | using声明 |
| `class` | parseClassDeclaration | L199 | 类声明 |
| `struct` | parseStructDeclaration | L211 | 结构体声明 |
| `enum` | parseEnumDeclaration | L223 | 枚举声明 |
| `union` | parseUnionDeclaration | L235 | 联合体声明 |
| `typedef` | parseTypedefDeclaration | L247 | typedef |
| `static_assert` | parseStaticAssertDeclaration | L254 | 静态断言 |
| `asm` | parseAsmDeclaration | L261 | 汇编声明 |
| `[[]]` | parseAttributeSpecifier | L270 | 属性说明符 |
| `extern "C"` | parseLinkageSpecDeclaration | L282 | 链接规范 |

#### 2.2 通用声明路径（L287-340）

对于没有特殊关键字的声明（变量、函数），走通用路径：

```cpp
// Step 1: 解析声明说明符序列（int, const, static等）
DeclSpec DS;
parseDeclSpecifierSeq(DS);

if (!DS.hasTypeSpecifier()) {
  emitError(DiagID::err_expected_type);
  return nullptr;
}

// Step 2: 检查结构化绑定（auto [x, y] = expr）
if (Tok.is(TokenKind::l_square)) {
  return nullptr;  // 应该在语句中处理
}

// Step 3: 解析声明符（名字 + 指针/引用/数组/函数修饰）
Declarator D(DS, DeclaratorContext::FileContext);
parseDeclarator(D);

if (!D.hasName()) {
  emitError(DiagID::err_expected_identifier);
  return nullptr;
}

// Step 4: 处理限定名（ClassName::member）
if (Tok.is(TokenKind::coloncolon)) {
  consumeToken();
  D.setName(Tok.getText(), Tok.getLocation());
  consumeToken();
}

// Step 5: 根据声明符类型构建AST
if (D.isFunctionDeclarator()) {
  return buildFunctionDecl(D);  // 函数声明
}
return buildVarDecl(D);  // 变量声明
```

---

## 🎯 关键发现

### 1. 声明识别策略

**两级分发**：
1. **第一级**：按关键字快速识别特殊声明（class, template, namespace等）
2. **第二级**：通用路径通过 DeclSpec + Declarator 分析

**优势**：
- 特殊声明有专用解析器，更精确
- 通用声明复用 DeclSpec/Declarator 框架

---

### 2. 错误恢复机制

**三层恢复**：
1. **声明级别**：`skipUntilNextDeclaration()` - 跳到下一个声明边界
2. **Token级别**：`consumeToken()` - 消耗错误Token
3. **循环级别**：`continue` - 跳过当前迭代，继续下一个

**示例**：
```cpp
Decl *D = parseDeclaration();
if (D) {
  // 成功
} else {
  // 错误恢复
  if (!skipUntilNextDeclaration()) {
    break;  // 到达EOF
  }
}
```

---

### 3. Sema集成点

**所有声明最终都调用 Sema 的 ActOn 函数**：

| Parser函数 | Sema回调 | 说明 |
|-----------|---------|------|
| buildFunctionDecl | ActOnFunctionDecl | 函数声明 |
| buildVarDecl | ActOnVarDeclFull | 变量声明 |
| parseClassDeclaration | ActOnTag | 类声明 |
| parseTemplateDeclaration | ActOnFunctionTemplateDecl | 模板声明 |
| ... | ... | ... |

**关键点**：
- Parser只负责语法分析和AST节点创建
- Sema负责语义检查和符号表注册
- AST节点通过 `Actions.ActOnXXX()` 传递给Sema

---

### 4. 结构化绑定的特殊处理

**问题**：`auto [x, y] = expr` 在 L301-306 被检测但返回nullptr

**原因**：注释说明"Structured bindings should be handled in parseDeclarationStatement, not here"

**影响**：
- 顶层的结构化绑定无法解析
- 只能在语句上下文中解析（如函数体内）

**建议**：这是一个潜在的bug或设计限制

---

## 📊 统计信息

### 声明类型覆盖

| 类别 | 数量 | 示例 |
|------|------|------|
| 特殊声明 | 18种 | class, template, namespace... |
| 通用声明 | 2种 | 函数、变量 |
| **总计** | **20种** | - |

### 代码规模

- `parseTranslationUnit()`: 30行
- `parseDeclaration()`: 260行
- 分支数量: ~20个
- Sema回调点: ~15个

---
---

## ✅ 验收标准

- [x] 追踪parseTranslationUnit()的实现
- [x] 理解parseDeclaration()的分支逻辑
- [x] 识别所有声明类型的处理方式
- [x] 绘制完整的调用流程图
- [x] 记录关键的Sema集成点
- [x] 发现潜在问题

---

## 🔗 下一步

**依赖的Task**: Task 1.3 (细化Sema流程)  
**可以开始**: 是（依赖已满足）

**建议**: 
1. 从 Sema::ActOnFunctionDecl 开始深入
2. 追踪 Sema 如何处理不同类型的声明
3. 识别名称查找、类型检查的关键点

---

**输出文件**: 
- 本报告: `docs/review/reports/review_task_1.2_report.md`
- 流程图: 见上方Mermaid图
