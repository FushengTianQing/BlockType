# BlockType 诊断系统国际化设计

## 概述

BlockType 编译器提供双语诊断信息系统，支持中英文错误、警告和提示信息。本文档描述诊断系统的国际化设计。

## 诊断消息格式

### YAML 格式

诊断消息使用 YAML 格式存储，便于维护和翻译。

```yaml
# diagnostics/en-US.yaml
version: 1
language: en-US

diagnostics:
  # 词法分析错误
  err_lex_unterminated_string:
    severity: error
    message: "unterminated string literal"
    note: "string literal must be closed with a matching quote"

  err_lex_invalid_char:
    severity: error
    message: "invalid character '%0' in source"

  # 语法分析错误
  err_parse_expected_token:
    severity: error
    message: "expected '%0' but found '%1'"

  err_parse_unexpected_token:
    severity: error
    message: "unexpected token '%0'"

  # 语义分析错误
  err_sema_undeclared_var:
    severity: error
    message: "use of undeclared identifier '%0'"

  err_sema_redefinition:
    severity: error
    message: "redefinition of '%0'"
    note: "previous definition is here"

  # 类型错误
  err_type_mismatch:
    severity: error
    message: "cannot convert '%0' to '%1'"
```

```yaml
# diagnostics/zh-CN.yaml
version: 1
language: zh-CN

diagnostics:
  # 词法分析错误
  err_lex_unterminated_string:
    severity: error
    message: "未终止的字符串字面量"
    note: "字符串字面量必须用匹配的引号闭合"

  err_lex_invalid_char:
    severity: error
    message: "源代码中包含无效字符 '%0'"

  # 语法分析错误
  err_parse_expected_token:
    severity: error
    message: "期望 '%0' 但找到 '%1'"

  err_parse_unexpected_token:
    severity: error
    message: "意外的标记 '%0'"

  # 语义分析错误
  err_sema_undeclared_var:
    severity: error
    message: "使用了未声明的标识符 '%0'"

  err_sema_redefinition:
    severity: error
    message: "'%0' 的重定义"
    note: "前一个定义在此处"

  # 类型错误
  err_type_mismatch:
    severity: error
    message: "无法将 '%0' 转换为 '%1'"
```

## 诊断引擎设计

### 核心接口

```cpp
// include/blocktype/Basic/DiagnosticsEngine.h
#pragma once

#include "blocktype/Basic/SourceLocation.h"
#include "blocktype/Basic/Language.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace blocktype {

enum class DiagnosticSeverity {
  Ignored,
  Note,
  Remark,
  Warning,
  Error,
  Fatal,
  ErrorFatal
};

class DiagnosticsEngine {
  TranslationManager *Translations;
  Language CurrentLang;

public:
  DiagnosticsEngine(TranslationManager *TM, Language Lang = Language::English)
    : Translations(TM), CurrentLang(Lang) {}

  /// 设置当前语言
  void setLanguage(Language Lang) { CurrentLang = Lang; }

  /// 报告诊断信息
  void Report(SourceLocation Loc, unsigned DiagID,
              llvm::ArrayRef<llvm::StringRef> Args = {});

  /// 格式化诊断消息
  std::string FormatMessage(unsigned DiagID,
                           llvm::ArrayRef<llvm::StringRef> Args);

  /// 获取诊断严重性
  DiagnosticSeverity getSeverity(unsigned DiagID) const;
};

} // namespace blocktype
```

### 翻译管理器

```cpp
// include/blocktype/Basic/TranslationManager.h
#pragma once

#include "blocktype/Basic/Language.h"
#include "llvm/ADT/StringMap.h"
#include <memory>
#include <string>

namespace blocktype {

struct DiagnosticInfo {
  DiagnosticSeverity Severity;
  std::string Message;
  std::string Note;
};

class TranslationManager {
  llvm::StringMap<DiagnosticInfo> EnDiagnostics;
  llvm::StringMap<DiagnosticInfo> ZhDiagnostics;

public:
  /// 加载诊断消息目录
  bool loadDiagnostics(Language Lang, llvm::StringRef Path);

  /// 获取诊断信息
  const DiagnosticInfo* getDiagnostic(unsigned DiagID, Language Lang) const;

  /// 获取所有支持的语言
  llvm::ArrayRef<Language> getSupportedLanguages() const;
};

} // namespace blocktype
```

## 诊断输出示例

### 英文输出

```
test.cpp:5:10: error: use of undeclared identifier 'foo'
    foo();
    ^
test.cpp:6:5: error: cannot convert 'int' to 'std::string'
    std::string s = 42;
    ^             ~~
```

### 中文输出

```
test.cpp:5:10: 错误：使用了未声明的标识符 'foo'
    foo();
    ^
test.cpp:6:5: 错误：无法将 'int' 转换为 'std::string'
    std::string s = 42;
    ^             ~~
```

## 参数化消息

诊断消息支持参数化，使用 `%0`, `%1`, `%2` 等占位符：

```cpp
// 定义诊断消息
err_type_mismatch:
  message: "cannot convert '%0' to '%1'"

// 使用诊断消息
Diags.Report(Loc, diag::err_type_mismatch)
  << SourceType << TargetType;

// 输出结果
// cannot convert 'int' to 'std::string'
```

## 实现要点

### 1. 消息加载

- 编译器启动时加载默认语言的诊断消息
- 切换语言时重新加载对应的消息目录
- 消息目录缓存在内存中，避免重复加载

### 2. 消息格式化

- 使用 LLVM 的格式化工具
- 支持多种参数类型（字符串、整数、类型等）
- 格式化失败时回退到原始消息

### 3. 源位置显示

- 显示文件名、行号、列号
- 显示源代码行和错误位置标记
- 支持多行错误显示

## 总结

BlockType 的诊断系统国际化设计遵循以下原则：

1. **用户友好**：使用母语显示错误信息
2. **易于维护**：消息目录独立于代码
3. **灵活切换**：运行时语言切换
4. **完整信息**：提供详细的错误位置和上下文
