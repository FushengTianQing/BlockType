# Phase 1：词法分析与预处理
> **目标：** 实现完整的 C++26 词法分析器和预处理器，将源代码转换为 Token 流并处理预处理指令
> **前置依赖：** Phase 0 完成（项目基础设施）
> **验收标准：** 能正确词法分析 C++26 源代码；预处理指令正确处理；宏展开正确；支持 C++26 新增的 Token 和预处理特性

---

## 📌 阶段总览

```
Phase 1 包含 4 个 Stage，共 14 个 Task，预计 6 周完成。
建议并行度：Stage 1.1 和 1.2 可并行开始，Stage 1.3 依赖 1.1 完成，Stage 1.4 最后完成。
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 1.1** | Token 系统 | Token 定义、TokenKind 枚举 | 1 周 |
| **Stage 1.2** | Lexer 核心 | 词法分析器、Token 识别 | 2 周 |
| **Stage 1.3** | Preprocessor | 预处理器、宏展开、头文件搜索 | 2 周 |
| **Stage 1.4** | C++26 特性 + 测试 | C++26 新 Token、完整测试 | 1 周 |

**Phase 1 架构图：**

```
源代码文件
    │
    ▼
┌─────────────────────────────────────────────────────────┐
│                   FileManager                            │
│  读取文件、缓存、编码检测                                │
└─────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────┐
│                      Lexer                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │  字符处理   │→ │  Token 识别  │→ │  Token 缓冲    │  │
│  └─────────────┘  └─────────────┘  └─────────────────┘  │
└─────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────┐
│                   Preprocessor                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │  宏展开    │  │  条件编译   │  │  头文件搜索    │  │
│  └─────────────┘  └─────────────┘  └─────────────────┘  │
└─────────────────────────────────────────────────────────┘
    │
    ▼
