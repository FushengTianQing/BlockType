# Phase 2：语法分析器（表达式与语句）
> **目标：** 完成表达式解析和语句解析，构建完整的表达式 AST 和语句 AST
> **前置依赖：** Phase 1 完成（Lexer + Preprocessor）
> **验收标准：** 能够正确解析 C++26 标准中的所有表达式和语句类型，生成结构正确的 AST；支持 C++26 新增的运算符和表达式语法

---

## 📌 阶段总览

```
Phase 2 包含 4 个 Stage，共 12 个 Task，预计 6 周完成。
建议并行度：Stage 2.1 和 2.2 可并行开始，Stage 2.3 需等 2.1 完成，Stage 2.4 需等 2.3 完成。
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 2.1** | AST 节点定义 | 所有表达式/语句 AST 节点 | 1.5 周 |
| **Stage 2.2** | 表达式解析 | 表达式 Parser、运算符优先级 | 2 周 |
| **Stage 2.3** | 语句解析 | 语句 Parser、控制流 | 1.5 周 |
| **Stage 2.4** | C++26 特性 + 集成测试 | C++26 新语法、完整测试 | 1 周 |

**Phase 2 架构图：**

```
Token 流 (from Preprocessor)
    │
    ▼
┌─────────────────────────────────────────────────────────┐
│                      Parser                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │ Expression  │  │  Statement  │  │   Declaration   │  │
│  │   Parser    │  │   Parser    │  │    Parser       │  │
│  └──────┬──────┘  └──────┬──────┘  └────────┬────────┘  │
│         │                │                   │          │
│         ▼                ▼                   ▼          │
│  ┌──────────────────────────────────────────────────┐  │
│  │                    AST Nodes                     │  │
│  │  Expr/Stmt/Decl 层次结构                         │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
    │
    ▼
