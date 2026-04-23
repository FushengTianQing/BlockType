# BlockType 编译器完整流程图

**生成时间**: 2026-04-21 20:48
**基于**: Task 1.1-1.3 分析结果

---

## 📊 完整编译流程

```mermaid
graph TD
    A[main] --> B[Driver::compile]
    B --> C[CompilerInstance::ExecuteCompilation]
    C --> D[Parser::parseTranslationUnit]
    
    D --> E[Parser::parseDeclaration]
    E --> F{声明类型?}
    
    %% 变量声明路径
    F -->|变量| G[Parser::buildVarDecl]
    G --> H[Sema::ActOnVarDeclFull]
    H --> I[TypeCheck::CheckInitialization]
    H --> J[添加到符号表]
    H --> K[返回 VarDecl*]
    
    %% 函数声明路径
    F -->|函数| L[Parser::buildFunctionDecl]
    L --> M[Sema::ActOnFunctionDeclFull]
    M --> N[创建 FunctionDecl]
    M --> O[ActOnStartOfFunctionDef]
    O --> P[解析函数体]
    P --> Q[TypeCheck 函数体]
    Q --> R[ActOnFinishOfFunctionDef]
    R --> S[返回 FunctionDecl*]
    
    %% 类声明路径
    F -->|类| T[Parser::parseClassDeclaration]
    T --> U[Sema::ActOnCXXRecordDeclFactory]
    U --> V[创建 CXXRecordDecl]
    U --> W[解析类体]
    W --> X[parseClassMember]
    X --> Y[返回 CXXRecordDecl*]
    
    %% 模板声明路径
    F -->|模板| Z[Parser::parseTemplateDeclaration]
    Z --> AA[解析模板参数]
    AA --> AB[解析模板内容]
    AB --> AC[返回 TemplateDecl*]
```

---

## 🔄 表达式处理流程

```mermaid
graph TD
    A[Parser::parseExpression] --> B{表达式类型?}
    
    %% 函数调用
    B -->|函数调用| C[Parser::parseCallExpression]
    C --> D[Sema::ActOnCallExpr]
    D --> E{是 Lambda?}
    E -->|是| F[查找 operator()]
    E -->|否| G[获取 FunctionDecl]
    F --> H[TypeCheck::CheckCall]
    G --> H
    H --> I[创建 CallExpr]
    I --> J[设置返回类型]
    J --> K[返回 CallExpr*]
    
    %% 二元运算
    B -->|二元运算| L[Parser::parseRHS]
    L --> M[Sema::ActOnBinaryOp]
    M --> N[TypeCheck::getCommonType]
    N --> O[创建 BinaryOperator]
    O --> P[设置结果类型]
    P --> Q[返回 BinaryOperator*]
    
    %% 一元运算
    B -->|一元运算| R[Parser::parseUnaryExpression]
    R --> S[Sema::ActOnUnaryOp]
    S --> T[TypeCheck::getUnaryOperatorResultType]
    T --> U[创建 UnaryOperator]
    U --> V[返回 UnaryOperator*]
    
    %% 变量引用
    B -->|标识符| W[Parser::parseIdentifier]
    W --> X[Sema::ActOnIdExpr]
    X --> Y[查找符号]
    Y --> Z[创建 DeclRefExpr]
    Z --> AA[返回 DeclRefExpr*]
    
    %% 字面量
    B -->|字面量| AB[Parser::parseIntegerLiteral等]
    AB --> AC[Sema::ActOnIntegerLiteral等]
    AC --> AD[创建字面量表达式]
    AD --> AE[返回表达式*]
```

---

## 🏗️ Sema 语义分析流程

```mermaid
graph TD
    A[Sema 入口] --> B{ActOn 函数类型?}
    
    %% 声明处理
    B -->|ActOnVarDecl| C[创建 VarDecl]
    C --> D{有初始化?}
    D -->|是| E[CheckInitialization]
    D -->|否| F[跳过检查]
    E --> G{检查通过?}
    G -->|是| H[添加到符号表]
    G -->|否| I[返回 Invalid]
    F --> H
    H --> J[添加到上下文]
    J --> K[返回 DeclResult]
    
    %% 函数处理
    B -->|ActOnFunctionDecl| L[创建 FunctionDecl]
    L --> M[注册到符号表]
    M --> N{有函数体?}
    N -->|是| O[ActOnStartOfFunctionDef]
    O --> P[PushScope]
    P --> Q[注册参数]
    Q --> R[解析函数体]
    R --> S[TypeCheck 语句]
    S --> T[ActOnFinishOfFunctionDef]
    T --> U[PopScope]
    N -->|否| V[跳过函数体处理]
    U --> W[返回 DeclResult]
    V --> W
    
    %% 表达式处理
    B -->|ActOnCallExpr| X[检查调用者]
    X --> Y{调用者类型?}
    Y -->|Lambda| Z[查找 operator()]
    Y -->|函数| AA[获取 FunctionDecl]
    Z --> AB[CheckCall]
    AA --> AB
    AB --> AC{参数匹配?}
    AC -->|是| AD[创建 CallExpr]
    AC -->|否| AE[发出错误]
    AD --> AF[设置返回类型]
    AF --> AG[返回 ExprResult]
```

---

## 🔍 TypeCheck 类型检查流程