Token 流 (to Parser)
```

---

## Stage 1.1 — Token 系统

### Task 1.1.1 TokenKind 枚举定义

**目标：** 定义完整的 C++26 Token 类型

**开发要点：**

- **E1.1.1** 创建 `include/nova-cc/Lex/Token.h`，定义 Token 结构：
  ```cpp
  #pragma once
  
  #include "nova-cc/Basic/LLVM.h"
  #include "nova-cc/Basic/SourceLocation.h"
  
  namespace nova {
  
  enum class TokenKind {
  #define TOKEN(X) X,
  #define KEYWORD(X, Y) kw_##X,
  #include "nova-cc/Lex/TokenKinds.def"
  #undef TOKEN
  #undef KEYWORD
    NUM_TOKENS
  };
  
  class Token {
    TokenKind Kind;
    SourceLocation Location;
    unsigned Length;
    const char *LiteralData;  // 字面量的原始数据指针
    
  public:
    TokenKind getKind() const { return Kind; }
    void setKind(TokenKind K) { Kind = K; }
    
    SourceLocation getLocation() const { return Location; }
    void setLocation(SourceLocation L) { Location = L; }
    
    unsigned getLength() const { return Length; }
    void setLength(unsigned L) { Length = L; }
    
    bool is(TokenKind K) const { return Kind == K; }
    bool isNot(TokenKind K) const { return Kind != K; }
    
    StringRef getLiteralData() const;
    void setLiteralData(const char *Data) { LiteralData = Data; }
  };
  
  } // namespace nova
  ```

- **E1.1.2** 创建 `include/nova-cc/Lex/TokenKinds.def`：
  ```
  // TokenKinds.def - Token 和关键字定义
  
  // 标点符号
  TOKEN(unknown)          // 未知字符
  TOKEN(eof)              // 文件结束
  TOKEN(eod)              // 指令结束
  TOKEN(code_completion)  // 代码补全
  
  // 标识符和字面量
  TOKEN(identifier)       // 标识符
  TOKEN(numeric_constant) // 数字字面量
  TOKEN(char_constant)    // 字符字面量 'c'
  TOKEN(wide_char_constant)    // 宽字符 L'c'
  TOKEN(utf8_char_constant)    // UTF-8 字符 u8'c' (C++20)
  TOKEN(utf16_char_constant)   // UTF-16 字符 u'c' (C++11)
  TOKEN(utf32_char_constant)   // UTF-32 字符 U'c' (C++11)
  TOKEN(string_literal)        // 字符串字面量 "..."
  TOKEN(wide_string_literal)   // 宽字符串 L"..."
  TOKEN(utf8_string_literal)   // UTF-8 字符串 u8"..." (C++11)
  TOKEN(utf16_string_literal)  // UTF-16 字符串 u"..." (C++11)
  TOKEN(utf32_string_literal)  // UTF-32 字符串 U"..." (C++11)
  
  // 运算符
  TOKEN(plus)             // +
  TOKEN(minus)            // -
  TOKEN(star)             // *
  TOKEN(slash)            // /
  TOKEN(percent)          // %
  TOKEN(caret)            // ^
  TOKEN(amp)              // &
  TOKEN(pipe)             // |
  TOKEN(tilde)            // ~
  TOKEN(exclaim)          // !
  TOKEN(equal)            // =
  TOKEN(less)             // <
  TOKEN(greater)          // >
  TOKEN(plusplus)         // ++
  TOKEN(minusminus)       // --
  TOKEN(lessequal)        // <=
  TOKEN(greaterequal)     // >=
  TOKEN(equalequal)       // ==
  TOKEN(exclaimequal)     // !=
  TOKEN(lessless)         // <<
  TOKEN(greatergreater)   // >>
  TOKEN(lesslessequal)    // <<=
  TOKEN(greatergreaterequal) // >>=
  TOKEN(plusequal)        // +=
  TOKEN(minusequal)       // -=
  TOKEN(starequal)        // *=
  TOKEN(slashequal)       // /=
  TOKEN(percentequal)     // %=
  TOKEN(caretequal)       // ^=
  TOKEN(ampequal)         // &=
  TOKEN(pipeequal)        // |=
  TOKEN(ampamp)           // &&
  TOKEN(pipepipe)         // ||
  TOKEN(lesslessless)     // <<<  (C++26 digraph)
  TOKEN(greatergreatergreater) // >>> (C++26 digraph)
  TOKEN(spaceship)        // <=> (C++20)
  TOKEN(hash)             // #
  TOKEN(hashhash)         // ##
  TOKEN(hashat)           // #@ (MSVC 扩展)
  
  // 括号
  TOKEN(l_paren)          // (
  TOKEN(r_paren)          // )
  TOKEN(l_brace)          // {
  TOKEN(r_brace)          // }
  TOKEN(l_square)         // [
  TOKEN(r_square)         // ]
  
  // 标点
  TOKEN(period)           // .
  TOKEN(ellipsis)         // ...
  TOKEN(comma)            // ,
  TOKEN(colon)            // :
  TOKEN(coloncolon)       // ::
  TOKEN(semicolon)        // ;
  TOKEN(question)         // ?
  TOKEN(at)               // @
  
  // C++26 新增
  TOKEN(dotdot)           // .. (placeholder pattern)
  
  // ========== 关键字 ==========
  // C 关键字
  KEYWORD(auto, KEYALL)
  KEYWORD(break, KEYALL)
  KEYWORD(case, KEYALL)
  KEYWORD(char, KEYALL)
  KEYWORD(const, KEYALL)
  KEYWORD(continue, KEYALL)
  KEYWORD(default, KEYALL)
  KEYWORD(do, KEYALL)
  KEYWORD(double, KEYALL)
  KEYWORD(else, KEYALL)
  KEYWORD(enum, KEYALL)
  KEYWORD(extern, KEYALL)
  KEYWORD(float, KEYALL)
  KEYWORD(for, KEYALL)
  KEYWORD(goto, KEYALL)
  KEYWORD(if, KEYALL)
  KEYWORD(inline, KEYALL)
  KEYWORD(int, KEYALL)
  KEYWORD(long, KEYALL)
  KEYWORD(register, KEYALL)
  KEYWORD(restrict, KEYALL)
  KEYWORD(return, KEYALL)
  KEYWORD(short, KEYALL)
  KEYWORD(signed, KEYALL)
  KEYWORD(sizeof, KEYALL)
  KEYWORD(static, KEYALL)
  KEYWORD(struct, KEYALL)
  KEYWORD(switch, KEYALL)
  KEYWORD(typedef, KEYALL)
  KEYWORD(union, KEYALL)
  KEYWORD(unsigned, KEYALL)
  KEYWORD(void, KEYALL)
  KEYWORD(volatile, KEYALL)
  KEYWORD(while, KEYALL)
  
  // C++ 关键字
  KEYWORD(alignas, KEYCXX)
  KEYWORD(alignof, KEYCXX)
  KEYWORD(and, KEYCXX)
  KEYWORD(and_eq, KEYCXX)
  KEYWORD(asm, KEYALL)
  KEYWORD(atomic_cancel, KEYCXX)
  KEYWORD(atomic_commit, KEYCXX)
  KEYWORD(atomic_noexcept, KEYCXX)
  KEYWORD(bitand, KEYCXX)
  KEYWORD(bitor, KEYCXX)
  KEYWORD(bool, KEYALL)
  KEYWORD(catch, KEYCXX)
  KEYWORD(char8_t, KEYCXX)
  KEYWORD(char16_t, KEYCXX)
  KEYWORD(char32_t, KEYCXX)
  KEYWORD(class, KEYCXX)
  KEYWORD(compl, KEYCXX)
  KEYWORD(concept, KEYCXX)      // C++20
  KEYWORD(const_cast, KEYCXX)
  KEYWORD(consteval, KEYCXX)    // C++20
  KEYWORD(constexpr, KEYCXX)
  KEYWORD(constinit, KEYCXX)    // C++20
  KEYWORD(co_await, KEYCXX)     // C++20
  KEYWORD(co_return, KEYCXX)    // C++20
  KEYWORD(co_yield, KEYCXX)     // C++20
  KEYWORD(decltype, KEYCXX)
  KEYWORD(delete, KEYCXX)
  KEYWORD(dynamic_cast, KEYCXX)
  KEYWORD(explicit, KEYCXX)
  KEYWORD(export, KEYCXX)
  KEYWORD(false, KEYALL)
  KEYWORD(friend, KEYCXX)
  KEYWORD(mutable, KEYCXX)
  KEYWORD(namespace, KEYCXX)
  KEYWORD(new, KEYCXX)
  KEYWORD(noexcept, KEYCXX)
  KEYWORD(not, KEYCXX)
  KEYWORD(not_eq, KEYCXX)
  KEYWORD(nullptr, KEYCXX)
  KEYWORD(operator, KEYCXX)
  KEYWORD(or, KEYCXX)
  KEYWORD(or_eq, KEYCXX)
  KEYWORD(private, KEYCXX)
  KEYWORD(protected, KEYCXX)
  KEYWORD(public, KEYCXX)
  KEYWORD(reflexpr, KEYCXX)     // C++26 反射
  KEYWORD(reinterpret_cast, KEYCXX)
  KEYWORD(requires, KEYCXX)     // C++20
  KEYWORD(static_assert, KEYCXX)
  KEYWORD(static_cast, KEYCXX)
  KEYWORD(template, KEYCXX)
  KEYWORD(this, KEYCXX)
  KEYWORD(thread_local, KEYCXX)
  KEYWORD(throw, KEYCXX)
  KEYWORD(true, KEYALL)
  KEYWORD(try, KEYCXX)
  KEYWORD(typename, KEYCXX)
  KEYWORD(using, KEYCXX)
  KEYWORD(virtual, KEYCXX)
  KEYWORD(wchar_t, KEYALL)
  KEYWORD(xor, KEYCXX)
  KEYWORD(xor_eq, KEYCXX)
  
  // C++26 新增关键字
  KEYWORD(delete, KEYCXX)       // delete("reason") 增强
  ```

**开发关键点提示：**
> 请为 nova-cc 定义完整的 Token 系统。
>
> **TokenKinds.def 格式**：
> - 使用宏定义，方便扩展
> - TOKEN(X)：普通 Token
> - KEYWORD(X, Y)：关键字，Y 是语言版本
>
> **Token 类型分类**：
> 1. 字面量：数字、字符、字符串
> 2. 标识符：变量名、函数名等
> 3. 运算符：+、-、*、/、==、&& 等
> 4. 标点：(、)、{、}、;、, 等
> 5. 关键字：int、class、template 等
> 6. 特殊：eof、unknown、code_completion
>
> **C++26 新 Token**：
> - reflexpr：反射关键字
> - .. ：占位符模式
> - delete 增强：delete("reason")
>
> **Token 类**：
> - 存储 Token 类型、位置、长度
> - 存储字面量的原始数据指针
> - 提供 is/isNot 查询方法

**Checkpoint：** TokenKinds.def 定义完整；Token 类编译通过

---

### Task 1.1.2 Token 辅助函数

**目标：** 实现 Token 类型的查询和打印辅助函数

**开发要点：**

- **E1.2.1** 创建 `include/nova-cc/Lex/TokenKinds.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/Lex/Token.h"
  
  namespace nova {
  
  /// 获取 Token 的名称字符串
  const char *getTokenName(TokenKind Kind);
  
  /// 判断是否是关键字
  bool isKeyword(TokenKind Kind);
  
  /// 判断是否是字面量
  bool isLiteral(TokenKind Kind);
  
  /// 判断是否是字符串字面量
  bool isStringLiteral(TokenKind Kind);
  
  /// 判断是否是字符字面量
  bool isCharLiteral(TokenKind Kind);
  
  /// 判断是否是数字字面量
  bool isNumericConstant(TokenKind Kind);
  
  /// 判断是否是标点符号
  bool isPunctuation(TokenKind Kind);
  
  /// 判断是否是赋值运算符
  bool isAssignmentOperator(TokenKind Kind);
  
  /// 判断是否是比较运算符
  bool isComparisonOperator(TokenKind Kind);
  
  /// 判断是否是二元运算符
  bool isBinaryOperator(TokenKind Kind);
  
  /// 判断是否是一元运算符
  bool isUnaryOperator(TokenKind Kind);
  
  } // namespace nova
  ```

- **E1.2.2** 实现 `src/Lex/TokenKinds.cpp`：
  ```cpp
  #include "nova-cc/Lex/TokenKinds.h"
  
  namespace nova {
  
  const char *getTokenName(TokenKind Kind) {
    switch (Kind) {
  #define TOKEN(X) case TokenKind::X: return #X;
  #define KEYWORD(X, Y) case TokenKind::kw_##X: return #X;
  #include "nova-cc/Lex/TokenKinds.def"
      default: return "UNKNOWN";
    }
  }
  
  bool isKeyword(TokenKind Kind) {
    return Kind >= TokenKind::kw_first_keyword && 
           Kind <= TokenKind::kw_last_keyword;
  }
  
  bool isLiteral(TokenKind Kind) {
    return isStringLiteral(Kind) || isCharLiteral(Kind) || 
           Kind == TokenKind::numeric_constant;
  }
  
  // ... 其他函数实现
  } // namespace nova
  ```

**开发关键点提示：**
> 请为 nova-cc 实现 Token 辅助函数。
>
> **getTokenName**：
> - 返回 Token 的可读名称
> - 使用 TokenKinds.def 宏简化实现
>
> **分类函数**：
> - isKeyword：判断关键字
> - isLiteral：判断字面量（数字、字符、字符串）
> - isStringLiteral：判断字符串字面量
> - isCharLiteral：判断字符字面量
> - isNumericConstant：判断数字字面量
> - isPunctuation：判断标点符号
>
> **运算符分类**：
> - isAssignmentOperator：=, +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=
> - isComparisonOperator：==, !=, <, >, <=, >=, <=>
> - isBinaryOperator：+, -, *, /, %, &, |, ^, <<, >>, &&, ||
> - isUnaryOperator：+, -, !, ~, *, &, ++, --

**Checkpoint：** 所有辅助函数实现完成；编译通过

---

### Task 1.1.3 SourceManager 集成

**目标：** 实现源代码位置管理，支持多文件和宏展开位置追踪

**开发要点：**

- **E1.3.1** 创建 `include/nova-cc/Basic/SourceManager.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/Basic/LLVM.h"
  #include "nova-cc/Basic/SourceLocation.h"
  #include <memory>
  #include <vector>
  
  namespace nova {
  
  class FileInfo {
    StringRef Filename;
    StringRef Content;
    unsigned LineCount;
  public:
    FileInfo(StringRef Name, StringRef Data);
    StringRef getFilename() const { return Filename; }
    StringRef getContent() const { return Content; }
    unsigned getLineCount() const { return LineCount; }
  };
  
  class SourceManager {
    std::vector<std::unique_ptr<FileInfo>> Files;
    std::vector<std::pair<unsigned, unsigned>> LocationTable;  // (FileID, Offset)
    
  public:
    /// 创建主文件
    SourceLocation createMainFileID(StringRef Filename, StringRef Content);
    
    /// 创建包含文件
    SourceLocation createFileID(StringRef Filename, StringRef Content);
    
    /// 获取源位置对应的文件信息
    const FileInfo* getFileInfo(SourceLocation Loc) const;
    
    /// 获取源位置的行列号
    std::pair<unsigned, unsigned> getLineAndColumn(SourceLocation Loc) const;
    
    /// 获取源位置对应的文本
    StringRef getCharacterData(SourceLocation Loc) const;
    
    /// 打印源位置（用于诊断）
    void printLocation(raw_ostream &OS, SourceLocation Loc) const;
  };
  
  } // namespace nova
  ```

- **E1.3.2** 实现 `src/Basic/SourceManager.cpp`

**开发关键点提示：**
> 请为 nova-cc 实现 SourceManager。
>
> **核心功能**：
> - 管理多个源文件
> - 将 SourceLocation 映射到文件和行列号
> - 支持宏展开位置追踪（后续 Phase 完善）
>
> **FileInfo 类**：
> - 存储文件名、内容
> - 计算行数（缓存）
>
> **SourceManager 类**：
> - createMainFileID：创建主文件 ID
> - createFileID：创建包含文件 ID
> - getFileInfo：获取文件信息
> - getLineAndColumn：获取行列号
> - getCharacterData：获取源文本
> - printLocation：格式化输出位置（如 "test.cpp:10:5"）
>
> **LocationTable 设计**：
> - 每个 SourceLocation 对应一个 (FileID, Offset) 对
> - FileID 是 Files 数组的索引
> - Offset 是文件内的字符偏移

**Checkpoint：** SourceManager 编译通过；能正确获取行列号

---

## Stage 1.2 — Lexer 核心

### Task 1.2.1 Lexer 基础架构

**目标：** 建立 Lexer 的基础框架

**开发要点：**

- **E1.2.1** 创建 `include/nova-cc/Lex/Lexer.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/Basic/LLVM.h"
  #include "nova-cc/Basic/SourceLocation.h"
  #include "nova-cc/Lex/Token.h"
  
  namespace nova {
  
  class SourceManager;
  class DiagnosticsEngine;
  
  class Lexer {
    SourceManager &SM;
    DiagnosticsEngine &Diags;
    
    // 当前位置
    const char *BufferStart;
    const char *BufferEnd;
    const char *BufferPtr;  // 当前字符位置
    
    // 当前 Token
    Token CurToken;
    
  public:
    Lexer(SourceManager &SM, DiagnosticsEngine &Diags, 
          StringRef Buffer, SourceLocation Loc);
    
    /// 词法分析下一个 Token
    bool lexToken(Token &Result);
    
    /// 获取当前 Token
    const Token& peek() const { return CurToken; }
    
    /// 获取下一个 Token（不消费）
    Token peekNext();
    
  private:
    /// 跳过空白和注释
    void skipWhitespaceAndComments();
    
    /// 识别并创建 Token
    bool formToken(Token &Result, TokenKind Kind);
    
    /// 读取字符
    char peekChar(unsigned Offset = 0) const;
    char getChar();
    void consumeChar();
  };
  
  } // namespace nova
  ```

- **E1.2.2** 实现 `src/Lex/Lexer.cpp` 基础框架：
  ```cpp
  #include "nova-cc/Lex/Lexer.h"
  #include "nova-cc/Basic/SourceManager.h"
  #include "nova-cc/Basic/Diagnostics.h"
  
  namespace nova {
  
  Lexer::Lexer(SourceManager &SM, DiagnosticsEngine &Diags,
               StringRef Buffer, SourceLocation Loc)
    : SM(SM), Diags(Diags),
      BufferStart(Buffer.begin()), BufferEnd(Buffer.end()),
      BufferPtr(BufferStart) {
    CurToken.setKind(TokenKind::unknown);
  }
  
  bool Lexer::lexToken(Token &Result) {
    // 跳过空白和注释
    skipWhitespaceAndComments();
    
    // 检查 EOF
    if (BufferPtr >= BufferEnd) {
      Result.setKind(TokenKind::eof);
      return false;
    }
    
    // 根据首字符分发
    char C = *BufferPtr;
    switch (C) {
      // 标识符或关键字
      case 'a': case 'b': case 'c': case 'd': case 'e':
      case 'f': case 'g': case 'h': case 'i': case 'j':
      case 'k': case 'l': case 'm': case 'n': case 'o':
      case 'p': case 'q': case 'r': case 's': case 't':
      case 'u': case 'v': case 'w': case 'x': case 'y':
      case 'z':
      case 'A': case 'B': case 'C': case 'D': case 'E':
      case 'F': case 'G': case 'H': case 'I': case 'J':
      case 'K': case 'L': case 'M': case 'N': case 'O':
      case 'P': case 'Q': case 'R': case 'S': case 'T':
      case 'U': case 'V': case 'W': case 'X': case 'Y':
      case 'Z':
      case '_':
        return lexIdentifier(Result);
        
      // 数字
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9':
        return lexNumericConstant(Result);
        
      // 字符和字符串
      case '"': return lexStringLiteral(Result);
      case '\'': return lexCharConstant(Result);
      case 'L': case 'u': case 'U': case 'R':
        // 可能是宽字符/字符串/原始字符串
        return lexWideOrRawLiteral(Result);
        
      // 标点符号
      // ... 大量 case
      
      default:
        // 未知字符
        Diags.report(/*...*/);
        Result.setKind(TokenKind::unknown);
        return true;
    }
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现 Lexer 基础架构。
>
> **Lexer 类设计**：
> - 持有 SourceManager 和 DiagnosticsEngine 引用
> - 维护缓冲区指针（BufferStart, BufferEnd, BufferPtr）
> - 支持向前看（peekNext）
>
> **核心方法**：
> - lexToken：词法分析一个 Token
> - skipWhitespaceAndComments：跳过空白和注释
> - formToken：创建 Token
> - peekChar/getChar/consumeChar：字符操作
>
> **分发逻辑**：
> - 根据首字符分发到不同的 lex 函数
> - 标识符：字母或下划线开头
> - 数字：0-9 开头
> - 字符串：" 开头
> - 字符：' 开头
> - 标点符号：各种符号
>
> **错误处理**：
> - 遇到未知字符时报告错误
> - 创建 unknown Token 继续分析

**Checkpoint：** Lexer 基础框架编译通过；能词法分析简单代码

---

### Task 1.2.2 标识符和关键字识别

**目标：** 实现标识符和关键字的词法分析

**开发要点：**

- **E1.2.2** 实现标识符词法分析：
  ```cpp
  bool Lexer::lexIdentifier(Token &Result) {
    const char *Start = BufferPtr;
    
    // 读取标识符字符：字母、数字、下划线
    while (BufferPtr < BufferEnd) {
      char C = *BufferPtr;
      if (isalnum(C) || C == '_') {
        ++BufferPtr;
      } else {
        break;
      }
    }
    
    StringRef Text(Start, BufferPtr - Start);
    
    // 检查是否是关键字
    TokenKind Kind = getKeywordKind(Text);
    
    Result.setKind(Kind);
    Result.setLocation(SourceLocation::getFromPointer(Start));
    Result.setLength(BufferPtr - Start);
    Result.setLiteralData(Start);
    
    return true;
  }
  ```

- **E1.2.3** 实现关键字查找表：
  ```cpp
  // src/Lex/KeywordLookup.cpp
  
  struct KeywordInfo {
    const char *Name;
    TokenKind Kind;
    unsigned Flags;  // C/C++ 版本标志
  };
  
  static const KeywordInfo Keywords[] = {
    {"int", TokenKind::kw_int, KEYALL},
    {"class", TokenKind::kw_class, KEYCXX},
    {"template", TokenKind::kw_template, KEYCXX},
    {"concept", TokenKind::kw_concept, KEYCXX},   // C++20
    {"co_await", TokenKind::kw_co_await, KEYCXX}, // C++20
    {"reflexpr", TokenKind::kw_reflexpr, KEYCXX}, // C++26
    // ...
  };
  
  TokenKind getKeywordKind(StringRef Text) {
    // 使用 hash 表或完美 hash 查找
    // ...
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现标识符和关键字识别。
>
> **标识符规则**：
> - 首字符：字母（a-z, A-Z）或下划线
> - 后续字符：字母、数字、下划线
> - 示例：foo, _bar, CamelCase, snake_case
>
> **关键字查找**：
> - 使用 hash 表快速查找
> - 可考虑使用完美 hash（gperf）
> - 支持不同语言版本（C/C++20/C++26）
>
> **关键字列表**：
> - C 关键字：int, char, void, if, else, for, while, ...
> - C++ 关键字：class, template, typename, namespace, ...
> - C++11：nullptr, constexpr, noexcept, ...
> - C++20：concept, requires, co_await, co_yield, co_return, consteval, constinit
> - C++26：reflexpr, delete 增强
>
> **编码处理**：
> - 支持标识符中的 Unicode 字符（C++11 通用字符名）
> - \uXXXX 和 \UXXXXXXXX 格式

**Checkpoint：** 能正确识别所有 C++26 关键字

---

### Task 1.2.3 数字字面量解析

**目标：** 实现数字字面量的词法分析

**开发要点：**

- **E1.2.4** 实现数字字面量词法分析：
  ```cpp
  bool Lexer::lexNumericConstant(Token &Result) {
    const char *Start = BufferPtr;
    
    // 确定进制
    if (*BufferPtr == '0') {
      if (BufferPtr + 1 < BufferEnd) {
        char Next = tolower(*(BufferPtr + 1));
        if (Next == 'x' || Next == 'X') {
          // 十六进制: 0x1a2b
          return lexHexConstant(Result, Start);
        } else if (Next == 'b' || Next == 'B') {
          // 二进制: 0b1010 (C++14)
          return lexBinaryConstant(Result, Start);
        }
        // 可能是八进制: 0123
      }
    }
    
    // 十进制: 123, 123.456, 123e10, 123p10
    return lexDecimalConstant(Result, Start);
  }
  
  bool Lexer::lexDecimalConstant(Token &Result, const char *Start) {
    // 整数部分
    while (BufferPtr < BufferEnd && isdigit(*BufferPtr)) {
      ++BufferPtr;
    }
    
    // 小数部分
    if (BufferPtr < BufferEnd && *BufferPtr == '.') {
      ++BufferPtr;
      while (BufferPtr < BufferEnd && isdigit(*BufferPtr)) {
        ++BufferPtr;
      }
    }
    
    // 指数部分
    if (BufferPtr < BufferEnd && (tolower(*BufferPtr) == 'e' || tolower(*BufferPtr) == 'p')) {
      ++BufferPtr;
      if (BufferPtr < BufferEnd && (*BufferPtr == '+' || *BufferPtr == '-')) {
        ++BufferPtr;
      }
      while (BufferPtr < BufferEnd && isdigit(*BufferPtr)) {
        ++BufferPtr;
      }
    }
    
    // 数字分隔符 (C++14): 1'000'000
    // 需要在循环中处理
    
    // 后缀
    while (BufferPtr < BufferEnd && isalnum(*BufferPtr)) {
      ++BufferPtr;
    }
    
    // 创建 Token
    Result.setKind(TokenKind::numeric_constant);
    Result.setLocation(SourceLocation::getFromPointer(Start));
    Result.setLength(BufferPtr - Start);
    Result.setLiteralData(Start);
    
    return true;
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现数字字面量解析。
>
> **整数格式**：
> - 十进制：123, 123'456（C++14 分隔符）
> - 十六进制：0x1a2b, 0X1A2B
> - 八进制：0123, 0o123（C++？）
> - 二进制：0b1010, 0B1010（C++14）
>
> **浮点格式**：
> - 小数形式：123.456, .456, 123.
> - 指数形式：123e10, 123E-10, 123.456e+10
> - 十六进制浮点：0x1.8p10（C++17）
>
> **数字分隔符**：
> - C++14 允许使用单引号分隔：1'000'000
> - 不能出现在开头、结尾、基数前缀后
>
> **后缀**：
> - 整数：u, U, l, L, ll, LL, z, Z（C++23）
> - 浮点：f, F, l, L, f16, f32, f64, f128（C++23）
> - 用户定义字面量：123_i, 3.14_m
>
> **错误处理**：
> - 无效数字：0b123
> - 无效分隔符：'123, 123'
> - 溢出警告

**Checkpoint：** 所有数字格式正确解析

---

### Task 1.2.4 字符和字符串字面量解析

**目标：** 实现字符和字符串字面量的词法分析

**开发要点：**

- **E1.2.5** 实现字符字面量解析：
  ```cpp
  bool Lexer::lexCharConstant(Token &Result) {
    const char *Start = BufferPtr;
    assert(*BufferPtr == '\'');
    ++BufferPtr;  // 跳过开头的 '
    
    // 读取字符内容
    while (BufferPtr < BufferEnd && *BufferPtr != '\'') {
      if (*BufferPtr == '\\') {
        // 转义序列
        ++BufferPtr;
        if (BufferPtr < BufferEnd) {
          // 处理转义: \n, \t, \r, \\, \', \", \xHH, \uXXXX, \UXXXXXXXX
          ++BufferPtr;
        }
      } else if (*BufferPtr == '\n') {
        // 错误：未终止的字符字面量
        Diags.report(/*...*/);
        break;
      } else {
        ++BufferPtr;
      }
    }
    
    if (BufferPtr >= BufferEnd || *BufferPtr != '\'') {
      // 错误：未终止
      Diags.report(/*...*/);
    } else {
      ++BufferPtr;  // 跳过结尾的 '
    }
    
    Result.setKind(TokenKind::char_constant);
    Result.setLocation(SourceLocation::getFromPointer(Start));
    Result.setLength(BufferPtr - Start);
    Result.setLiteralData(Start);
    
    return true;
  }
  ```

- **E1.2.6** 实现字符串字面量解析：
  ```cpp
  bool Lexer::lexStringLiteral(Token &Result) {
    const char *Start = BufferPtr;
    assert(*BufferPtr == '"');
    ++BufferPtr;
    
    while (BufferPtr < BufferEnd && *BufferPtr != '"') {
      if (*BufferPtr == '\\') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd) ++BufferPtr;
      } else if (*BufferPtr == '\n') {
        // 错误或行拼接（取决于是否在原始字符串中）
        break;
      } else {
        ++BufferPtr;
      }
    }
    
    if (BufferPtr < BufferEnd && *BufferPtr == '"') {
      ++BufferPtr;
    } else {
      Diags.report(/*...*/);
    }
    
    Result.setKind(TokenKind::string_literal);
    Result.setLocation(SourceLocation::getFromPointer(Start));
    Result.setLength(BufferPtr - Start);
    Result.setLiteralData(Start);
    
    return true;
  }
  ```

- **E1.2.7** 实现原始字符串字面量（C++11）：
  ```cpp
  bool Lexer::lexRawStringLiteral(Token &Result) {
    // R"delim(content)delim"
    // 示例：R"(hello\nworld)"  -> hello\nworld (不转义)
    // 示例：R"foo((()))foo"   -> ((()))
    // ...
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现字符和字符串字面量解析。
>
> **字符字面量**：
> - 普通：'a', 'Z', '0'
> - 转义：'\n', '\t', '\r', '\\', '\'', '\"'
> - 数字转义：'\xHH'（十六进制）, '\OOO'（八进制）
> - Unicode：'\uXXXX'（C++11）, '\UXXXXXXXX'（C++11）
> - 宽字符：L'a'
> - UTF-8：u8'a'（C++20）
> - UTF-16：u'a'（C++11）
> - UTF-32：U'a'（C++11）
>
> **字符串字面量**：
> - 普通："hello"
> - 宽字符串：L"hello"
> - UTF-8：u8"hello"（C++11）
> - UTF-16：u"hello"（C++11）
> - UTF-32：U"hello"（C++11）
> - 原始字符串：R"(hello\nworld)"（C++11）
> - 原始字符串带分隔符：R"delim(content)delim"
>
> **转义序列处理**：
> - 简单转义：\n, \t, \r, \\, \', \"
> - 数字转义：\xHH, \OOO
> - Unicode 转义：\uXXXX, \UXXXXXXXX
> - 条件转义：\e（C++23）
>
> **错误处理**：
> - 未终止的字面量
> - 无效的转义序列
> - 多字符字符字面量警告

**Checkpoint：** 所有字符和字符串格式正确解析

---

### Task 1.2.5 运算符和标点符号解析

**目标：** 实现运算符和标点符号的词法分析

**开发要点：**

- **E1.2.8** 实现标点符号最长匹配：
  ```cpp
  bool Lexer::lexPunctuation(Token &Result) {
    const char *Start = BufferPtr;
    char C = *BufferPtr;
    
    switch (C) {
      case '+':
        ++BufferPtr;
        if (BufferPtr < BufferEnd) {
          if (*BufferPtr == '+') { ++BufferPtr; return formToken(Result, TokenKind::plusplus); }
          if (*BufferPtr == '=') { ++BufferPtr; return formToken(Result, TokenKind::plusequal); }
        }
        return formToken(Result, TokenKind::plus);
        
      case '-':
        ++BufferPtr;
        if (BufferPtr < BufferEnd) {
          if (*BufferPtr == '-') { ++BufferPtr; return formToken(Result, TokenKind::minusminus); }
          if (*BufferPtr == '=') { ++BufferPtr; return formToken(Result, TokenKind::minusequal); }
          if (*BufferPtr == '>') {
            ++BufferPtr;
            if (BufferPtr < BufferEnd && *BufferPtr == '*') {
              ++BufferPtr;
              return formToken(Result, TokenKind::arrowstar);
            }
            return formToken(Result, TokenKind::arrow);
          }
        }
        return formToken(Result, TokenKind::minus);
        
      case '<':
        ++BufferPtr;
        if (BufferPtr < BufferEnd) {
          if (*BufferPtr == '=') { ++BufferPtr; return formToken(Result, TokenKind::lessequal); }
          if (*BufferPtr == '<') {
            ++BufferPtr;
            if (BufferPtr < BufferEnd && *BufferPtr == '=') {
              ++BufferPtr;
              return formToken(Result, TokenKind::lesslessequal);
            }
            return formToken(Result, TokenKind::lessless);
          }
          // C++20: <=>
          if (*BufferPtr == '=') {
            ++BufferPtr;
            if (BufferPtr < BufferEnd && *BufferPtr == '>') {
              ++BufferPtr;
              return formToken(Result, TokenKind::spaceship);
            }
            return formToken(Result, TokenKind::lessequal);
          }
        }
        return formToken(Result, TokenKind::less);
        
      // ... 其他情况
    }
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现运算符和标点符号解析。
>
> **最长匹配原则**：
> - 读入尽可能长的合法 Token
> - 例：+++ → ++ + (不是 + ++)
> - 例：->* → ->* (不是 -> *)
>
> **运算符分类**：
> - 单字符：+ - * / % ^ & | ~ ! = < > . ? , ; : ( ) [ ] { }
> - 双字符：++ -- += -= *= /= %= ^= &= |= == != <= >= << >> && || :: -> .* ->*
> - 三字符：<<= >>= ... <=> (C++20)
> - 四字符：可能无
>
> **C++20 新增**：
> - <=> (spaceship operator)
>
> **C++26 新增**：
> - .. (placeholder pattern) - 可能作为单独 Token
>
> **Digraphs 和 Trigraphs**：
> - Digraphs（C++）： <% { %> } <: [ :> ] %: # %:%: ##
> - Trigraphs（C++17 移除）： ??< { ??> } 等

**Checkpoint：** 所有运算符和标点符号正确解析

---

### Task 1.2.6 注释处理

**目标：** 实现注释的识别和跳过

**开发要点：**

- **E1.2.9** 实现注释处理：
  ```cpp
  void Lexer::skipWhitespaceAndComments() {
    while (BufferPtr < BufferEnd) {
      char C = *BufferPtr;
      
      // 空白字符
      if (isspace(C)) {
        ++BufferPtr;
        continue;
      }
      
      // 注释
      if (C == '/') {
        if (BufferPtr + 1 < BufferEnd) {
          char Next = *(BufferPtr + 1);
          
          // 行注释 //
          if (Next == '/') {
            BufferPtr += 2;
            while (BufferPtr < BufferEnd && *BufferPtr != '\n') {
              ++BufferPtr;
            }
            continue;
          }
          
          // 块注释 /* ... */
          if (Next == '*') {
            BufferPtr += 2;
            while (BufferPtr + 1 < BufferEnd) {
              if (*BufferPtr == '*' && *(BufferPtr + 1) == '/') {
                BufferPtr += 2;
                break;
              }
              ++BufferPtr;
            }
            // 未终止的块注释错误
            if (BufferPtr + 1 >= BufferEnd) {
              Diags.report(/*...*/);
            }
            continue;
          }
        }
      }
      
      // 不是空白或注释，结束
      break;
    }
  }
  ```

- **E1.2.10** 保留注释（可选）：
  ```cpp
  // 某些场景需要保留注释（如文档生成）
  bool Lexer::lexComment(Token &Result) {
    // 将注释作为 Token 返回
    Result.setKind(TokenKind::comment);
    // ...
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现注释处理。
>
> **注释类型**：
> - 行注释：// 直到行尾
> - 块注释：/* ... */
>
> **处理策略**：
> - 默认跳过注释
> - 可选保留注释（用于文档生成、lint 等）
>
> **错误处理**：
> - 未终止的块注释：/* ...（文件结束）
>
> **换行处理**：
> - 行注释遇到 \n 结束
> - 行注释可能在反斜杠续行后继续
>
> **嵌套注释**：
> - 标准 C++ 不支持嵌套
> - 可作为扩展选项支持

**Checkpoint：** 注释正确跳过；错误正确报告

---

## Stage 1.3 — Preprocessor

### Task 1.3.1 Preprocessor 基础架构

**目标：** 建立预处理器的基础框架

**开发要点：**

- **E1.3.1** 创建 `include/nova-cc/Lex/Preprocessor.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/Basic/LLVM.h"
  #include "nova-cc/Lex/Token.h"
  #include <memory>
  #include <vector>
  
  namespace nova {
  
  class Lexer;
  class SourceManager;
  class DiagnosticsEngine;
  class HeaderSearch;
  class MacroInfo;
  
  class Preprocessor {
    SourceManager &SM;
    DiagnosticsEngine &Diags;
    HeaderSearch &Headers;
    
    // 包含栈（用于处理 #include）
    std::vector<std::unique_ptr<Lexer>> IncludeStack;
    Lexer *CurLexer;
    
    // 宏定义表
    std::map<IdentifierInfo*, MacroInfo*> Macros;
    
    // 预定义宏
    void initializePredefinedMacros();
    
  public:
    Preprocessor(SourceManager &SM, DiagnosticsEngine &Diags, HeaderSearch &Headers);
    
    /// 预处理下一个 Token
    bool lexToken(Token &Result);
    
    /// 处理预处理指令
    void handleDirective(Token &Directive);
    
    /// 定义宏
    void defineMacro(StringRef Name, StringRef Body);
    
    /// 取消宏定义
    void undefMacro(StringRef Name);
    
    /// 判断宏是否定义
    bool isMacroDefined(StringRef Name) const;
    
    /// 展开宏
    bool expandMacro(Token &Result, IdentifierInfo *II);
  };
  
  } // namespace nova
  ```

- **E1.3.2** 实现 `src/Lex/Preprocessor.cpp` 基础框架

**开发关键点提示：**
> 请为 nova-cc 实现预处理器基础架构。
>
> **核心功能**：
> - 从 Lexer 获取 Token
> - 识别和处理预处理指令
> - 宏定义和展开
> - 文件包含
>
> **包含栈**：
> - 每遇到 #include 压入新 Lexer
> - 文件结束时弹出
> - 支持递归包含检测
>
> **宏表**：
> - 使用 hash 表存储宏定义
> - 支持 #define、#undef
> - 支持预定义宏（__cplusplus, __FILE__, __LINE__ 等）
>
> **预处理 Token 流**：
> - 输出预处理后的 Token 流
> - 隐藏预处理细节

**Checkpoint：** Preprocessor 基础框架编译通过

---

### Task 1.3.2 预处理指令处理

**目标：** 实现各种预处理指令的处理

**开发要点：**

- **E1.3.3** 实现预处理指令识别：
  ```cpp
  void Preprocessor::handleDirective(Token &Directive) {
    StringRef Name = Directive.getIdentifierInfo()->getName();
    
    if (Name == "include" || Name == "include_next") {
      handleIncludeDirective(Directive);
    } else if (Name == "define") {
      handleDefineDirective(Directive);
    } else if (Name == "undef") {
      handleUndefDirective(Directive);
    } else if (Name == "if") {
      handleIfDirective(Directive);
    } else if (Name == "ifdef") {
      handleIfdefDirective(Directive);
    } else if (Name == "ifndef") {
      handleIfndefDirective(Directive);
    } else if (Name == "else") {
      handleElseDirective(Directive);
    } else if (Name == "elif") {
      handleElifDirective(Directive);
    } else if (Name == "endif") {
      handleEndifDirective(Directive);
    } else if (Name == "pragma") {
      handlePragmaDirective(Directive);
    } else if (Name == "line") {
      handleLineDirective(Directive);
    } else if (Name == "error") {
      handleErrorDirective(Directive);
    } else if (Name == "warning") {
      handleWarningDirective(Directive);
    } else {
      // 未知指令
      Diags.report(/*...*/);
    }
  }
  ```

- **E1.3.4** 实现 #define 和 #undef

**开发关键点提示：**
> 请为 nova-cc 实现预处理指令处理。
>
> **预处理指令列表**：
> - #include：文件包含
> - #define：宏定义
> - #undef：取消宏定义
> - #if, #ifdef, #ifndef：条件编译开始
> - #elif：条件分支
> - #else：条件分支
> - #endif：条件编译结束
> - #pragma：编译器指示
> - #line：行号控制
> - #error：错误报告
> - #warning：警告报告
> - #embed：二进制嵌入（C++26）
>
> **条件编译栈**：
> - 维护嵌套的条件编译状态
> - 处理 #if、#elif、#else、#endif 匹配
> - 支持表达式求值
>
> **#pragma 处理**：
> - 解析 pragma 参数
> - 支持 once、push/pop macro 等常用 pragma

**Checkpoint：** 基本预处理指令正确处理

---

### Task 1.3.3 宏展开

**目标：** 实现完整的宏展开机制

**开发要点：**

- **E1.3.5** 定义宏信息结构：
  ```cpp
  class MacroInfo {
    SourceLocation DefinitionLocation;
    std::vector<Token> ReplacementTokens;
    std::vector<IdentifierInfo*> ParameterList;  // 函数宏参数
    bool IsFunctionLike : 1;
    bool IsVariadic : 1;  // 可变参数宏
    bool IsPredefined : 1;
    
  public:
    // ...
  };
  ```

- **E1.3.6** 实现对象宏展开：
  ```cpp
  bool Preprocessor::expandObjectMacro(Token &Result, MacroInfo *MI) {
    // 直接替换为替换列表
    // ...
  }
  ```

- **E1.3.7** 实现函数宏展开：
  ```cpp
  bool Preprocessor::expandFunctionMacro(Token &Result, MacroInfo *MI) {
    // 1. 解析参数列表
    // 2. 替换参数
    // 3. 字符串化 # 和连接 ##
    // ...
  }
  ```

- **E1.3.8** 实现特殊操作符：
  ```cpp
  // # 参数 → 字符串化
  // ## 连接两个 Token
  // __VA_ARGS__ 可变参数
  // __VA_OPT__ 可变参数优化（C++20）
  ```

**开发关键点提示：**
> 请为 nova-cc 实现宏展开。
>
> **对象宏**：
> - 简单替换：#define PI 3.14
> - 多 Token：#define FOREVER for(;;)
>
> **函数宏**：
> - 带参数：#define MAX(a, b) ((a) > (b) ? (a) : (b))
> - 可变参数：#define LOG(...) printf(__VA_ARGS__)
> - C++20 __VA_OPT__：#define F(...) f(0 __VA_OPT__(,) __VA_ARGS__)
>
> **特殊操作符**：
> - #：字符串化，#param → "param_value"
> - ##：Token 连接，a ## b → ab
> - __VA_ARGS__：可变参数占位符
> - __VA_OPT__：条件展开
>
> **递归展开**：
> - 防止无限递归
> - 蓝色标记法或类似算法
>
> **预定义宏**：
> - __cplusplus：C++ 版本
> - __FILE__：当前文件名
> - __LINE__：当前行号
> - __DATE__, __TIME__：编译日期时间
> - __STDC_HOSTED__：是否托管环境
> - __has_include：检测头文件是否存在

**Checkpoint：** 宏展开正确；支持可变参数和特殊操作符

---

### Task 1.3.4 头文件搜索

**目标：** 实现 #include 的头文件搜索机制

**开发要点：**

- **E1.3.9** 创建 `include/nova-cc/Lex/HeaderSearch.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/Basic/LLVM.h"
  #include <vector>
  #include <string>
  
  namespace nova {
  
  class FileManager;
  class FileEntry;
  
  class HeaderSearch {
    FileManager &FileMgr;
    
    // 搜索路径
    std::vector<std::string> SearchPaths;
    
    // 已包含的文件（用于 #pragma once）
    std::set<const FileEntry*> IncludedFiles;
    
  public:
    HeaderSearch(FileManager &FM);
    
    /// 添加搜索路径
    void addSearchPath(StringRef Path, bool IsSystem = false);
    
    /// 查找头文件
    const FileEntry* lookupHeader(StringRef Filename, bool IsAngled);
    
    /// 检查是否已包含
    bool isIncluded(const FileEntry *FE) const;
    
    /// 标记为已包含
    void markIncluded(const FileEntry *FE);
  };
  
  } // namespace nova
  ```

- **E1.3.10** 实现 `src/Lex/HeaderSearch.cpp`

**开发关键点提示：**
> 请为 nova-cc 实现头文件搜索。
>
> **Include 语法**：
> - #include <header>：系统头文件，尖括号搜索
> - #include "header"：用户头文件，先当前目录，后系统路径
>
> **搜索顺序**：
> 1. #include "..." 形式：
>    - 当前文件所在目录
>    - -I 指定的路径
>    - 系统路径
> 2. #include <...> 形式：
>    - -I 指定的路径
>    - 系统路径
>
> **特殊处理**：
> - #pragma once：防止重复包含
> - Include Guard：#ifndef/#define/#endif
> - 相对路径：#include "subdir/header.h"
>
> **错误处理**：
> - 头文件未找到
> - 循环包含检测

**Checkpoint：** 头文件搜索正确；支持系统路径和用户路径

---

### Task 1.3.5 C++26 预处理新特性

**目标：** 实现 C++26 新增的预处理特性

**开发要点：**

- **E1.3.11** 实现 #embed（P1967）：
  ```cpp
  // C++26: #embed <file>
  // 将二进制文件内容嵌入源代码
  
  bool Preprocessor::handleEmbedDirective(Token &Directive) {
    // 1. 解析文件名
    // 2. 读取文件内容
    // 3. 生成数字序列 Token
    // 例如：#embed "data.bin" → 0x12, 0x34, 0x56, ...
  }
  ```

- **E1.3.12** 实现新预定义宏：
  ```cpp
  // C++26 新增预定义宏
  // __cpp_static_assert = 202306L
  // __cpp_reflexpr = 202502L (反射)
  // __cpp_contracts = 202502L (合约)
  ```

**开发关键点提示：**
> 请为 nova-cc 实现 C++26 预处理新特性。
>
> **#embed 指令（P1967）**：
> - 语法：#embed <filename> [limit(n)] [suffix(s)]
> - 功能：将二进制文件嵌入源代码
> - 示例：
>   ```cpp
>   const unsigned char data[] = {
>     #embed "image.png"
>   };
>   ```
> - 参数：
>   - limit(n)：最多读取 n 字节
>   - suffix(s)：最后一个元素后加后缀
>
> **新预定义宏**：
> - __cpp_static_assert：202306L
> - __cpp_reflexpr：202502L
> - __cpp_contracts：202502L
> - __cpp_pack_indexing：202411L
>
> **__has_include 增强**：
> - 支持 #embed 检测

**Checkpoint：** #embed 正确实现；新预定义宏正确定义

---

## Stage 1.4 — C++26 特性 + 测试

### Task 1.4.1 C++26 新 Token 支持

**目标：** 支持 C++26 新增的 Token 和语法

**开发要点：**

- **E1.4.1** 添加 reflexpr 关键字
- **E1.4.2** 添加占位符 Token（如 ..）
- **E1.4.3** 支持合约属性（[[pre:]], [[post:]], [[assert:]]）

**开发关键点提示：**
> 请为 nova-cc 添加 C++26 新 Token 支持。
>
> **reflexpr 关键字**：
> - 用于静态反射
> - 语法：reflexpr(type) 或 reflexpr(expression)
>
> **占位符模式**：
> - .. 作为占位符（具体语法待标准确定）
>
> **合约属性**：
> - [[pre: condition]]：前置条件
> - [[post: condition]]：后置条件
> - [[assert: condition]]：断言
>
> **delete 增强**：
> - delete("reason")：带原因的删除

**Checkpoint：** C++26 新 Token 正确识别

---

### Task 1.4.2 词法分析器测试

**目标：** 建立 Lexer 的完整测试覆盖

**开发要点：**

- **E1.4.1** 创建测试目录：
  ```
  tests/unit/Lex/
  ├── LexerTest.cpp
  ├── TokenTest.cpp
  └── PreprocessorTest.cpp
  ```

- **E1.4.2** 编写 Token 测试：
  ```cpp
  TEST(LexerTest, Identifiers) {
    EXPECT_TRUE(lex("foo", result));
    EXPECT_EQ(result.getKind(), TokenKind::identifier);
    EXPECT_EQ(result.getLiteralData(), "foo");
  }
  
  TEST(LexerTest, Keywords) {
    EXPECT_TRUE(lex("int", result));
    EXPECT_EQ(result.getKind(), TokenKind::kw_int);
  }
  
  TEST(LexerTest, Numbers) {
    EXPECT_TRUE(lex("123", result));
    EXPECT_EQ(result.getKind(), TokenKind::numeric_constant);
    
    EXPECT_TRUE(lex("0x1a2b", result));
    EXPECT_EQ(result.getLiteralData(), "0x1a2b");
  }
  ```

- **E1.4.3** 编写预处理器测试：
  ```cpp
  TEST(PreprocessorTest, MacroExpansion) {
    defineMacro("PI", "3.14");
    EXPECT_TRUE(lex("PI", result));
    EXPECT_EQ(result.getKind(), TokenKind::numeric_constant);
    EXPECT_EQ(result.getLiteralData(), "3.14");
  }
  
  TEST(PreprocessorTest, Include) {
    // 测试头文件包含
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 建立 Lexer 和 Preprocessor 测试。
>
> **测试覆盖**：
> - 所有 Token 类型
> - 所有关键字
> - 所有数字格式
> - 所有字符和字符串格式
> - 所有运算符
> - 注释处理
> - 预处理指令
> - 宏展开
> - 文件包含
> - C++26 新特性
>
> **错误测试**：
> - 无效字符
> - 未终止的字面量
> - 无效的数字
> - 未定义的宏
> - 未找到的头文件

**Checkpoint：** 测试覆盖率 ≥ 80%

---

### Task 1.4.3 Lit 回归测试

**目标：** 建立 Lexer 的 Lit 回归测试

**开发要点：**

- **E1.4.1** 创建测试文件：
  ```lit
  // tests/lit/lex/identifiers.test
  // RUN: %nova-cc -dump-tokens %s | FileCheck %s
  
  int main() {}
  // CHECK: int 'int'
  // CHECK: identifier 'main'
  // CHECK: l_paren '('
  // CHECK: r_paren ')'
  // CHECK: l_brace '{'
  // CHECK: r_brace '}'
  ```

- **E1.4.2** 创建预处理器测试：
  ```lit
  // tests/lit/preprocess/macro.test
  // RUN: %nova-cc -E %s | FileCheck %s
  
  #define PI 3.14
  double x = PI;
  // CHECK: double x = 3.14;
  ```

**开发关键点提示：**
> 请为 nova-cc 建立 Lexer Lit 测试。
>
> **测试类别**：
> - 词法分析测试：lex/*.test
> - 预处理测试：preprocess/*.test
>
> **测试格式**：
> - 使用 RUN 行指定命令
> - 使用 FileCheck 验证输出
> - 使用 CHECK, CHECK-NEXT, CHECK-NOT 等指令

**Checkpoint：** Lit 测试套件完整

---

### Task 1.4.4 性能优化

**目标：** 优化 Lexer 和 Preprocessor 性能

**开发要点：**

- **E1.4.1** 实现 Token 缓冲区：
  ```cpp
  class TokenBuffer {
    std::vector<Token> Tokens;
    unsigned CurIndex = 0;
  public:
    void pushToken(Token T);
    const Token& peek(unsigned Offset = 0) const;
    Token consume();
    bool empty() const;
  };
  ```

- **E1.4.2** 实现关键字查找优化：
  - 使用完美 hash（gperf）
  - 或使用 Trie 树

- **E1.4.3** 实现宏展开缓存

**开发关键点提示：**
> 请为 nova-cc 优化 Lexer 性能。
>
> **优化点**：
> - Token 缓冲区：预读多个 Token，减少函数调用
> - 关键字查找：使用完美 hash 或 Trie
> - 宏展开缓存：缓存展开结果
> - 内存分配：使用 Arena 分配器
>
> **性能目标**：
> - 词法分析速度接近 Clang
> - 支持大文件（> 1MB）
> - 支持深度包含（> 100 层）

**Checkpoint：** 性能基准建立；满足目标

---

## 📋 Phase 1 验收检查清单

```
[ ] TokenKinds.def 定义完整
[ ] Token 类实现完成
[ ] Token 辅助函数实现完成
[ ] SourceManager 实现完成
[ ] Lexer 基础框架实现完成
[ ] 标识符和关键字识别正确
[ ] 数字字面量解析正确
[ ] 字符和字符串字面量解析正确
[ ] 运算符和标点符号解析正确
[ ] 注释处理正确
[ ] Preprocessor 基础框架实现完成
[ ] 预处理指令处理正确
[ ] 宏展开正确（包括可变参数、#、##）
[ ] 头文件搜索正确
[ ] C++26 #embed 实现完成
[ ] C++26 新预定义宏定义完成
[ ] C++26 新 Token 支持完成
[ ] 单元测试覆盖率 ≥ 80%
[ ] Lit 回归测试通过
[ ] 性能基准建立
```

---

*Phase 1 完成标志：能正确词法分析和预处理 C++26 源代码，生成正确的 Token 流，测试通过，覆盖率 ≥ 80%。*