AST Root (TranslationUnitDecl)
```

---

## Stage 2.1 — AST 节点定义

### Task 2.1.1 AST 基础架构

**目标：** 建立 AST 节点的基类体系和访问者模式

**开发要点：**

- **E1.1.1** 创建 `AST/ASTNode.h`，定义 AST 节点基类：
  ```cpp
  class ASTNode {
    SourceLocation Loc;
  public:
    virtual ~ASTNode() = default;
    virtual void dump(raw_ostream &OS, unsigned Indent = 0) const = 0;
    SourceLocation getLocation() const { return Loc; }
  };
  ```

- **E1.1.2** 创建 `AST/ASTContext.h/cpp`，管理 AST 节点的内存分配：
  ```cpp
  class ASTContext {
    std::vector<std::unique_ptr<ASTNode>> Nodes;
    llvm::BumpPtrAllocator Allocator;
  public:
    template<typename T, typename... Args>
    T* create(Args&&... args);
  };
  ```

- **E1.1.3** 创建 `AST/ASTVisitor.h`，实现访问者模式：
  ```cpp
  template<typename Derived>
  class ASTVisitor {
  public:
    void visit(ASTNode* Node);
    // 每个 AST 节点类型有对应的 visit 方法
  };
  ```

- **E1.1.4** 实现类型的 RTTI 系统（不使用 C++ RTTI）：
  ```cpp
  class Expr : public ASTNode {
  public:
    enum ExprKind { 
      IntegerLiteral, FloatingLiteral, StringLiteral, CharacterLiteral,
      DeclRefExpr, MemberExpr, CallExpr, BinaryOperator, UnaryOperator,
      ConditionalOperator, CXXNullPtrLiteral, CXXThisExpr, ...
    };
    virtual ExprKind getKind() const = 0;
    static bool classof(const ASTNode* N);
  };
  ```

- **E1.1.5** 实现源位置追踪：每个 AST 节点记录开始和结束位置

**开发关键点提示：**
> 请为 zetacc 实现 AST 基础架构。
>
> **ASTNode.h**：
> - 基类 `ASTNode`：包含 `SourceLocation`、虚析构函数、`dump()` 方法
> - 不使用 C++ RTTI，自定义 `getKind()` 方法实现类型判断
> - 使用 `classof()` 静态方法支持 `dyn_cast<>` / `isa<>` 类型转换
>
> **ASTContext.h/cpp**：
> - 使用 `llvm::BumpPtrAllocator` 分配内存
> - 提供 `create<T>(args...)` 模板方法创建节点
> - 所有节点由 ASTContext 统一管理，析构时自动释放
>
> **ASTVisitor.h**：
> - CRTP 模式：`template<typename Derived> class ASTVisitor`
> - 提供遍历所有 AST 节点的 visit 方法
> - 支持前序/后序遍历

**Checkpoint：** 编译通过；`ASTContext::create<IntegerLiteral>(...)` 成功创建节点；访问者模式能遍历 AST

---

### Task 2.1.2 表达式 AST 节点

**目标：** 定义所有 C++26 表达式的 AST 节点

**开发要点：**

- **E1.2.1** 创建 `AST/Expr.h`，定义表达式基类和所有表达式节点：

  **字面量表达式：**
  ```cpp
  class IntegerLiteral : public Expr { uint64_t Value; };
  class FloatingLiteral : public Expr { llvm::APFloat Value; };
  class StringLiteral : public Expr { StringRef Value; };
  class CharacterLiteral : public Expr { uint32_t Value; };
  class CXXNullPtrLiteral : public Expr {};  // nullptr
  class CXXBoolLiteral : public Expr { bool Value; };  // true/false
  ```

  **引用表达式：**
  ```cpp
  class DeclRefExpr : public Expr { ValueDecl* Decl; };  // 变量引用
  class MemberExpr : public Expr { Expr* Base; ValueDecl* Member; };  // x.member, p->member
  ```

  **运算符表达式：**
  ```cpp
  class BinaryOperator : public Expr { 
    Expr* LHS, *RHS; 
    BinaryOperatorKind Opcode;  // +, -, *, /, <, >, ==, &&, ||, ...
  };
  class UnaryOperator : public Expr { 
    Expr* SubExpr; 
    UnaryOperatorKind Opcode;  // +, -, !, ~, *, &, ++, --, ...
    bool IsPrefix;  // 前缀还是后缀
  };
  class ConditionalOperator : public Expr { Expr* Cond, *TrueExpr, *FalseExpr; };  // ?:
  ```

  **函数调用：**
  ```cpp
  class CallExpr : public Expr { 
    Expr* Callee; 
    std::vector<Expr*> Args; 
  };
  class CXXMemberCallExpr : public CallExpr {};  // 成员函数调用
  ```

  **C++ 特有表达式：**
  ```cpp
  class CXXThisExpr : public Expr {};  // this
  class CXXNewExpr : public Expr { Expr* ArraySize; Expr* Initializer; };  // new
  class CXXDeleteExpr : public Expr { Expr* Argument; bool IsArray; };  // delete
  class CXXThrowExpr : public Expr { Expr* SubExpr; };  // throw
  class CXXConstructExpr : public Expr { CXXConstructorDecl* Constructor; };  // 构造函数调用
  class CXXTemporaryObjectExpr : public CXXConstructExpr {};  // 临时对象
  ```

  **类型相关表达式：**
  ```cpp
  class CastExpr : public Expr { Expr* SubExpr; CastKind Kind; };  // 类型转换
  class CXXStaticCastExpr : public CastExpr {};  // static_cast
  class CXXDynamicCastExpr : public CastExpr {};  // dynamic_cast
  class CXXConstCastExpr : public CastExpr {};  // const_cast
  class CXXReinterpretCastExpr : public CastExpr {};  // reinterpret_cast
  class CStyleCastExpr : public CastExpr {};  // (T)e
  ```

  **Lambda 表达式：**
  ```cpp
  class LambdaExpr : public Expr { 
    std::vector<LambdaCapture> Captures;
    CompoundStmt* Body;
    bool IsMutable;
  };
  ```

  **C++20/26 新表达式：**
  ```cpp
  class RequiresExpr : public Expr { ... };  // C++20 requires 表达式
  class CXXFoldExpr : public Expr { Expr* Pattern; BinaryOperatorKind Op; bool IsLeft; };  // 折叠表达式
  class PackIndexingExpr : public Expr { Expr* Pack; Expr* Index; };  // C++26 T...[I]
  class ReflexprExpr : public Expr { Expr* Operand; };  // C++26 reflexpr
  ```

- **E1.2.2** 每个表达式类实现 `classof()` 方法支持类型判断
- **E1.2.3** 实现完整的 `dump()` 方法用于调试

**开发关键点提示：**
> 请为 zetacc 实现 `include/zetacc/AST/Expr.h` 和 `src/AST/Expr.cpp`。
>
> 定义所有 C++26 表达式的 AST 节点，包括：
> - 字面量：IntegerLiteral, FloatingLiteral, StringLiteral, CharacterLiteral, CXXNullPtrLiteral, CXXBoolLiteral
> - 引用：DeclRefExpr, MemberExpr
> - 运算符：BinaryOperator, UnaryOperator, ConditionalOperator
> - 调用：CallExpr, CXXMemberCallExpr
> - C++ 特有：CXXThisExpr, CXXNewExpr, CXXDeleteExpr, CXXThrowExpr, CXXConstructExpr
> - 类型转换：CastExpr 及其子类
> - Lambda：LambdaExpr
> - C++20/26：RequiresExpr, CXXFoldExpr, PackIndexingExpr, ReflexprExpr
>
> 每个类需要：
> - 成员变量存储必要信息
> - `getKind()` 方法返回类型枚举
> - `classof()` 静态方法支持 isa/dyn_cast
> - `dump()` 方法打印调试信息
> - `getSourceRange()` 方法返回源位置范围

**Checkpoint：** 所有表达式类编译通过；`isa<BinaryOperator>(expr)` 类型判断正确

---

### Task 2.1.3 语句 AST 节点

**目标：** 定义所有 C++ 语句的 AST 节点

**开发要点：**

- **E1.3.1** 创建 `AST/Stmt.h`，定义语句基类和所有语句节点：

  **基础语句：**
  ```cpp
  class NullStmt : public Stmt {};  // 空语句 ;
  class CompoundStmt : public Stmt { std::vector<Stmt*> Body; };  // { ... }
  class ReturnStmt : public Stmt { Expr* ReturnValue; };
  class ExprStmt : public Stmt { Expr* Expression; };  // 表达式语句
  ```

  **声明语句：**
  ```cpp
  class DeclStmt : public Stmt { DeclGroupRef Decls; };  // 声明作为语句
  ```

  **控制流语句：**
  ```cpp
  class IfStmt : public Stmt { 
    Expr* Condition; 
    Stmt* Then; 
    Stmt* Else; 
    VarDecl* ConditionVar;  // if (int x = ...)
  };
  class SwitchStmt : public Stmt { 
    Expr* Condition; 
    Stmt* Body; 
    VarDecl* ConditionVar;
  };
  class CaseStmt : public Stmt { Expr* LHS; Expr* RHS; Stmt* SubStmt; };  // case / case ... :
  class DefaultStmt : public Stmt { Stmt* SubStmt; };
  class BreakStmt : public Stmt {};
  class ContinueStmt : public Stmt {};
  class GotoStmt : public Stmt { LabelDecl* Label; };
  class LabelStmt : public Stmt { LabelDecl* Label; Stmt* SubStmt; };
  ```

  **循环语句：**
  ```cpp
  class WhileStmt : public Stmt { Expr* Condition; Stmt* Body; VarDecl* ConditionVar; };
  class DoStmt : public Stmt { Stmt* Body; Expr* Condition; };
  class ForStmt : public Stmt { 
    Stmt* Init; 
    Expr* Condition; 
    Expr* Increment; 
    Stmt* Body; 
    VarDecl* ConditionVar;
  };
  class CXXForRangeStmt : public Stmt { ... };  // C++11 范围 for
  ```

  **异常处理：**
  ```cpp
  class CXXTryStmt : public Stmt { CompoundStmt* TryBlock; std::vector<CXXCatchStmt*> Handlers; };
  class CXXCatchStmt : public Stmt { VarDecl* ExceptionDecl; Stmt* HandlerBlock; };
  ```

  **协程语句（C++20）：**
  ```cpp
  class CoreturnStmt : public Stmt { Expr* Operand; };  // co_return
  class CoyieldStmt : public Stmt { Expr* Operand; };   // co_yield
  ```

- **E1.3.2** 实现语句迭代器，方便遍历复合语句的子语句
- **E1.3.3** 实现 `getSourceRange()` 方法返回语句的源位置范围

**开发关键点提示：**
> 请为 zetacc 实现 `include/zetacc/AST/Stmt.h` 和 `src/AST/Stmt.cpp`。
>
> 定义所有 C++ 语句的 AST 节点，包括：
> - 基础：NullStmt, CompoundStmt, ReturnStmt, ExprStmt
> - 声明：DeclStmt
> - 控制流：IfStmt, SwitchStmt, CaseStmt, DefaultStmt, BreakStmt, ContinueStmt, GotoStmt, LabelStmt
> - 循环：WhileStmt, DoStmt, ForStmt, CXXForRangeStmt
> - 异常：CXXTryStmt, CXXCatchStmt
> - 协程：CoreturnStmt, CoyieldStmt
>
> 每个类需要完整的 `getKind()`, `classof()`, `dump()`, `getSourceRange()` 方法。
>
> CompoundStmt 需要提供迭代器支持 `for (Stmt* S : Compound)`。

**Checkpoint：** 所有语句类编译通过；能正确构建嵌套的复合语句 AST

---

### Task 2.1.4 AST 类型系统基础

**目标：** 建立类型系统基础，为表达式和声明提供类型信息

**开发要点：**

- **E1.4.1** 创建 `AST/Type.h`，定义类型表示：
  ```cpp
  class Type {
  public:
    enum TypeClass {
      Builtin, Pointer, Reference, Array, Function,
      Record, Enum, Typedef, TemplateTypeParm, 
      TemplateSpecialization, Dependent, ...
    };
  };
  
  class BuiltinType : public Type { BuiltinKind Kind; };  // int, char, void, ...
  class PointerType : public Type { Type* Pointee; };
  class ReferenceType : public Type { Type* Referenced; bool IsLValueRef; };
  class ArrayType : public Type { Type* ElementType; Expr* Size; };
  class FunctionType : public Type { Type* ReturnType; std::vector<Type*> ParamTypes; };
  class RecordType : public Type { RecordDecl* Decl; };  // class/struct
  class EnumType : public Type { EnumDecl* Decl; };
  ```

- **E1.4.2** 创建 `AST/QualType.h`，实现带 CV 限定符的类型：
  ```cpp
  class QualType {
    Type* Ty;
    unsigned Qualifiers;  // const, volatile, restrict
  public:
    bool isConst() const;
    bool isVolatile() const;
    bool isRestrict() const;
    Type* getTypePtr() const;
  };
  ```

- **E1.4.3** 实现 `Type::isInteger()`, `Type::isFloating()`, `Type::isPointer()` 等辅助方法
- **E1.4.4** 实现类型的规范化和比较

**开发关键点提示：**
> 请为 zetacc 实现类型系统基础 `include/zetacc/AST/Type.h` 和 `Type.cpp`。
>
> 定义：
> - `Type` 基类及其子类（BuiltinType, PointerType, ReferenceType, ArrayType, FunctionType, RecordType, EnumType）
> - `QualType` 类：包装 Type* 和 CV 限定符（const, volatile, restrict）
> - 类型比较和规范化
>
> BuiltinType 需要支持：Void, Bool, Char, Short, Int, Long, LongLong, Int128, Float, Double, LongDouble, Float128, Char8, Char16, Char32, WChar 等。
>
> QualType 需要实现：
> - `isConstQualified()`, `isVolatileQualified()`, `isRestrictQualified()`
> - `withConst()`, `withVolatile()`, `withoutConstQualifier()`
> - `operator==` 和 `operator!=`

**Checkpoint：** 类型系统能表示 C++ 所有内置类型；`QualType` 正确处理 CV 限定符

---

## Stage 2.2 — 表达式解析

### Task 2.2.1 Parser 基础架构

**目标：** 建立 Parser 的基础框架和错误恢复机制

**开发要点：**

- **E2.1.1** 创建 `Parse/Parser.h`，定义 Parser 类：
  ```cpp
  class Parser {
    Preprocessor &PP;
    TokenBuffer &Tokens;
    ASTContext &Context;
    DiagnosticsEngine &Diags;
    // 当前 token 缓存
    Token Tok;  // 当前 token
    Token NextTok;  // 下一个 token
  public:
    Parser(Preprocessor &PP, ASTContext &Ctx);
    TranslationUnitDecl* parseTranslationUnit();
  };
  ```

- **E2.1.2** 实现基础 token 操作：
  ```cpp
  void consumeToken();  // 消费当前 token
  bool tryConsumeToken(TokenKind K);  // 尝试消费，成功返回 true
  void expectAndConsume(TokenKind K, const char *Msg);  // 期望并消费
  Token& peekToken();  // 查看下一个 token
  ```

- **E2.1.3** 实现错误恢复机制：
  ```cpp
  void skipUntil(std::initializer_list<TokenKind> StopTokens);
  void emitError(SourceLocation Loc, DiagID ID);
  Expr* createRecoveryExpr(SourceLocation Loc);  // 创建错误恢复表达式
  ```

- **E2.1.4** 实现解析上下文跟踪：
  ```cpp
  enum ParsingContext {
    Expression, Statement, Declaration, MemberInitializer, TemplateArgument
  };
  std::vector<ParsingContext> ContextStack;
  ```

**开发关键点提示：**
> 请为 zetacc 实现 Parser 基础架构 `src/Parse/Parser.h` 和 `Parser.cpp`。
>
> **Parser 类核心成员**：
> - `Preprocessor& PP`：预处理器的引用
> - `TokenBuffer& Tokens`：token 缓冲区
> - `ASTContext& Context`：AST 上下文
> - `DiagnosticsEngine& Diags`：诊断引擎
> - `Token Tok, NextTok`：当前和下一个 token
>
> **核心方法**：
> - `consumeToken()`：消费当前 token，更新 Tok 和 NextTok
> - `tryConsumeToken(TokenKind K)`：如果当前 token 匹配则消费，返回是否成功
> - `expectAndConsume(TokenKind K, const char *Msg)`：期望特定 token，不匹配时报错
> - `skipUntil({TokenKind...})`：跳过 token 直到遇到指定的 token
>
> **错误恢复**：
> - 遇到语法错误时，跳到下一个安全的同步点
> - 创建 `RecoveryExpr` 占位，避免后续阶段崩溃

**Checkpoint：** Parser 能正确消费 token；错误恢复机制能跳过错误 token 到达同步点

---

### Task 2.2.2 运算符优先级表

**目标：** 建立完整的 C++ 运算符优先级表

**开发要点：**

- **E2.2.1** 创建 `Parse/OperatorPrecedence.h`，定义优先级枚举：
  ```cpp
  enum PrecedenceLevel {
    Unknown = 0,        // 未知
    Comma = 1,          // ,
    Assignment = 2,     // = += -= *= /= %= &= |= ^= <<= >>=
    Conditional = 3,    // ?:
    LogicalOr = 4,      // ||
    LogicalAnd = 5,     // &&
    InclusiveOr = 6,    // |
    ExclusiveOr = 7,    // ^
    And = 8,            // &
    Equality = 9,       // == !=
    Relational = 10,    // < > <= >= <=>
    Shift = 11,         // << >>
    Additive = 12,      // + -
    Multiplicative = 13, // * / %
    PM = 14,            // .* ->*
    Unary = 15,         // ++ -- + - ! ~ * & sizeof new delete
    Postfix = 16,       // () [] . -> ++ -- typeid
    Primary = 17,       // 字面量、标识符、(expr)
  };
  ```

- **E2.2.2** 实现 `getBinOpPrecedence(TokenKind K)` 查表函数
- **E2.2.3** 实现 `isRightAssociative(TokenKind K)` 判断右结合性
- **E2.2.4** 实现 `getUnaryOpPrecedence(TokenKind K)` 获取一元运算符优先级

**开发关键点提示：**
> 请实现 C++ 运算符优先级表 `src/Parse/OperatorPrecedence.h`。
>
> 遵循 C++ 标准定义的优先级（从低到高）：
> 1. 逗号
> 2. 赋值 = += -= *= /= %= &= |= ^= <<= >>=（右结合）
> 3. 条件 ?:（右结合）
> 4. 逻辑或 ||
> 5. 逻辑与 &&
> 6. 位或 |
> 7. 位异或 ^
> 8. 位与 &
> 9. 相等 == !=
> 10. 关系 < > <= >= <=>
> 11. 移位 << >>
> 12. 加减 + -
> 13. 乘除模 * / %
> 14. 成员指针 .* ->*
> 15. 一元 ++ -- + - ! ~ * & sizeof（前缀）
> 16. 后缀 () [] . -> ++ -- typeid
> 17. 基本 字面量、标识符、括号表达式
>
> 提供函数：
> - `PrecedenceLevel getBinOpPrecedence(TokenKind K)`
> - `bool isRightAssociative(TokenKind K)`
> - `PrecedenceLevel getUnaryOpPrecedence(TokenKind K)`

**Checkpoint：** 所有运算符优先级正确；右结合性判断正确

---

### Task 2.2.3 表达式解析实现

**目标：** 实现完整的表达式解析算法

**开发要点：**

- **E2.3.1** 实现主入口：
  ```cpp
  Expr* parseExpression();
  ```

- **E2.3.2** 实现优先级爬升算法：
  ```cpp
  Expr* parseRHS(Expr* LHS, PrecedenceLevel MinPrec);
  ```

- **E2.3.3** 实现基本表达式解析：
  ```cpp
  Expr* parsePrimaryExpression();  // 字面量、标识符、(expr)
  Expr* parseIntegerLiteral();
  Expr* parseFloatingLiteral();
  Expr* parseStringLiteral();
  Expr* parseCharacterLiteral();
  Expr* parseParenExpression();
  Expr* parseIdentifier();
  ```

- **E2.3.4** 实现一元表达式解析：
  ```cpp
  Expr* parseUnaryExpression();  // ++e, --e, +e, -e, !e, ~e, *e, &e, sizeof, new, delete
  ```

- **E2.3.5** 实现后缀表达式解析：
  ```cpp
  Expr* parsePostfixExpression(Expr* Base);  // e(), e[], e., e->, e++, e--
  Expr* parseCallArguments();  // 解析函数调用参数
  ```

- **E2.3.6** 实现三元条件表达式解析：
  ```cpp
  Expr* parseConditionalExpression(Expr* Cond);  // cond ? true : false
  ```

- **E2.3.7** 实现赋值表达式解析：
  ```cpp
  Expr* parseAssignmentExpression(Expr* LHS);
  ```

- **E2.3.8** 实现字面量解析：
  ```cpp
  Expr* parseBoolLiteral();  // true, false
  Expr* parseNullPtrLiteral();  // nullptr
  Expr* parseUserDefinedLiteral();  // 用户定义字面量 123_i
  ```

**开发关键点提示：**
> 请为 zetacc 实现完整的表达式解析 `src/Parse/ParseExpr.cpp`。
>
> **算法选择**：优先级爬升算法，比递归下降更灵活。
>
> **核心函数结构**：
> ```cpp
> // 入口
> Expr* Parser::parseExpression() {
>   Expr* LHS = parseUnaryExpression();
>   return parseRHS(LHS, PrecedenceLevel::Comma);
> }
>
> // 优先级爬升
> Expr* Parser::parseRHS(Expr* LHS, PrecedenceLevel MinPrec) {
>   while (true) {
>     PrecedenceLevel TokPrec = getBinOpPrecedence(Tok.Kind);
>     if (TokPrec < MinPrec) break;
>     
>     TokenKind BinOp = Tok.Kind;
>     consumeToken();
>     
>     Expr* RHS = parseUnaryExpression();
>     
>     // 检查下一个运算符优先级
>     PrecedenceLevel NextPrec = getBinOpPrecedence(Tok.Kind);
>     if (TokPrec < NextPrec || (TokPrec == NextPrec && isRightAssociative(BinOp))) {
>       RHS = parseRHS(RHS, TokPrec);
>     }
>     
>     LHS = Context.create<BinaryOperator>(LHS, RHS, BinOp, LHS->getLocation());
>   }
>   return LHS;
> }
> ```
>
> **解析函数列表**：
> - `parsePrimaryExpression()`：字面量、标识符、括号表达式
> - `parseUnaryExpression()`：前缀运算符
> - `parsePostfixExpression(Expr*)`：后缀运算符
> - `parseConditionalExpression(Expr*)`：三元条件
> - `parseAssignmentExpression(Expr*)`：赋值
>
> **C++26 特殊处理**：
> - `PackIndexingExpr`：`T...[I]` 语法
> - `ReflexprExpr`：`reflexpr(type)` 语法

**Checkpoint：** 能正确解析所有 C++ 表达式；运算符优先级正确；嵌套表达式正确

---

### Task 2.2.4 C++ 特有表达式解析

**目标：** 实现C++ 特有的表达式解析

**开发要点：**

- **E2.4.1** 实现 new/delete 表达式解析：
  ```cpp
  Expr* parseCXXNewExpression();
  Expr* parseCXXDeleteExpression();
  ```

- **E2.4.2** 实现类型转换表达式解析：
  ```cpp
  Expr* parseCXXStaticCastExpr();
  Expr* parseCXXDynamicCastExpr();
  Expr* parseCXXConstCastExpr();
  Expr* parseCXXReinterpretCastExpr();
  Expr* parseCStyleCastExpr();  // (T)e
  ```

- **E2.4.3** 实现 this 表达式解析：
  ```cpp
  Expr* parseCXXThisExpr();
  ```

- **E2.4.4** 实现 throw 表达式解析：
  ```cpp
  Expr* parseCXXThrowExpr();
  ```

- **E2.4.5** 实现 Lambda 表达式解析：
  ```cpp
  Expr* parseLambdaExpression();  // [captures](params) -> ret { body }
  ```

- **E2.4.6** 实现折叠表达式解析（C++17）：
  ```cpp
  Expr* parseFoldExpression();  // (... op expr), (expr op ...), (expr1 op ... op expr2)
  ```

- **E2.4.7** 实现 requires 表达式解析（C++20）：
  ```cpp
  Expr* parseRequiresExpression();  // requires { requirements }
  ```

**开发关键点提示：**
> 请实现 C++ 特有表达式的解析 `src/Parse/ParseExprCXX.cpp`。
>
> **Lambda 表达式解析**：
> 语法：`[captures](params) mutable -> ret { body }`
> 1. 解析 capture 列表：`[x, &y, &z = expr]`
> 2. 解析参数列表（可选）
> 3. 解析 mutable（可选）
> 4. 解析返回类型（可选）
> 5. 解析函数体
>
> **new 表达式解析**：
> 语法：`new T`, `new T()`, `new T(args)`, `new T[n]`
> - 支持 placement new：`new (ptr) T`
>
> **类型转换解析**：
> - `static_cast<T>(e)`：解析 <T> 和 (e)
> - `dynamic_cast<T>(e)`
> - `const_cast<T>(e)`
> - `reinterpret_cast<T>(e)`
> - `(T)e`：C 风格转换
>
> **requires 表达式解析**：
> 语法：`requires { requirement; requirement; ... }`
> requirement 类型：simple, type, compound, nested

**Checkpoint：** Lambda、new/delete、类型转换、requires 表达式正确解析

---

## Stage 2.3 — 语句解析

### Task 2.3.1 基础语句解析

**目标：** 实现基础语句的解析

**开发要点：**

- **E3.1.1** 实现语句解析入口：
  ```cpp
  Stmt* parseStatement();
  ```

- **E3.1.2** 实现各类语句解析：
  ```cpp
  Stmt* parseCompoundStatement();  // { ... }
  Stmt* parseReturnStatement();  // return expr;
  Stmt* parseNullStatement();  // ;
  Stmt* parseDeclarationStatement();  // int x = 10;
  Stmt* parseExpressionStatement();  // expr;
  ```

- **E3.1.3** 实现标签语句解析：
  ```cpp
  Stmt* parseLabelStatement();  // label:
  Stmt* parseCaseStatement();  // case expr:
  Stmt* parseDefaultStatement();  // default:
  ```

- **E3.1.4** 实现跳转语句解析：
  ```cpp
  Stmt* parseBreakStatement();
  Stmt* parseContinueStatement();
  Stmt* parseGotoStatement();
  ```

**开发关键点提示：**
> 请实现基础语句解析 `src/Parse/ParseStmt.cpp`。
>
> **核心逻辑**：
> ```cpp
> Stmt* Parser::parseStatement() {
>   switch (Tok.Kind) {
>     case tok::l_brace:   return parseCompoundStatement();
>     case tok::kw_return: return parseReturnStatement();
>     case tok::kw_break:  return parseBreakStatement();
>     case tok::kw_continue: return parseContinueStatement();
>     case tok::kw_goto:   return parseGotoStatement();
>     case tok::kw_case:   return parseCaseStatement();
>     case tok::kw_default: return parseDefaultStatement();
>     case tok::identifier:
>       if (NextTok.Kind == tok::colon) return parseLabelStatement();
>       break;
>     // ... 其他情况
>   }
>   // 默认：表达式语句或声明语句
>   if (isDeclarationStatement()) return parseDeclarationStatement();
>   return parseExpressionStatement();
> }
> ```
>
> **复合语句**：
> `{` → 循环解析语句 → `}`
> 使用 `CompoundStmt` 收集所有子语句。
>
> **return 语句**：
> `return` → [表达式] → `;`
>
> **声明语句**：
> 判断 token 序列是否为类型开始，若是则解析声明。

**Checkpoint：** 复合语句、return、break、continue、goto、label 语句正确解析

---

### Task 2.3.2 控制流语句解析

**目标：** 实现控制流语句的解析

**开发要点：**

- **E3.2.1** 实现if 语句解析：
  ```cpp
  Stmt* parseIfStatement();  // if (cond) stmt [else stmt]
  ```

- **E3.2.2** 实现switch 语句解析：
  ```cpp
  Stmt* parseSwitchStatement();  // switch (cond) stmt
  ```

- **E3.2.3** 实现循环语句解析：
  ```cpp
  Stmt* parseWhileStatement();  // while (cond) stmt
  Stmt* parseDoStatement();  // do stmt while (cond);
  Stmt* parseForStatement();  // for (init; cond; inc) stmt
  Stmt* parseCXXForRangeStatement();  // for (decl : range) stmt
  ```

- **E3.2.4** 处理条件中的声明：
  ```cpp
  // if (int x = getValue()) ...
  VarDecl* parseConditionDeclaration();
  ```

**开发关键点提示：**
> 请实现控制流语句解析，追加到 `src/Parse/ParseStmt.cpp`。
>
> **if 语句**：
> ```
> if-statement:
>   'if' '(' condition ')' statement
>   'if' '(' condition ')' statement 'else' statement
> 
> condition:
>   expression
>   type-specifier-seq declarator '=' expression
> ```
>
> **for 循环**：
> ```
> for-statement:
>   'for' '(' for-init-statement condition ';' expression ')' statement
> 
> for-init-statement:
>   expression-statement
>   simple-declaration
> ```
>
> **范围 for（C++11）**：
> ```
> for-range-statement:
>   'for' '(' for-range-declaration ':' for-range-initializer ')' statement
> ```
>
> 注意：条件中的声明需要正确处理作用域。

**Checkpoint：** if/switch/while/do/for/范围for 语句正确解析

---

### Task 2.3.3 异常处理语句解析

**目标：** 实现 try/catch 语句的解析

**开发要点：**

- **E3.3.1** 实现 try 语句解析：
  ```cpp
  Stmt* parseCXXTryStatement();  // try { } catch { } ...
  ```

- **E3.3.2** 实现 catch 子句解析：
  ```cpp
  CXXCatchStmt* parseCXXCatchClause();  // catch (type name) { }
  ```

- **E3.3.3** 处理 catch-all：`catch (...)`

**开发关键点提示：**
> 请实现异常处理语句解析，追加到 `src/Parse/ParseStmtCXX.cpp`。
>
> **try 语句**：
> ```
> try-statement:
>   'try' compound-statement handler-seq
> 
> handler-seq:
>   handler handler-seq?
> 
> handler:
>   'catch' '(' exception-declaration ')' compound-statement
> 
> exception-declaration:
>   type-specifier-seq declarator
>   type-specifier-seq abstract-declarator?
>   '...'
> ```

**Checkpoint：** try/catch 语句正确解析；catch-all 正确处理

---

### Task 2.3.4 协程语句解析（C++20）

**目标：** 实现协程相关语句的解析

**开发要点：**

- **E4.1.1** 实现 co_return 解析：
  ```cpp
  Stmt* parseCoreturnStatement();  // co_return expr;
  ```

- **E4.1.2** 实现 co_yield 解析：
  ```cpp
  Stmt* parseCoyieldStatement();  // co_yield expr;
  ```

- **E4.1.3** 实现 co_await 表达式解析：
  ```cpp
  Expr* parseCoawaitExpression();  // co_await expr
  ```

**开发关键点提示：**
> 请实现协程语句解析，追加到 `src/Parse/ParseStmtCXX.cpp`。
>
> **co_return**：
> ```
> coreturn-statement:
>   'co_return' expression? ';'
> ```
>
> **co_yield**：
> ```
> coyield-statement:
>   'co_yield' expression ';'
> ```
>
> **co_await**（作为一元表达式）：
> ```
> coawait-expression:
>   'co_await' cast-expression
> ```

**Checkpoint：** co_return/co_yield/co_await 正确解析

---

## Stage 2.4 — C++26 特性 + 集成测试

### Task 2.4.1 C++26 表达式新特性

**目标：** 实现 C++26 新增的表达式语法

**开发要点：**

- **E4.1.1** 实现参数包索引表达式解析：
  ```cpp
  Expr* parsePackIndexingExpr();  // T...[I]
  ```

- **E4.1.2** 实现反射表达式解析：
  ```cpp
  Expr* parseReflexprExpr();  // reflexpr(type)
  ```

- **E4.1.3** 在 `-std=c++26` 模式下启用新语法

**开发关键点提示：**
> 请实现 C++26 表达式新特性的解析。
>
> **参数包索引（Pack Indexing）**：
> 语法：`T...[I]`
> - T 是参数包
> - I 是编译时常量索引
> - 示例：`template<typename... Ts> void f() { using First = Ts...[0]; }`
>
> **反射表达式**：
> 语法：`reflexpr(type-id)` 或 `reflexpr(expression)`
> - 返回类型的元信息
> - 示例：`constexpr auto info = reflexpr(int);`

**Checkpoint：** C++26 新表达式语法正确解析

---

### Task 2.4.2 AST 打印与调试

**目标：** 实现 AST 的完整打印和调试功能

**开发要点：**

- **E4.2.1** 实现 `ASTDumper` 类：
  ```cpp
  class ASTDumper {
    raw_ostream &OS;
    unsigned Indent = 0;
  public:
    void dump(const ASTNode* Node);
    void dump(const Expr* E);
    void dump(const Stmt* S);
  };
  ```

- **E4.2.2** 为每个 AST 节点实现格式化输出
- **E4.2.3** 添加 `-ast-dump` 编译选项
- **E4.2.4** 输出格式示例：
  ```
  TranslationUnitDecl
  `-FunctionDecl main 'int ()'
    `-CompoundStmt
      |-ReturnStmt
      | `-IntegerLiteral '0'
      `-DeclStmt
        `-VarDecl x 'int' cinit
          `-IntegerLiteral '42'
  ```

**开发关键点提示：**
> 请实现 AST 打印功能 `src/AST/ASTDumper.cpp`。
>
> 格式要求：
> - 使用树形结构（类似 Clang 的 -ast-dump）
> - 每个节点一行，包含节点类型和关键信息
> - 缩进表示层级关系
> - 使用 `|-` 和 `` ` - `` 表示分支和叶子
>
> 输出内容：
> - 表达式：类型、值（字面量）、运算符
> - 语句：语句类型
> - 声明：名称、类型