```mermaid
graph TD
    A[TypeCheck 入口] --> B{检查类型?}
    
    %% 初始化检查
    B -->|CheckInitialization| C{目标类型?}
    C -->|引用| D[CheckReferenceBinding]
    C -->|列表| E[CheckListInitialization]
    C -->|普通| F[CheckAssignment]
    
    D --> G[检查引用绑定规则]
    G --> H{绑定有效?}
    H -->|是| I[返回 true]
    H -->|否| J[发出诊断]
    
    E --> K[检查列表元素]
    K --> L{所有元素匹配?}
    L -->|是| I
    L -->|否| J
    
    F --> M[isTypeCompatible]
    M --> N{类型兼容?}
    N -->|是| I
    N -->|否| J
    
    %% 函数调用检查
    B -->|CheckCall| O[获取参数列表]
    O --> P{参数数量匹配?}
    P -->|否| J
    P -->|是| Q[遍历参数]
    Q --> R[CheckInitialization]
    R --> S{所有参数通过?}
    S -->|是| I
    S -->|否| J
    
    %% 返回语句检查
    B -->|CheckReturn| T[获取返回类型]
    T --> U[CheckAssignment]
    U --> V{返回类型匹配?}
    V -->|是| I
    V -->|否| J
```

---

## 📦 符号管理流程

```mermaid
graph TD
    A[符号管理] --> B{操作类型?}
    
    %% 作用域管理
    B -->|PushScope| C[创建新 Scope]
    C --> D[设置父作用域]
    D --> E[压入作用域栈]
    E --> F[返回新作用域]
    
    B -->|PopScope| G[弹出作用域栈]
    G --> H[清理符号]
    H --> I[返回父作用域]
    
    %% 符号注册
    B -->|registerDecl| J{有当前作用域?}
    J -->|是| K[添加到 CurrentScope]
    J -->|否| L[跳过]
    K --> M{有当前上下文?}
    M -->|是| N[添加到 CurContext]
    M -->|否| L
    
    %% 符号查找
    B -->|LookupName| O[从当前作用域开始]
    O --> P[遍历作用域链]
    P --> Q{找到符号?}
    Q -->|是| R[返回声明]
    Q -->|否| S[继续向上查找]
    S --> T{到达全局作用域?}
    T -->|否| P
    T -->|是| U[返回 nullptr]
```

---

## 🎯 数据流图

### Parser → Sema 数据流

```mermaid
sequenceDiagram
    participant P as Parser
    participant S as Sema
    participant T as TypeCheck
    
    Note over P: 解析声明
    P->>S: ActOnVarDecl(Loc, Name, Type, Init)
    S->>T: CheckInitialization(Type, Init, Loc)
    T-->>S: bool (成功/失败)
    S->>S: 添加到符号表
    S-->>P: DeclResult(VarDecl*)
    
    Note over P: 解析函数调用
    P->>S: ActOnCallExpr(Fn, Args, LParen, RParen)
    S->>T: CheckCall(FD, Args, CallLoc)
    T-->>S: bool (成功/失败)
    S->>S: 创建 CallExpr
    S-->>P: ExprResult(CallExpr*)
```

### Sema 内部数据流

```mermaid
sequenceDiagram
    participant S as Sema
    participant TC as TypeCheck
    participant SYM as SymbolTable
    participant CTX as DeclContext
    
    Note over S: 处理变量声明
    S->>S: 创建 VarDecl
    S->>TC: CheckInitialization
    TC-->>S: bool
    S->>SYM: addDecl(VarDecl)
    S->>CTX: addDecl(VarDecl)
    S-->>S: 返回 DeclResult
    
    Note over S: 处理函数定义
    S->>S: 创建 FunctionDecl
    S->>SYM: registerDecl(FunctionDecl)
    S->>S: ActOnStartOfFunctionDef
    S->>SYM: PushScope(FunctionBody)
    S->>SYM: 注册参数
    loop 遍历函数体
        S->>TC: 检查语句
    end
    S->>SYM: PopScope
    S->>S: ActOnFinishOfFunctionDef
    S-->>S: 返回 DeclResult
```

---

## 📊 统计数据

### 函数调用统计

| 模块 | 函数类型 | 数量 | 主要功能 |
|------|----------|------|----------|
| Parser | parse* | 50+ | 语法解析 |
| Sema | ActOn* | 50+ | 语义分析 |
| TypeCheck | Check* | 9 | 类型检查 |
| SymbolTable | addDecl/Lookup | 10+ | 符号管理 |

### AST 节点统计

| 类别 | 节点类型 | 数量 |
|------|----------|------|
| 声明 | Decl | 30+ |
| 语句 | Stmt | 15+ |
| 表达式 | Expr | 40+ |
| 类型 | Type | 20+ |

---

## 💡 关键发现

### 1. 清晰的职责分离

- **Parser**: 语法分析，构建 AST 结构
- **Sema**: 语义分析，类型检查，符号管理
- **TypeCheck**: 类型兼容性检查
- **SymbolTable**: 符号存储和查找

### 2. 统一的模式

所有 ActOn 函数遵循相同模式：
1. 创建 AST 节点
2. 类型检查
3. 符号注册
4. 返回结果

### 3. 错误处理机制

使用 Result 对象统一处理错误：
- `DeclResult::getInvalid()`
- `ExprResult::getInvalid()`
- `StmtResult::getInvalid()`

### 4. 作用域管理

使用 RAII 模式管理作用域：
- PushScope/PopScope 配对
- 自动清理符号

---

## 🔗 相关文档

- Task 1.1: Parser 流程梳理
- Task 1.2: Parser 问题诊断
- Task 1.3: Sema 流程细化（本文档）

---

**报告生成时间**: 2026-04-21 20:48
**文件位置**: `docs/review_flowchart.md`