**Checkpoint：** `-ast-dump` 输出格式正确、可读

---

### Task 2.4.3 Parser 单元测试

**目标：** 建立 Parser 的完整测试覆盖

**开发要点：**

- **E4.3.1** 创建 `tests/unit/Parse/` 目录
- **E4.3.2** 编写表达式解析测试：
  ```cpp
  TEST(ParserTest, BinaryExpression) {
    EXPECT_TRUE(parseExpr("1 + 2 * 3", result));
    EXPECT_TRUE(isa<BinaryOperator>(result));
    EXPECT_EQ(result->dump(), "(1 + (2 * 3))");
  }
  ```
- **E4.3.3** 编写语句解析测试
- **E4.3.4** 编写错误恢复测试
- **E4.3.5** 测试覆盖率目标：≥ 80%

**Checkpoint：** 所有测试通过；覆盖率 ≥ 80%

---

### Task 2.4.4 Parser 性能基准

**目标：** 建立 Parser 性能基准

**开发要点：**

- **E4.4.1** 创建性能测试用例：
  - 深度嵌套表达式
  - 大型复合语句
  - 模板实例化密集代码
- **E4.4.2** 建立性能基准数据
- **E4.4.3** 对比 Clang 的解析性能

**Checkpoint：** 性能基准建立；解析速度在 Clang 的 2 倍以内

---

## 📋 Phase 2 验收检查清单

```
[ ] 所有表达式 AST 节点定义完成
[ ] 所有语句 AST 节点定义完成
[ ] 类型系统基础完成（BuiltinType, PointerType, ReferenceType, ArrayType, FunctionType, RecordType, EnumType, QualType）
[ ] 运算符优先级表完整
[ ] 优先级爬升算法正确实现
[ ] 所有 C++ 表达式类型正确解析
[ ] 所有 C++ 语句类型正确解析
[ ] Lambda 表达式正确解析
[ ] new/delete 表达式正确解析
[ ] 类型转换表达式正确解析
[ ] if/switch/while/do/for 语句正确解析
[ ] 范围 for 正确解析
[ ] try/catch 语句正确解析
[ ] co_return/co_yield/co_await 正确解析
[ ] C++26 参数包索引表达式正确解析
[ ] C++26 反射表达式正确解析
[ ] 错误恢复机制工作正常
[ ] -ast-dump 输出正确可读
[ ] 单元测试覆盖率 ≥ 80%
[ ] 性能基准建立
```

---

*Phase 2 完成标志：能够将预处理后的 Token 流正确解析为 AST，-ast-dump 输出可读，所有测试通过，覆盖率 ≥ 80%。*
