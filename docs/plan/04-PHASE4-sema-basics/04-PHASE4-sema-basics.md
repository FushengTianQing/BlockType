# Phase 4：语义分析基础（Sema）
> **目标：** 实现完整的语义分析框架，包括符号表、类型系统、名字查找、重载决议、类型检查等核心功能
> **前置依赖：** Phase 0-3 完成（Lexer + Parser + AST）
> **验收标准：** 能对完整的 C++ 程序进行语义分析，生成正确的符号表和类型信息

---

## 📌 阶段总览

```
Phase 4 包含 5 个 Stage，共 15 个 Task，预计 5 周完成。
依赖链：Stage 4.1 → Stage 4.2 → Stage 4.3 → Stage 4.4 → Stage 4.5
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 4.1** | Sema 基础设施 | Sema 类、符号表、Scope 完善 | 1 周 |
| **Stage 4.2** | 类型系统完善 | 类型工厂补全、CV 限定符、类型推导 | 1 周 |
| **Stage 4.3** | 名字查找 | Unqualified/Qualified Lookup、ADL | 1 周 |
| **Stage 4.4** | 重载决议 | 候选集、排序、最佳匹配 | 1 周 |
| **Stage 4.5** | 语义检查 | 类型检查、访问控制、诊断、常量求值 | 1 周 |

### 已有基础设施（Phase 0-3 已完成）

以下组件已存在且**无需从零创建**，Phase 4 应在它们之上构建：

| 组件 | 位置 | 状态 |
|------|------|------|
| **Scope** | `include/blocktype/Sema/Scope.h` + `src/Sema/Scope.cpp` | ✅ 完整实现 |
| **DeclContext** | `include/blocktype/AST/DeclContext.h` | ✅ 完整实现 |
| **Type 体系** | `include/blocktype/AST/Type.h` | ✅ 完整（22 种内建类型、指针、引用、数组、函数、记录、枚举等） |
| **QualType** | `include/blocktype/AST/Type.h` | ✅ 完整（Qualifier enum、CV 限定符操作） |
| **ASTContext** | `include/blocktype/AST/ASTContext.h` | ✅ 类型工厂已有（getBuiltinType, getPointerType 等） |
| **Decl 体系** | `include/blocktype/AST/Decl.h` | ✅ 完整（30+ 种 Decl 子类） |
| **TemplateArgument** | `include/blocktype/AST/Type.h` | ✅ 完整（Type/Integral/Expression/Declaration/Pack 等） |
| **DiagnosticsEngine** | `include/blocktype/Basic/Diagnostics.h` | ✅ 双语诊断 |
| **DiagnosticIDs** | `include/blocktype/Basic/DiagnosticIDs.h` + `.def` | ✅ 已有诊断 ID |
| **AccessSpecifier** | `include/blocktype/AST/Decl.h` | ✅ AS_public/AS_protected/AS_private |

**Phase 4 需要新建的组件：**

| 组件 | 文件 | 说明 |
|------|------|------|
| Sema 主类 | `include/blocktype/Sema/Sema.h` + `src/Sema/Sema.cpp` | 语义分析引擎 |
| 符号表 | `include/blocktype/Sema/SymbolTable.h` + `src/Sema/SymbolTable.cpp` | 全局/局部符号管理 |
| 名字查找 | `include/blocktype/Sema/Lookup.h` + `src/Sema/Lookup.cpp` | Unqualified/Qualified/ADL |
| 重载决议 | `include/blocktype/Sema/Overload.h` + `src/Sema/Overload.cpp` | 候选集、转换排序 |
| 隐式转换 | `include/blocktype/Sema/Conversion.h` + `src/Sema/Conversion.cpp` | 标准转换序列 |
| 类型检查 | `include/blocktype/Sema/TypeCheck.h` + `src/Sema/TypeCheck.cpp` | 类型兼容性检查 |
| 访问控制 | `include/blocktype/Sema/AccessControl.h` + `src/Sema/AccessControl.cpp` | 权限检查 |
| 常量求值 | `include/blocktype/Sema/ConstantExpr.h` + `src/Sema/ConstantExpr.cpp` | constexpr 求值 |
| 类型推导 | `include/blocktype/Sema/TypeDeduction.h` + `src/Sema/TypeDeduction.cpp` | auto/decltype 推导 |
| Sema 诊断 ID | `include/blocktype/Basic/DiagnosticSemaKinds.def` | 语义分析诊断 |

**Phase 4 架构图：**

```
┌─────────────────────────────────────────────────────────────┐
│                        Sema                                  │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ SymbolTable  │  │  (已有 Scope)│  │   (已有 DeclCtx) │  │
│  │              │  │              │  │                  │  │
│  │ - globals_   │  │ - Current_   │  │ - ParentCtx      │  │
│  │ - tags_      │  │ - Decls_     │  │ - Decls[]        │  │
│  │ - typedefs_  │  │ - lookup()   │  │ - lookup()       │  │
│  └──────────────┘  └──────────────┘  └──────────────────┘  │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                Lookup (名字查找)                       │   │
│  │  LookupResult LookupUnqualified(Name, Scope)          │   │
│  │  LookupResult LookupQualified(Name, NestedNameSpec)   │   │
│  │  void         LookupADL(Name, Args, Result)           │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │             Overload Resolution (重载决议)             │   │
│  │  OverloadCandidateSet → resolve(Args) → FunctionDecl* │   │
│  │  ImplicitConversion → StandardConversionSequence      │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Type Check + Access + ConstEval           │   │
│  │  CheckAssign/Init/Call/Return  CheckAccess  EvalConst │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## Stage 4.1 — Sema 基础设施

### Task 4.1.1 Sema 主类

**目标：** 实现语义分析引擎的核心类

**开发要点：**

- **E4.1.1.1** 创建 `include/blocktype/Sema/Sema.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/ASTContext.h"
  #include "blocktype/AST/Decl.h"
  #include "blocktype/AST/Expr.h"
  #include "blocktype/AST/Stmt.h"
  #include "blocktype/AST/Type.h"
  #include "blocktype/Basic/Diagnostics.h"
  #include "blocktype/Sema/SymbolTable.h"
  #include "blocktype/Sema/Scope.h"
  #include "blocktype/Sema/Lookup.h"
  #include "blocktype/Sema/Overload.h"
  #include "llvm/ADT/DenseMap.h"
  
  namespace blocktype {
  
  /// ExprResult - Wrapper for expression semantic analysis results.
  /// Contains either a valid Expr* or an error marker.
  class ExprResult {
    Expr *Val = nullptr;
    bool Invalid = false;
  
  public:
    ExprResult() = default;
    ExprResult(Expr *E) : Val(E) {}
  
    static ExprResult getInvalid() {
      ExprResult R;
      R.Invalid = true;
      return R;
    }
  
    bool isInvalid() const { return Invalid; }
    bool isUsable() const { return Val != nullptr && !Invalid; }
    Expr *get() const { return Val; }
    explicit operator bool() const { return isUsable(); }
  };
  
  /// StmtResult - Wrapper for statement semantic analysis results.
  class StmtResult {
    Stmt *Val = nullptr;
    bool Invalid = false;
  
  public:
    StmtResult() = default;
    StmtResult(Stmt *S) : Val(S) {}
  
    static StmtResult getInvalid() {
      StmtResult R;
      R.Invalid = true;
      return R;
    }
  
    bool isInvalid() const { return Invalid; }
    bool isUsable() const { return Val != nullptr && !Invalid; }
    Stmt *get() const { return Val; }
    explicit operator bool() const { return isUsable(); }
  };
  
  /// DeclResult - Wrapper for declaration semantic analysis results.
  class DeclResult {
    Decl *Val = nullptr;
    bool Invalid = false;
  
  public:
    DeclResult() = default;
    DeclResult(Decl *D) : Val(D) {}
  
    static DeclResult getInvalid() {
      DeclResult R;
      R.Invalid = true;
      return R;
    }
  
    bool isInvalid() const { return Invalid; }
    bool isUsable() const { return Val != nullptr && !Invalid; }
    Decl *get() const { return Val; }
    explicit operator bool() const { return isUsable(); }
  };
  
  /// Sema - Semantic analysis engine.
  ///
  /// Coordinates all semantic analysis activities:
  /// - Name lookup and resolution
  /// - Type checking and conversion
  /// - Overload resolution
  /// - Access control checking
  /// - Template instantiation
  /// - Diagnostic emission
  class Sema {
    ASTContext &Context;
    DiagnosticsEngine &Diags;
    SymbolTable Symbols;
  
    /// Scope stack - tracks the current lexical scope chain.
    /// Uses the existing Scope class from Sema/Scope.h.
    Scope *CurrentScope = nullptr;
  
    /// Current DeclContext - tracks the current semantic context.
    DeclContext *CurContext = nullptr;
  
    /// Current function being analyzed (for return type checking).
    FunctionDecl *CurFunction = nullptr;
  
    /// Translation unit being processed.
    TranslationUnitDecl *CurTU = nullptr;
  
  public:
    Sema(ASTContext &C, DiagnosticsEngine &D);
    ~Sema();
  
    // Non-copyable
    Sema(const Sema &) = delete;
    Sema &operator=(const Sema &) = delete;
  
    //===------------------------------------------------------------------===//
    // Accessors
    //===------------------------------------------------------------------===//
  
    ASTContext &getASTContext() const { return Context; }
    DiagnosticsEngine &getDiagnostics() const { return Diags; }
    bool hasErrorOccurred() const { return Diags.hasErrorOccurred(); }
  
    //===------------------------------------------------------------------===//
    // Scope management (uses existing Scope class)
    //===------------------------------------------------------------------===//
  
    void PushScope(ScopeFlags Flags);
    void PopScope();
    Scope *getCurrentScope() const { return CurrentScope; }
  
    //===------------------------------------------------------------------===//
    // DeclContext management (uses existing DeclContext class)
    //===------------------------------------------------------------------===//
  
    DeclContext *getCurrentContext() const { return CurContext; }
    void PushDeclContext(DeclContext *DC);
    void PopDeclContext();
    void setCurContext(DeclContext *DC) { CurContext = DC; }
  
    //===------------------------------------------------------------------===//
    // Translation unit
    //===------------------------------------------------------------------===//
  
    TranslationUnitDecl *getCurTranslationUnitDecl() const { return CurTU; }
    void ActOnTranslationUnit(TranslationUnitDecl *TU);
  
    //===------------------------------------------------------------------===//
    // Declaration handling (ActOnXXX pattern, following Clang)
    //===------------------------------------------------------------------===//
  
    /// Act on a declarator after parsing completes.
    /// Creates the appropriate Decl node and adds it to the symbol table.
    DeclResult ActOnDeclarator(Decl *D);
  
    /// Act on a completed declaration.
    void ActOnFinishDecl(Decl *D);
  
    /// Act on a variable declaration.
    DeclResult ActOnVarDecl(SourceLocation Loc, llvm::StringRef Name,
                            QualType T, Expr *Init = nullptr);
  
    /// Act on a function declaration.
    DeclResult ActOnFunctionDecl(SourceLocation Loc, llvm::StringRef Name,
                                 QualType T,
                                 llvm::ArrayRef<ParmVarDecl *> Params,
                                 Stmt *Body = nullptr);
  
    /// Act on starting a function definition.
    void ActOnStartOfFunctionDef(FunctionDecl *FD);
  
    /// Act on completing a function definition.
    void ActOnFinishOfFunctionDef(FunctionDecl *FD);
  
    //===------------------------------------------------------------------===//
    // Expression handling
    //===------------------------------------------------------------------===//
  
    /// Act on a parsed expression. Performs type deduction.
    ExprResult ActOnExpr(Expr *E);
  
    /// Act on a function call expression.
    ExprResult ActOnCallExpr(Expr *Fn, llvm::ArrayRef<Expr *> Args,
                             SourceLocation LParenLoc,
                             SourceLocation RParenLoc);
  
    /// Act on a member access expression.
    ExprResult ActOnMemberExpr(Expr *Base, llvm::StringRef Member,
                               SourceLocation MemberLoc, bool IsArrow);
  
    /// Act on a binary operator expression.
    ExprResult ActOnBinaryOperator(Expr *LHS, Expr *RHS,
                                   SourceLocation OpLoc);
  
    /// Act on a unary operator expression.
    ExprResult ActOnUnaryOperator(Expr *Operand, SourceLocation OpLoc);
  
    /// Act on a cast expression.
    ExprResult ActOnCastExpr(QualType TargetType, Expr *E,
                             SourceLocation LParenLoc,
                             SourceLocation RParenLoc);
  
    /// Act on an array subscript expression.
    ExprResult ActOnArraySubscriptExpr(Expr *Base,
                                       llvm::ArrayRef<Expr *> Indices,
                                       SourceLocation LLoc,
                                       SourceLocation RLoc);
  
    /// Act on a conditional operator.
    ExprResult ActOnConditionalExpr(Expr *Cond, Expr *Then, Expr *Else,
                                    SourceLocation QuestionLoc,
                                    SourceLocation ColonLoc);
  
    //===------------------------------------------------------------------===//
    // Statement handling
    //===------------------------------------------------------------------===//
  
    StmtResult ActOnReturnStmt(Expr *RetVal, SourceLocation ReturnLoc);
    StmtResult ActOnIfStmt(Expr *Cond, Stmt *Then, Stmt *Else,
                           SourceLocation IfLoc);
    StmtResult ActOnWhileStmt(Expr *Cond, Stmt *Body,
                              SourceLocation WhileLoc);
    StmtResult ActOnForStmt(Stmt *Init, Expr *Cond, Expr *Inc, Stmt *Body,
                            SourceLocation ForLoc);
    StmtResult ActOnDoStmt(Expr *Cond, Stmt *Body, SourceLocation DoLoc);
    StmtResult ActOnSwitchStmt(Expr *Cond, Stmt *Body,
                               SourceLocation SwitchLoc);
    StmtResult ActOnCaseStmt(Expr *Val, Stmt *Body, SourceLocation CaseLoc);
    StmtResult ActOnDefaultStmt(Stmt *Body, SourceLocation DefaultLoc);
    StmtResult ActOnBreakStmt(SourceLocation BreakLoc);
    StmtResult ActOnContinueStmt(SourceLocation ContinueLoc);
    StmtResult ActOnGotoStmt(llvm::StringRef Label, SourceLocation GotoLoc);
    StmtResult ActOnCompoundStmt(llvm::ArrayRef<Stmt *> Stmts,
                                 SourceLocation LBraceLoc,
                                 SourceLocation RBraceLoc);
    StmtResult ActOnDeclStmt(Decl *D);
    StmtResult ActOnNullStmt(SourceLocation Loc);
  
    //===------------------------------------------------------------------===//
    // Type handling
    //===------------------------------------------------------------------===//
  
    /// Check if a type is complete (has a definition).
    bool isCompleteType(QualType T) const;
  
    /// Require a complete type. Emits error if incomplete.
    bool RequireCompleteType(QualType T, SourceLocation Loc);
  
    /// Get the canonical type for comparison.
    QualType getCanonicalType(QualType T) const;
  
    //===------------------------------------------------------------------===//
    // Diagnostics helpers
    //===------------------------------------------------------------------===//
  
    /// Emit a diagnostic at the given location.
    void Diag(SourceLocation Loc, DiagID ID);
    void Diag(SourceLocation Loc, DiagID ID, llvm::StringRef Extra);
  };
  
  } // namespace blocktype
  ```

- **E4.1.1.2** 实现 `src/Sema/Sema.cpp` 核心逻辑：
  ```cpp
  #include "blocktype/Sema/Sema.h"
  
  namespace blocktype {
  
  Sema::Sema(ASTContext &C, DiagnosticsEngine &D)
    : Context(C), Diags(D), Symbols(C) {
    // Initialize translation unit scope
    PushScope(ScopeFlags::TranslationUnitScope);
  }
  
  Sema::~Sema() {
    // Clean up scope stack
    while (CurrentScope && CurrentScope->getParent()) {
      PopScope();
    }
  }
  
  void Sema::PushScope(ScopeFlags Flags) {
    CurrentScope = new Scope(CurrentScope, Flags);
  }
  
  void Sema::PopScope() {
    if (!CurrentScope) return;
    Scope *Parent = CurrentScope->getParent();
    delete CurrentScope;
    CurrentScope = Parent;
  }
  
  void Sema::PushDeclContext(DeclContext *DC) {
    CurContext = DC;
  }
  
  void Sema::PopDeclContext() {
    if (CurContext) {
      CurContext = CurContext->getParent();
    }
  }
  
  void Sema::ActOnTranslationUnit(TranslationUnitDecl *TU) {
    CurTU = TU;
    CurContext = TU;
  }
  
  // ... ActOnXXX implementations follow in subsequent tasks
  
  } // namespace blocktype
  ```

**开发关键点提示：**
> 请为 BlockType 实现 Sema 主类。
>
> **核心职责**（参照 `docs/ARCHITECTURE.md` Sema 模块）：
> 1. 协调符号表、作用域、类型系统
> 2. 处理声明（变量、函数、类等）
> 3. 处理表达式（类型推导、重载决议）
> 4. 处理语句（控制流、跳转检查）
> 5. 发出语义错误诊断（使用已有的 DiagnosticsEngine）
>
> **与已有组件的关系**：
> - `Scope`（`Sema/Scope.h`）：已实现，直接使用。提供 `addDecl()`, `lookup()`, `lookupInScope()` 等
> - `DeclContext`（`AST/DeclContext.h`）：已实现，作为 AST 中的声明层级
> - `ASTContext`（`AST/ASTContext.h`）：已实现，提供 `create<T>()` 和类型工厂方法
> - `DiagnosticsEngine`（`Basic/Diagnostics.h`）：已实现，提供 `report(Loc, Level, Msg)` 和 `report(Loc, DiagID)`
>
> **ActOnXXX 方法模式**（参照 Clang Sema）：
> - Parser 解析后调用 Sema 的 ActOnXXX 方法
> - ActOn 方法执行语义检查，返回 ExprResult/StmtResult/DeclResult
> - 错误时返回 `XXXResult::getInvalid()`，不抛异常
> - 通过 `Diags.report()` 发出诊断
>
> **Scope 和 DeclContext 的区别**：
> - Scope：编译时的词法作用域栈，随 PushScope/PopScope 创建销毁
> - DeclContext：AST 中的声明层级（NamespaceDecl, CXXRecordDecl, FunctionDecl 等），持久存在于 AST 中

**Checkpoint：** Sema 类编译通过；PushScope/PopScope 与现有 Scope 协作正常

---

### Task 4.1.2 符号表实现

**目标：** 实现全局符号表，管理跨作用域的符号信息

**开发要点：**

- **E4.1.2.1** 创建 `include/blocktype/Sema/SymbolTable.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/Decl.h"
  #include "blocktype/AST/Type.h"
  #include "blocktype/Basic/LLVM.h"
  #include "llvm/ADT/StringMap.h"
  #include "llvm/ADT/SmallVector.h"
  #include "llvm/Support/raw_ostream.h"
  
  namespace blocktype {
  
  class ASTContext;
  
  /// SymbolTable - Manages global symbol information across all scopes.
  ///
  /// The SymbolTable provides fast lookup for declarations by name,
  /// organized by declaration kind (ordinary names, tags, typedefs,
  /// namespaces). It complements the Scope system by providing
  /// persistent storage that outlives individual scope lifetimes.
  ///
  /// Design follows Clang's IdentifierTable + multiple lookup maps pattern.
  class SymbolTable {
    ASTContext &Context;
  
    // Ordinary symbols: name → list of declarations (for overloading)
    llvm::StringMap<llvm::SmallVector<NamedDecl *, 4>> OrdinarySymbols;
  
    // Tags: class/struct/union/enum declarations
    llvm::StringMap<TagDecl *> Tags;
  
    // Typedefs: typedef and using-alias declarations
    llvm::StringMap<TypedefNameDecl *> Typedefs;
  
    // Namespaces
    llvm::StringMap<NamespaceDecl *> Namespaces;
  
    // Template names
    llvm::StringMap<TemplateDecl *> Templates;
  
    // Concepts
    llvm::StringMap<ConceptDecl *> Concepts;
  
  public:
    explicit SymbolTable(ASTContext &C) : Context(C) {}
  
    //===------------------------------------------------------------------===//
    // Symbol addition
    //===------------------------------------------------------------------===//
  
    /// Add an ordinary symbol (variable, function, etc.).
    /// Returns true on success, false on redefinition error.
    bool addOrdinarySymbol(NamedDecl *D);
  
    /// Add a tag declaration (class/struct/union/enum).
    bool addTagDecl(TagDecl *D);
  
    /// Add a typedef or type alias declaration.
    bool addTypedefDecl(TypedefNameDecl *D);
  
    /// Add a namespace declaration.
    void addNamespaceDecl(NamespaceDecl *D);
  
    /// Add a template declaration.
    void addTemplateDecl(TemplateDecl *D);
  
    /// Add a concept declaration.
    void addConceptDecl(ConceptDecl *D);
  
    /// Generic add - dispatches to the appropriate method based on Decl kind.
    bool addDecl(NamedDecl *D);
  
    //===------------------------------------------------------------------===//
    // Symbol lookup
    //===------------------------------------------------------------------===//
  
    /// Look up ordinary symbols (variables, functions).
    /// Returns all declarations with the given name (for overloading).
    llvm::ArrayRef<NamedDecl *> lookupOrdinary(llvm::StringRef Name) const;
  
    /// Look up a tag declaration (class/struct/union/enum).
    TagDecl *lookupTag(llvm::StringRef Name) const;
  
    /// Look up a typedef or type alias.
    TypedefNameDecl *lookupTypedef(llvm::StringRef Name) const;
  
    /// Look up a namespace.
    NamespaceDecl *lookupNamespace(llvm::StringRef Name) const;
  
    /// Look up a template.
    TemplateDecl *lookupTemplate(llvm::StringRef Name) const;
  
    /// Look up a concept.
    ConceptDecl *lookupConcept(llvm::StringRef Name) const;
  
    /// Generic lookup - returns any NamedDecl with the given name.
    /// Tries all categories in order: ordinary → tags → typedefs → namespaces.
    llvm::ArrayRef<NamedDecl *> lookup(llvm::StringRef Name) const;
  
    //===------------------------------------------------------------------===//
    // Queries
    //===------------------------------------------------------------------===//
  
    /// Check if a name exists in any category.
    bool contains(llvm::StringRef Name) const;
  
    /// Get the number of symbols in the table.
    size_t size() const;
  
    /// Dump the symbol table for debugging.
    void dump(llvm::raw_ostream &OS) const;
    void dump() const;
  };
  
  } // namespace blocktype
  ```

- **E4.1.2.2** 实现 `src/Sema/SymbolTable.cpp`：
  ```cpp
  #include "blocktype/Sema/SymbolTable.h"
  #include "blocktype/AST/ASTContext.h"
  
  namespace blocktype {
  
  bool SymbolTable::addOrdinarySymbol(NamedDecl *D) {
    llvm::StringRef Name = D->getName();
    auto &Decls = OrdinarySymbols[Name];
  
    // Allow function overloading
    if (isa<FunctionDecl>(D) || isa<CXXMethodDecl>(D)) {
      Decls.push_back(D);
      return true;
    }
  
    // Non-function: check for redefinition
    for (auto *Existing : Decls) {
      // Allow redeclaration of extern variables
      if (auto *ExistingVar = dyn_cast<VarDecl>(Existing)) {
        if (auto *NewVar = dyn_cast<VarDecl>(D)) {
          // TODO: Check extern/redeclaration rules
        }
      }
      // Otherwise it's a redefinition
      // Error is reported by caller via Diags
    }
  
    Decls.push_back(D);
    return true;
  }
  
  bool SymbolTable::addTagDecl(TagDecl *D) {
    llvm::StringRef Name = D->getName();
    if (Name.empty()) return true; // Anonymous tags
  
    auto It = Tags.find(Name);
    if (It != Tags.end()) {
      // Forward declarations are OK
      // TODO: Check if existing is just a forward declaration
      return true;
    }
    Tags[Name] = D;
    return true;
  }
  
  bool SymbolTable::addTypedefDecl(TypedefNameDecl *D) {
    llvm::StringRef Name = D->getName();
    auto It = Typedefs.find(Name);
    if (It != Typedefs.end()) {
      // C: redeclaration of typedef is OK (C11)
      // C++: redefinition is an error
      return false;
    }
    Typedefs[Name] = D;
    return true;
  }
  
  void SymbolTable::addNamespaceDecl(NamespaceDecl *D) {
    Namespaces[D->getName()] = D;
  }
  
  void SymbolTable::addTemplateDecl(TemplateDecl *D) {
    Templates[D->getName()] = D;
  }
  
  void SymbolTable::addConceptDecl(ConceptDecl *D) {
    Concepts[D->getName()] = D;
  }
  
  bool SymbolTable::addDecl(NamedDecl *D) {
    if (isa<TagDecl>(D))       return addTagDecl(cast<TagDecl>(D));
    if (isa<TypedefNameDecl>(D)) return addTypedefDecl(cast<TypedefNameDecl>(D));
    if (isa<NamespaceDecl>(D))   { addNamespaceDecl(cast<NamespaceDecl>(D)); return true; }
    if (isa<TemplateDecl>(D))    { addTemplateDecl(cast<TemplateDecl>(D)); return true; }
    if (isa<ConceptDecl>(D))     { addConceptDecl(cast<ConceptDecl>(D)); return true; }
    return addOrdinarySymbol(D);
  }
  
  llvm::ArrayRef<NamedDecl *> SymbolTable::lookupOrdinary(llvm::StringRef Name) const {
    auto It = OrdinarySymbols.find(Name);
    if (It != OrdinarySymbols.end()) return It->second;
    return {};
  }
  
  TagDecl *SymbolTable::lookupTag(llvm::StringRef Name) const {
    auto It = Tags.find(Name);
    return It != Tags.end() ? It->second : nullptr;
  }
  
  TypedefNameDecl *SymbolTable::lookupTypedef(llvm::StringRef Name) const {
    auto It = Typedefs.find(Name);
    return It != Typedefs.end() ? It->second : nullptr;
  }
  
  NamespaceDecl *SymbolTable::lookupNamespace(llvm::StringRef Name) const {
    auto It = Namespaces.find(Name);
    return It != Namespaces.end() ? It->second : nullptr;
  }
  
  TemplateDecl *SymbolTable::lookupTemplate(llvm::StringRef Name) const {
    auto It = Templates.find(Name);
    return It != Templates.end() ? It->second : nullptr;
  }
  
  ConceptDecl *SymbolTable::lookupConcept(llvm::StringRef Name) const {
    auto It = Concepts.find(Name);
    return It != Concepts.end() ? It->second : nullptr;
  }
  
  llvm::ArrayRef<NamedDecl *> SymbolTable::lookup(llvm::StringRef Name) const {
    if (auto Ord = lookupOrdinary(Name); !Ord.empty()) return Ord;
    // For single-result lookups, check tags/typedefs
    return {};
  }
  
  bool SymbolTable::contains(llvm::StringRef Name) const {
    return OrdinarySymbols.count(Name) || Tags.count(Name) ||
           Typedefs.count(Name) || Namespaces.count(Name) ||
           Templates.count(Name) || Concepts.count(Name);
  }
  
  size_t SymbolTable::size() const {
    return OrdinarySymbols.size() + Tags.size() + Typedefs.size() +
           Namespaces.size() + Templates.size() + Concepts.size();
  }
  
  } // namespace blocktype
  ```

**开发关键点提示：**
> **与 Scope 的分工**（参照 Clang）：
> - `Scope`（已实现）：编译时的词法作用域栈，管理局部变量。随着 `{` `}` 进出栈
> - `SymbolTable`（新建）：全局符号存储，管理所有已声明的符号。整个编译期间持久存在
> - 查找时先查 Scope（局部），再查 SymbolTable（全局）
>
> **重载处理**：
> - 函数允许同名（重载）：`OrdinarySymbols` 使用 `SmallVector<NamedDecl*, 4>`
> - 变量不允许重名（同一作用域）：由 Scope::addDecl() 检查
> - 类和变量可以同名（不同名字空间，C++ elaborated type specifier）
>
> **Tag 查找特殊性**（参照 Clang）：
> - `class X;` 前向声明和 `class X { ... };` 定义共存时，应保留定义
> - Tag 和 ordinary name 可以同名：`int X; class X;` 是合法的（不同查找类别）

**Checkpoint：** 符号表能正确添加和查找符号；重定义错误能正确报告

---

### Task 4.1.3 Scope 完善（已有基础）

**目标：** 确认现有 Scope 实现满足 Sema 需求，必要时补充

**开发要点：**

Scope 已在 `include/blocktype/Sema/Scope.h` 完整实现（198 行），包含：

- **ScopeFlags 枚举**：TranslationUnitScope, NamespaceScope, ClassScope, FunctionPrototypeScope, FunctionBodyScope, BlockScope, TemplateScope, ControlScope, SwitchScope, TryScope, ConditionScope, ForRangeScope（共 12 种）
- **Scope 类**：addDecl(), addDeclAllowRedeclaration(), lookupInScope(), lookup(), getEnclosingFunctionScope(), getEnclosingClassScope() 等
- **Scope.cpp**：实现了查找、添加等逻辑

**需要补充的功能：**

- **E4.1.3.1** 为 Scope 添加 using 指令支持（如果尚未实现）：
  ```cpp
  // 在 Scope 类中添加（如果尚未存在）：
  
  /// Using directives in this scope (using namespace X).
  llvm::SmallVector<NamespaceDecl *, 4> UsingDirectives;
  
  /// Add a using directive.
  void addUsingDirective(NamespaceDecl *NS) { UsingDirectives.push_back(NS); }
  
  /// Get using directives.
  llvm::ArrayRef<NamespaceDecl *> getUsingDirectives() const { return UsingDirectives; }
  ```

- **E4.1.3.2** 确认 Scope 的查找算法正确处理 using 指令

**Checkpoint：** Scope 完整支持 using 指令；名字查找在 using namespace 作用域中正确

---

### Task 4.1.4 DeclContext 集成

**目标：** 确保 DeclContext（已有实现）正确集成到 Sema 流程

**开发要点：**

DeclContext 已在 `include/blocktype/AST/DeclContext.h` 完整实现（181 行），包含：
- `DeclContextKind` 枚举：TranslationUnit, Namespace, CXXRecord, Function, Enum, LinkageSpec, Block, TemplateParams
- `addDecl(Decl*)`, `addDecl(NamedDecl*)`, `lookupInContext()`, `lookup()`
- `getParent()`, `getEnclosingContext()`
- 迭代器支持

以下 Decl 子类已继承 DeclContext：
- `TranslationUnitDecl : public Decl, public DeclContext`
- `NamespaceDecl : public NamedDecl, public DeclContext`
- `CXXRecordDecl : public RecordDecl, public DeclContext`
- `EnumDecl : public TagDecl, public DeclContext`
- `LinkageSpecDecl : public Decl, public DeclContext`

**需要确认的集成点：**

- **E4.1.4.1** Sema::ActOnVarDecl 等方法将声明添加到 CurContext
- **E4.1.4.2** 名字查找同时查 Scope 和 DeclContext
- **E4.1.4.3** PushDeclContext/PopDeclContext 正确维护 CurContext

**Checkpoint：** DeclContext 与 Sema 正确集成；类成员查找测试通过

---

## Stage 4.2 — 类型系统完善

### Task 4.2.1 类型工厂补全

**目标：** 扩展 ASTContext 的类型工厂，确保所有类型的创建接口完整

**开发要点：**

ASTContext 已有以下类型工厂方法（`include/blocktype/AST/ASTContext.h`）：
- `getBuiltinType(BuiltinKind)` — 22 种内建类型
- `getPointerType(const Type*)`, `getLValueReferenceType(const Type*)`, `getRValueReferenceType(const Type*)`
- `getConstantArrayType(const Type*, Expr*, APInt)`, `getIncompleteArrayType(const Type*)`, `getVariableArrayType(const Type*, Expr*)`
- `getFunctionType(const Type*, ArrayRef<const Type*>, bool)`
- `getTemplateTypeParmType(TemplateTypeParmDecl*, unsigned, unsigned, bool)`
- `getDependentType(const Type*, StringRef)`, `getUnresolvedType(StringRef)`
- `getTemplateSpecializationType(StringRef)`, `getElaboratedType(const Type*, StringRef)`
- `getDecltypeType(Expr*, QualType)`, `getMemberPointerType(const Type*, const Type*)`
- `getTypeDeclType(const TypeDecl*)`, `getAutoType()`

**需要补充的工厂方法：**

- **E4.2.1.1** 为 ASTContext 添加缺失的类型工厂：
  ```cpp
  // 在 ASTContext 中添加：
  
  /// Gets or creates a RecordType for the given RecordDecl.
  QualType getRecordType(RecordDecl *D);
  
  /// Gets or creates an EnumType for the given EnumDecl.
  QualType getEnumType(EnumDecl *D);
  
  /// Gets the built-in void type.
  QualType getVoidType();
  
  /// Gets the built-in bool type.
  QualType getBoolType();
  
  /// Gets the built-in int type.
  QualType getIntType();
  
  /// Gets the built-in float type.
  QualType getFloatType();
  
  /// Gets the built-in double type.
  QualType getDoubleType();
  
  /// Gets the built-in long type.
  QualType getLongType();
  
  /// Gets the nullptr_t type.
  QualType getNullPtrType();
  
  /// Gets a qualified type with the given CVR qualifiers.
  QualType getQualifiedType(const Type *T, Qualifier Q);
  
  /// Gets a pointer-to-member function type.
  QualType getMemberFunctionType(const Type *ReturnType,
                                  llvm::ArrayRef<const Type *> ParamTypes,
                                  const Type *ClassType,
                                  bool IsConst, bool IsVolatile,
                                  bool IsVariadic);
  ```

**Checkpoint：** 所有内建类型和派生类型的创建接口完整

---

### Task 4.2.2 类型推导（auto, decltype）

**目标：** 实现 auto 和 decltype 类型推导

**开发要点：**

- **E4.2.2.1** 创建 `include/blocktype/Sema/TypeDeduction.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/Type.h"
  #include "blocktype/AST/Expr.h"
  #include "blocktype/AST/ASTContext.h"
  
  namespace blocktype {
  
  /// TypeDeduction - Handles auto and decltype type deduction.
  ///
  /// Follows the C++ standard rules:
  /// - auto deduction: [dcl.spec.auto]
  /// - decltype deduction: [dcl.type.decltype]
  /// - decltype(auto) deduction: [dcl.spec.auto]
  class TypeDeduction {
    ASTContext &Context;
  
  public:
    explicit TypeDeduction(ASTContext &C) : Context(C) {}
  
    //===------------------------------------------------------------------===//
    // auto deduction
    //===------------------------------------------------------------------===//
  
    /// Deduce the type for `auto x = init;`.
    /// Rules (C++ [dcl.spec.auto]):
    /// 1. Strip top-level reference from init type
    /// 2. Strip top-level const/volatile (unless declared as `const auto`)
    /// 3. Array decays to pointer
    /// 4. Function decays to function pointer
    QualType deduceAutoType(QualType DeclaredType, Expr *Init);
  
    /// Deduce the type for `auto& x = init;`.
    QualType deduceAutoRefType(QualType DeclaredType, Expr *Init);
  
    /// Deduce the type for `auto&& x = init;` (forwarding reference).
    QualType deduceAutoForwardingRefType(Expr *Init);
  
    /// Deduce the type for `auto* x = &init;`.
    QualType deduceAutoPointerType(Expr *Init);
  
    /// Deduce return type for `auto f() { return expr; }`.
    QualType deduceReturnType(Expr *ReturnExpr);
  
    /// Deduce from initializer list `auto x = {1, 2, 3}`.
    QualType deduceFromInitList(llvm::ArrayRef<Expr *> Inits);
  
    //===------------------------------------------------------------------===//
    // decltype deduction
    //===------------------------------------------------------------------===//
  
    /// Deduce the type for `decltype(expr)`.
    /// Rules (C++ [dcl.type.decltype]):
    /// - decltype(id) → declared type of id
    /// - decltype(expr) → type of expr, preserving value category
    /// - decltype((id)) → reference to declared type of id
    QualType deduceDecltypeType(Expr *E);
  
    /// Deduce the type for `decltype(auto)`.
    QualType deduceDecltypeAutoType(Expr *E);
  
    //===------------------------------------------------------------------===//
    // Template argument deduction (placeholder for later)
    //===------------------------------------------------------------------===//
  
    /// Deduce template arguments from a function call.
    /// Returns true if deduction succeeded.
    bool deduceTemplateArguments(TemplateDecl *Template,
                                  llvm::ArrayRef<Expr *> Args,
                                  llvm::SmallVectorImpl<TemplateArgument> &DeducedArgs);
  };
  
  } // namespace blocktype
  ```

- **E4.2.2.2** 实现 auto 推导核心逻辑：
  ```cpp
  QualType TypeDeduction::deduceAutoType(QualType DeclaredType, Expr *Init) {
    if (!Init) return QualType();
  
    QualType T = Init->getType();  // Expr::getType() 已在 Expr.h 定义
  
    // 1. Strip reference
    if (T->isReferenceType()) {
      if (auto *LRef = dyn_cast<LValueReferenceType>(T.getTypePtr()))
        T = QualType(LRef->getReferencedType(), T.getQualifiers());
      else if (auto *RRef = dyn_cast<RValueReferenceType>(T.getTypePtr()))
        T = QualType(RRef->getReferencedType(), T.getQualifiers());
    }
  
    // 2. Strip top-level const/volatile (auto drops top-level CV)
    //    Unless declared as const auto
    if (!DeclaredType.isConstQualified()) {
      T = T.withoutConstQualifier().withoutVolatileQualifier();
    }
  
    // 3. Array decay
    if (T->isArrayType()) {
      const Type *ElemType = nullptr;
      if (auto *CA = dyn_cast<ConstantArrayType>(T.getTypePtr()))
        ElemType = CA->getElementType();
      else if (auto *IA = dyn_cast<IncompleteArrayType>(T.getTypePtr()))
        ElemType = IA->getElementType();
      if (ElemType)
        T = QualType(Context.getPointerType(ElemType), Qualifier::None);
    }
  
    // 4. Function decay
    if (T->isFunctionType()) {
      T = QualType(Context.getPointerType(T.getTypePtr()), Qualifier::None);
    }
  
    return T;
  }
  ```

**开发关键点提示：**
> **auto 推导规则**（C++ [dcl.spec.auto]）：
> - `auto x = expr;` → 推导为 expr 类型（去除引用和顶层 CV）
> - `auto& x = expr;` → 推导为 expr 的引用类型（保留 CV）
> - `auto* x = &expr;` → 推导为指针类型
> - `const auto x = expr;` → 推导为 const T
> - `auto&& x = expr;` → 转发引用（universal reference）
>
> **decltype 推导规则**（C++ [dcl.type.decltype]）：
> - `decltype(id)` → id 的声明类型
> - `decltype(expr)` → expr 的类型（保留值类别）
> - `decltype((id))` → id 的引用类型
>
> **与现有类型系统的交互**：
> - 使用 `QualType::getTypePtr()` 获取底层 `Type*`
> - 使用 `dyn_cast<LValueReferenceType>()` 等（classof 已全部实现）
> - 使用 `ASTContext::getPointerType()` 等工厂方法创建类型

**Checkpoint：** auto 和 decltype 推导测试通过；常见推导场景正确

---

### Task 4.2.3 类型完整性检查

**目标：** 实现类型完整性检查和不完整类型处理

**开发要点：**

- **E4.2.3.1** 在 Sema 中实现类型完整性检查：
  ```cpp
  bool Sema::isCompleteType(QualType T) const {
    if (T.isNull()) return false;
  
    const Type *Ty = T.getTypePtr();
  
    // Builtin types are always complete
    if (Ty->isBuiltinType()) return true;
  
    // Pointer/reference types: pointee need not be complete
    if (Ty->isPointerType() || Ty->isReferenceType()) return true;
  
    // Array types: element must be complete (except incomplete arrays)
    if (auto *AT = dyn_cast<ArrayType>(Ty)) {
      return isCompleteType(QualType(AT->getElementType(), Qualifier::None));
    }
    if (isa<IncompleteArrayType>(Ty)) return false;
  
    // Function types are always complete
    if (Ty->isFunctionType()) return true;
  
    // Record types: must have a definition
    if (auto *RT = dyn_cast<RecordType>(Ty)) {
      // Check if the RecordDecl has a body
      if (auto *RD = dyn_cast<CXXRecordDecl>(RT->getDecl()))
        return !RD->members().empty() || RD->getNumBases() > 0;
      return false; // Without definition, it's incomplete
    }
  
    // Enum types: must have a definition
    if (auto *ET = dyn_cast<EnumType>(Ty)) {
      return !ET->getDecl()->enumerators().empty();
    }
  
    // Void is never complete
    if (Ty->isVoidType()) return false;
  
    return true;
  }
  ```

**Checkpoint：** isCompleteType() 对所有已有 Type 子类工作正确

---

## Stage 4.3 — 名字查找

### Task 4.3.1 Lookup 基础设施

**目标：** 实现名字查找的基础数据结构和接口

**开发要点：**

- **E4.3.1.1** 创建 `include/blocktype/Sema/Lookup.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/Decl.h"
  #include "blocktype/Sema/Scope.h"
  #include "llvm/ADT/SmallVector.h"
  
  namespace blocktype {
  
  /// LookupNameKind - The kind of name lookup being performed.
  ///
  /// Different lookup kinds follow different rules, e.g., tag lookup
  /// and ordinary name lookup can return different results for the
  /// same name (C++ allows `int X; class X;` in the same scope).
  enum class LookupNameKind {
    /// Ordinary name lookup: variables, functions, enumerators, etc.
    LookupOrdinaryName,
  
    /// Tag name lookup: class, struct, union, enum.
    LookupTagName,
  
    /// Member name lookup: class/struct members.
    LookupMemberName,
  
    /// Operator name lookup: operator overloading.
    LookupOperatorName,
  
    /// Namespace name lookup.
    LookupNamespaceName,
  
    /// Type name lookup: any type (class, enum, typedef, template).
    LookupTypeName,
  
    /// Concept name lookup (C++20).
    LookupConceptName,
  };
  
  /// LookupResult - Result of a name lookup operation.
  ///
  /// Can contain:
  /// - No results (name not found)
  /// - A single result (unique declaration)
  /// - Multiple results (overloaded functions)
  /// - Ambiguous results (error)
  class LookupResult {
    llvm::SmallVector<NamedDecl *, 4> Decls;
    bool Ambiguous = false;
    bool TypeName = false;       // Found result is a type name
    bool Overloaded = false;     // Found multiple function declarations
  
  public:
    LookupResult() = default;
  
    /// Construct with a single result.
    explicit LookupResult(NamedDecl *D) { Decls.push_back(D); }
  
    //===------------------------------------------------------------------===//
    // Adding results
    //===------------------------------------------------------------------===//
  
    /// Add a declaration to the result set.
    void addDecl(NamedDecl *D);
  
    /// Add all declarations from another result.
    void addAllDecls(const LookupResult &Other);
  
    /// Mark as ambiguous.
    void setAmbiguous(bool A = true) { Ambiguous = A; }
  
    /// Mark that the result is a type name.
    void setTypeName(bool T = true) { TypeName = T; }
  
    /// Mark that the result is overloaded.
    void setOverloaded(bool O = true) { Overloaded = O; }
  
    //===------------------------------------------------------------------===//
    // Result queries
    //===------------------------------------------------------------------===//
  
    /// Is the result empty (no declarations found)?
    bool empty() const { return Decls.empty(); }
  
    /// Is there exactly one result?
    bool isSingleResult() const { return Decls.size() == 1; }
  
    /// Is the result ambiguous?
    bool isAmbiguous() const { return Ambiguous; }
  
    /// Is this an overloaded result (multiple functions)?
    bool isOverloaded() const { return Overloaded; }
  
    /// Is the result a type name?
    bool isTypeName() const { return TypeName; }
  
    //===------------------------------------------------------------------===//
    // Result access
    //===------------------------------------------------------------------===//
  
    /// Get the first (or only) declaration.
    NamedDecl *getFoundDecl() const {
      return Decls.empty() ? nullptr : Decls.front();
    }
  
    /// Get all declarations.
    llvm::ArrayRef<NamedDecl *> getDecls() const { return Decls; }
  
    /// Get the number of declarations.
    unsigned getNumDecls() const { return Decls.size(); }
  
    /// Get declaration at index.
    NamedDecl *operator[](unsigned I) const { return Decls[I]; }
  
    /// Resolve to a single function declaration (for non-overloaded calls).
    /// Returns nullptr if ambiguous or not found.
    FunctionDecl *getAsFunction() const;
  
    /// Resolve to a single type declaration.
    TypeDecl *getAsTypeDecl() const;
  
    /// Resolve to a single tag declaration.
    TagDecl *getAsTagDecl() const;
  
    /// Clear all results.
    void clear() { Decls.clear(); Ambiguous = false; }
  };
  
  /// NestedNameSpecifier - Represents a C++ nested-name-specifier.
  ///
  /// Example: For `A::B::C`, the NestedNameSpecifier is A::B::
  /// Each component is either a namespace, a type, or the global specifier.
  class NestedNameSpecifier {
  public:
    enum SpecifierKind {
      /// Global scope: ::
      Global,
  
      /// Namespace: Ns::
      Namespace,
  
      /// Type (class/struct/enum/typedef): Type::
      TypeSpec,
  
      /// Template type: Template<T>::
      TemplateTypeSpec,
  
      /// Identifier (unresolved): Ident::
      Identifier,
    };
  
  private:
    SpecifierKind Kind;
    union {
      NamespaceDecl *Namespace;
      const blocktype::Type *TypeSpec;
      llvm::StringRef Identifier;
    } Data;
    NestedNameSpecifier *Prefix;
  
  public:
    // Static factory methods
    static NestedNameSpecifier *CreateGlobalSpecifier();
    static NestedNameSpecifier *Create(ASTContext &Ctx,
                                        NestedNameSpecifier *Prefix,
                                        NamespaceDecl *NS);
    static NestedNameSpecifier *Create(ASTContext &Ctx,
                                        NestedNameSpecifier *Prefix,
                                        const blocktype::Type *T);
    static NestedNameSpecifier *Create(ASTContext &Ctx,
                                        NestedNameSpecifier *Prefix,
                                        llvm::StringRef Identifier);
  
    // Accessors
    SpecifierKind getKind() const { return Kind; }
    NestedNameSpecifier *getPrefix() const { return Prefix; }
  
    NamespaceDecl *getAsNamespace() const {
      return Kind == Namespace ? Data.Namespace : nullptr;
    }
    const blocktype::Type *getAsType() const {
      return Kind == TypeSpec || Kind == TemplateTypeSpec ? Data.TypeSpec : nullptr;
    }
    llvm::StringRef getAsIdentifier() const {
      return Kind == Identifier ? Data.Identifier : llvm::StringRef();
    }
  
    /// Convert to string representation (for diagnostics).
    std::string getAsString() const;
  };
  
  } // namespace blocktype
  ```

- **E4.3.1.2** 实现 `src/Sema/Lookup.cpp` 中 LookupResult 的方法

**Checkpoint：** LookupResult 和 NestedNameSpecifier 编译通过

---

### Task 4.3.2 Unqualified Lookup

**目标：** 实现非限定名字查找

**开发要点：**

- **E4.3.2.1** 在 `src/Sema/Lookup.cpp` 中实现 Unqualified Lookup：
  ```cpp
  LookupResult Sema::LookupUnqualifiedName(llvm::StringRef Name, Scope *S,
                                            LookupNameKind Kind) {
    LookupResult Result;
  
    // 从当前作用域向上查找
    for (Scope *Cur = S; Cur; Cur = Cur->getParent()) {
      // 在当前作用域查找
      if (NamedDecl *D = Cur->lookupInScope(Name)) {
        Result.addDecl(D);
  
        // 标签查找或非函数声明：找到第一个就返回
        if (Kind == LookupNameKind::LookupTagName) {
          if (isa<TagDecl>(D)) return Result;
          continue; // Skip non-tags in tag lookup
        }
  
        if (!isa<FunctionDecl>(D) && !isa<CXXMethodDecl>(D)) {
          return Result; // Non-function: first match wins
        }
        // Function: continue collecting overloads
      }
  
      // 处理 using 指令
      for (NamespaceDecl *NS : Cur->getUsingDirectives()) {
        if (auto *D = NS->getDeclContext()
                          ? cast<NamedDecl>(NS->getDeclContext()->lookup(Name))
                          : nullptr) {
          Result.addDecl(D);
        }
      }
    }
  
    // 在全局符号表中查找
    if (Result.empty()) {
      for (NamedDecl *D : Symbols.lookupOrdinary(Name)) {
        Result.addDecl(D);
      }
    }
  
    if (Result.getNumDecls() > 1) {
      Result.setOverloaded(true);
    }
  
    return Result;
  }
  ```

**开发关键点提示：**
> **查找顺序**（参照 Clang Sema::LookupName）：
> 1. 当前块作用域
> 2. 外围块作用域
> 3. 函数参数作用域
> 4. 类作用域（如果是成员函数）
> 5. 命名空间作用域
> 6. 全局作用域
>
> **与已有组件的协作**：
> - 使用 `Scope::lookupInScope()` 只在当前作用域查找
> - 使用 `Scope::lookup()` 递归查找（已在 Scope.cpp 实现）
> - 使用 `SymbolTable::lookupOrdinary()` 查找全局符号
> - 使用 `DeclContext::lookup()` 查找命名空间/类成员

**Checkpoint：** 名字查找测试通过；作用域链正确

---

### Task 4.3.3 Qualified Lookup

**目标：** 实现限定名字查找（`X::name`）

**开发要点：**

- **E4.3.3.1** 实现限定查找：
  ```cpp
  LookupResult Sema::LookupQualifiedName(llvm::StringRef Name,
                                           NestedNameSpecifier *NNS) {
    LookupResult Result;
  
    // 解析 NestedNameSpecifier 到 DeclContext
    DeclContext *DC = nullptr;
  
    if (NNS->getKind() == NestedNameSpecifier::Global) {
      DC = CurTU;
    } else if (NNS->getKind() == NestedNameSpecifier::Namespace) {
      DC = NNS->getAsNamespace();
    } else if (NNS->getKind() == NestedNameSpecifier::TypeSpec) {
      // Class/struct/union: lookup in record
      if (auto *RT = dyn_cast<RecordType>(NNS->getAsType())) {
        if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
          DC = CXXRD->getDeclContext();
        }
      }
    }
  
    if (!DC) return Result;
  
    // 在指定上下文中查找
    if (NamedDecl *D = DC->lookup(Name)) {
      Result.addDecl(D);
    }
  
    return Result;
  }
  ```

**Checkpoint：** 限定查找测试通过；`::name`, `NS::name`, `Class::name` 正确

---

### Task 4.3.4 ADL（参数依赖查找）

**目标：** 实现参数依赖查找

**开发要点：**

- **E4.3.4.1** 在 `src/Sema/Lookup.cpp` 中实现 ADL：
  ```cpp
  void Sema::LookupADL(llvm::StringRef Name,
                        llvm::ArrayRef<Expr *> Args,
                        LookupResult &Result) {
    // 收集所有参数的关联命名空间和类
    llvm::SmallPtrSet<NamespaceDecl *, 8> AssociatedNamespaces;
    llvm::SmallPtrSet<const RecordType *, 8> AssociatedClasses;
  
    for (Expr *Arg : Args) {
      CollectAssociatedNamespacesAndClasses(Arg->getType(),
                                             AssociatedNamespaces,
                                             AssociatedClasses);
    }
  
    // 在每个关联命名空间中查找
    for (NamespaceDecl *NS : AssociatedNamespaces) {
      if (NamedDecl *D = NS->getDeclContext()
                            ? cast_or_null<NamedDecl>(NS->getDeclContext()->lookup(Name))
                            : nullptr) {
        Result.addDecl(D);
      }
    }
  
    // 在关联类的成员中查找友元函数
    for (const RecordType *RT : AssociatedClasses) {
      if (auto *CXXRD = dyn_cast_or_null<CXXRecordDecl>(RT->getDecl())) {
        // Search friend functions declared in the class
        // TODO: implement friend function lookup
      }
    }
  }
  
  void Sema::CollectAssociatedNamespacesAndClasses(
      QualType T,
      llvm::SmallPtrSetImpl<NamespaceDecl *> &Namespaces,
      llvm::SmallPtrSetImpl<const RecordType *> &Classes) {
    // Class type: class's namespace + base classes
    if (auto *RT = dyn_cast<RecordType>(T.getTypePtr())) {
      Classes.insert(RT);
      // Find the enclosing namespace
      if (auto *D = RT->getDecl()) {
        DeclContext *Ctx = D->getDeclContext()
            ? cast<DeclContext>(D)->getParent() : nullptr;
        while (Ctx) {
          if (Ctx->isNamespace()) {
            Namespaces.insert(cast<NamespaceDecl>(
                static_cast<Decl *>(Ctx)));
            break;
          }
          Ctx = Ctx->getParent();
        }
      }
    }
    // Pointer/reference: recurse into pointee
    if (T->isPointerType()) {
      CollectAssociatedNamespacesAndClasses(
          QualType(cast<PointerType>(T.getTypePtr())->getPointeeType(), Qualifier::None),
          Namespaces, Classes);
    }
    if (T->isReferenceType()) {
      CollectAssociatedNamespacesAndClasses(
          QualType(cast<ReferenceType>(T.getTypePtr())->getReferencedType(), Qualifier::None),
          Namespaces, Classes);
    }
    // Enum type: enum's namespace
    if (auto *ET = dyn_cast<EnumType>(T.getTypePtr())) {
      DeclContext *Ctx = ET->getDecl()
          ? ET->getDecl()->getDeclContext() : nullptr;
      while (Ctx) {
        if (Ctx->isNamespace()) {
          Namespaces.insert(cast<NamespaceDecl>(static_cast<Decl *>(Ctx)));
          break;
        }
        Ctx = Ctx->getParent();
      }
    }
  }
  ```

**Checkpoint：** ADL 测试通过；`std::swap` 等典型用例正确

---

## Stage 4.4 — 重载决议

### Task 4.4.1 重载候选集

**目标：** 实现重载候选收集和管理

**开发要点：**

- **E4.4.1.1** 创建 `include/blocktype/Sema/Overload.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/Decl.h"
  #include "blocktype/AST/Expr.h"
  #include "blocktype/AST/Type.h"
  #include "blocktype/Sema/Conversion.h"
  #include "llvm/ADT/SmallVector.h"
  
  namespace blocktype {
  
  /// OverloadCandidate - A single candidate in overload resolution.
  class OverloadCandidate {
    FunctionDecl *Function;
  
    /// Computed conversion rank for each argument.
    llvm::SmallVector<ConversionRank, 4> ArgRanks;
  
    /// Whether this candidate is viable (all conversions possible).
    bool Viable = false;
  
    /// Failure reason (for error diagnostics).
    llvm::StringRef FailureReason;
  
  public:
    explicit OverloadCandidate(FunctionDecl *F) : Function(F) {}
  
    FunctionDecl *getFunction() const { return Function; }
    bool isViable() const { return Viable; }
    void setViable(bool V) { Viable = V; }
  
    llvm::ArrayRef<ConversionRank> getArgRanks() const { return ArgRanks; }
    void addArgRank(ConversionRank R) { ArgRanks.push_back(R); }
  
    llvm::StringRef getFailureReason() const { return FailureReason; }
    void setFailureReason(llvm::StringRef Reason) { FailureReason = Reason; }
  
    /// Check argument count and conversions.
    bool checkViability(llvm::ArrayRef<Expr *> Args);
  
    /// Compare this candidate with another.
    /// Returns:
    ///  - <0 if this is better
    ///  - 0 if indistinguishable
    ///  - >0 if Other is better
    int compare(const OverloadCandidate &Other) const;
  };
  
  /// OverloadCandidateSet - A set of candidates for overload resolution.
  class OverloadCandidateSet {
    SourceLocation CallLoc;
    llvm::SmallVector<OverloadCandidate, 16> Candidates;
  
  public:
    explicit OverloadCandidateSet(SourceLocation Loc) : CallLoc(Loc) {}
  
    /// Add a candidate function.
    OverloadCandidate &addCandidate(FunctionDecl *F);
  
    /// Add all functions from a LookupResult.
    void addCandidates(const LookupResult &R);
  
    /// Get all candidates.
    llvm::ArrayRef<OverloadCandidate> getCandidates() const { return Candidates; }
  
    /// Get viable candidates only.
    llvm::SmallVector<OverloadCandidate *, 4> getViableCandidates() const;
  
    /// Resolve overload: find the best matching function.
    /// Returns nullptr if no match or ambiguous.
    FunctionDecl *resolve(llvm::ArrayRef<Expr *> Args);
  
    /// Get the number of candidates.
    unsigned size() const { return Candidates.size(); }
    bool empty() const { return Candidates.empty(); }
  };
  
  } // namespace blocktype
  ```

**Checkpoint：** 候选收集测试通过；函数模板实例化正确

---

### Task 4.4.2 隐式转换与排序

**目标：** 实现参数转换检查和候选排序

**开发要点：**

- **E4.4.2.1** 创建 `include/blocktype/Sema/Conversion.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/Type.h"
  #include "blocktype/Basic/SourceLocation.h"
  #include "llvm/ADT/SmallVector.h"
  
  namespace blocktype {
  
  /// ConversionRank - The rank of an implicit conversion sequence.
  ///
  /// Following the C++ standard [over.ics.rank]:
  /// - ExactMatch: no conversion needed (or only qualification adjustment)
  /// - Promotion: integral promotion (char→int, float→double)
  /// - Conversion: standard conversion (int→long, derived→base pointer)
  /// - UserDefined: user-defined conversion (constructor or conversion op)
  /// - Ellipsis: match via `...`
  /// - BadConversion: no valid conversion exists
  enum class ConversionRank : unsigned {
    ExactMatch   = 0,
    Promotion    = 1,
    Conversion   = 2,
    UserDefined  = 3,
    Ellipsis     = 4,
    BadConversion = 5,
  };
  
  /// StandardConversionKind - The three sub-ranks of a standard conversion.
  enum class StandardConversionKind {
    /// No conversion needed.
    Identity,
  
    /// Lvalue-to-rvalue conversion, function-to-pointer, array-to-pointer.
    LvalueTransformation,
  
    /// Integral promotion or floating-point promotion.
    Promotion,
  
    /// Integral conversion, floating-point conversion, floating-integral,
    /// pointer conversion, pointer-to-member conversion, boolean conversion.
    Conversion,
  
    /// Qualification adjustment (adding const/volatile).
    QualificationAdjustment,
  };
  
  /// StandardConversionSequence - A standard conversion sequence.
  ///
  /// A standard conversion sequence consists of up to three conversions:
  /// 1. Lvalue transformation
  /// 2. Qualification adjustment
  /// 3. Promotion or conversion
  class StandardConversionSequence {
    StandardConversionKind First = StandardConversionKind::Identity;
    StandardConversionKind Second = StandardConversionKind::Identity;
    StandardConversionKind Third = StandardConversionKind::Identity;
    ConversionRank Rank = ConversionRank::ExactMatch;
  
  public:
    StandardConversionSequence() = default;
  
    void setFirst(StandardConversionKind K) { First = K; }
    void setSecond(StandardConversionKind K) { Second = K; }
    void setThird(StandardConversionKind K) { Third = K; }
    void setRank(ConversionRank R) { Rank = R; }
  
    ConversionRank getRank() const { return Rank; }
    StandardConversionKind getFirst() const { return First; }
    StandardConversionKind getSecond() const { return Second; }
    StandardConversionKind getThird() const { return Third; }
  
    bool isBad() const { return Rank == ConversionRank::BadConversion; }
  
    /// Compare two standard conversion sequences.
    /// Returns <0 if this is better, 0 if equal, >0 if Other is better.
    int compare(const StandardConversionSequence &Other) const;
  };
  
  /// ImplicitConversionSequence - A complete implicit conversion sequence.
  ///
  /// Either a standard conversion sequence, a user-defined conversion,
  /// or an ellipsis conversion.
  class ImplicitConversionSequence {
  public:
    enum ConversionKind {
      /// Standard conversion sequence only.
      StandardConversion,
  
      /// User-defined conversion (constructor or conversion operator)
      /// followed by a standard conversion.
      UserDefinedConversion,
  
      /// Ellipsis conversion (...).
      EllipsisConversion,
  
      /// No valid conversion.
      BadConversion,
    };
  
  private:
    ConversionKind Kind = BadConversion;
    StandardConversionSequence Standard;
    FunctionDecl *UserDefinedConverter = nullptr;
    ConversionRank Rank = ConversionRank::BadConversion;
  
  public:
    ImplicitConversionSequence() = default;
  
    static ImplicitConversionSequence getStandard(StandardConversionSequence SCS) {
      ImplicitConversionSequence ICS;
      ICS.Kind = StandardConversion;
      ICS.Standard = SCS;
      ICS.Rank = SCS.getRank();
      return ICS;
    }
  
    static ImplicitConversionSequence getUserDefined(FunctionDecl *Converter,
                                                      StandardConversionSequence SCS) {
      ImplicitConversionSequence ICS;
      ICS.Kind = UserDefinedConversion;
      ICS.UserDefinedConverter = Converter;
      ICS.Standard = SCS;
      ICS.Rank = ConversionRank::UserDefined;
      return ICS;
    }
  
    static ImplicitConversionSequence getEllipsis() {
      ImplicitConversionSequence ICS;
      ICS.Kind = EllipsisConversion;
      ICS.Rank = ConversionRank::Ellipsis;
      return ICS;
    }
  
    static ImplicitConversionSequence getBad() {
      return ImplicitConversionSequence();
    }
  
    ConversionKind getKind() const { return Kind; }
    ConversionRank getRank() const { return Rank; }
    bool isBad() const { return Kind == BadConversion; }
    const StandardConversionSequence &getStandard() const { return Standard; }
    FunctionDecl *getUserDefinedConverter() const { return UserDefinedConverter; }
  
    /// Compare two implicit conversion sequences.
    int compare(const ImplicitConversionSequence &Other) const;
  };
  
  /// ConversionChecker - Static utility for checking implicit conversions.
  class ConversionChecker {
  public:
    /// Get the implicit conversion sequence from From to To.
    static ImplicitConversionSequence GetConversion(QualType From, QualType To);
  
    /// Check if a standard conversion exists from From to To.
    static StandardConversionSequence GetStandardConversion(QualType From,
                                                              QualType To);
  
    /// Check if an integral promotion exists.
    static bool isIntegralPromotion(QualType From, QualType To);
  
    /// Check if a floating-point promotion exists.
    static bool isFloatingPointPromotion(QualType From, QualType To);
  
    /// Check if a standard conversion exists (not promotion).
    static bool isStandardConversion(QualType From, QualType To);
  
    /// Check if a qualification conversion exists.
    static bool isQualificationConversion(QualType From, QualType To);
  
    /// Check if a derived-to-base pointer conversion exists.
    static bool isDerivedToBaseConversion(QualType From, QualType To);
  };
  
  } // namespace blocktype
  ```

**Checkpoint：** 转换排序测试通过；歧义检测正确

---

### Task 4.4.3 最佳函数选择

**目标：** 实现最佳匹配函数的选择

**开发要点：**

- **E4.4.3.1** 实现重载决议核心算法（已在 Overload.h 声明，此处实现 `resolve()`）：
  ```cpp
  FunctionDecl *OverloadCandidateSet::resolve(llvm::ArrayRef<Expr *> Args) {
    // 1. Check viability of all candidates
    for (auto &C : Candidates) {
      C.checkViability(Args);
    }
  
    // 2. Collect viable candidates
    auto Viable = getViableCandidates();
  
    // 3. No viable candidates
    if (Viable.empty()) return nullptr;
  
    // 4. Single viable candidate
    if (Viable.size() == 1) return Viable[0]->getFunction();
  
    // 5. Multiple viable: find the best
    OverloadCandidate *Best = Viable[0];
    bool Ambiguous = false;
  
    for (unsigned I = 1; I < Viable.size(); ++I) {
      int Cmp = Best->compare(*Viable[I]);
      if (Cmp > 0) {
        Best = Viable[I];
        Ambiguous = false;
      } else if (Cmp == 0) {
        Ambiguous = true;
      }
    }
  
    if (Ambiguous) return nullptr;
    return Best->getFunction();
  }
  ```

**Checkpoint：** 重载决议测试通过；错误诊断完整

---

## Stage 4.5 — 语义检查

### Task 4.5.1 类型检查

**目标：** 实现完整的类型检查系统

**开发要点：**

- **E4.5.1.1** 创建 `include/blocktype/Sema/TypeCheck.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/Expr.h"
  #include "blocktype/AST/Type.h"
  #include "blocktype/Basic/Diagnostics.h"
  #include "blocktype/Basic/SourceLocation.h"
  
  namespace blocktype {
  
  /// TypeCheck - Performs type checking operations.
  ///
  /// This class provides static methods for checking type compatibility,
  /// initialization, assignment, and other type-related operations.
  class TypeCheck {
    DiagnosticsEngine &Diags;
  
  public:
    explicit TypeCheck(DiagnosticsEngine &D) : Diags(D) {}
  
    //===------------------------------------------------------------------===//
    // Assignment and initialization
    //===------------------------------------------------------------------===//
  
    /// Check assignment compatibility: LHS = RHS.
    bool CheckAssignment(QualType LHS, QualType RHS, SourceLocation Loc);
  
    /// Check initialization compatibility: T x = init.
    bool CheckInitialization(QualType Dest, Expr *Init, SourceLocation Loc);
  
    /// Check direct initialization: T x(args...).
    bool CheckDirectInitialization(QualType Dest,
                                    llvm::ArrayRef<Expr *> Args,
                                    SourceLocation Loc);
  
    /// Check list initialization: T x = {args...}.
    bool CheckListInitialization(QualType Dest,
                                  llvm::ArrayRef<Expr *> Args,
                                  SourceLocation Loc);
  
    /// Check reference binding: T& ref = expr.
    bool CheckReferenceBinding(QualType RefType, Expr *Init,
                                SourceLocation Loc);
  
    //===------------------------------------------------------------------===//
    // Function call checking
    //===------------------------------------------------------------------===//
  
    /// Check a function call's arguments against the function declaration.
    bool CheckCall(FunctionDecl *F, llvm::ArrayRef<Expr *> Args,
                   SourceLocation CallLoc);
  
    /// Check that a return statement's expression matches the function type.
    bool CheckReturn(Expr *RetVal, QualType FuncRetType,
                     SourceLocation ReturnLoc);
  
    /// Check a condition expression (if, while, for, etc.).
    bool CheckCondition(Expr *Cond, SourceLocation Loc);
  
    /// Check a case expression (must be integral constant).
    bool CheckCaseExpression(Expr *Val, SourceLocation Loc);
  
    //===------------------------------------------------------------------===//
    // Type compatibility
    //===------------------------------------------------------------------===//
  
    /// Check if From is convertible to To.
    bool isTypeCompatible(QualType From, QualType To) const;
  
    /// Check if two types are the same (ignoring CVR qualifiers).
    bool isSameType(QualType T1, QualType T2) const;
  
    /// Get the common type for binary operations (usual arithmetic conversions).
    QualType getCommonType(QualType T1, QualType T2) const;
  
    /// Get the result type of a binary operator.
    QualType getBinaryOperatorResultType(QualType LHS, QualType RHS) const;
  
    /// Get the result type of a unary operator.
    QualType getUnaryOperatorResultType(QualType Operand) const;
  
    /// Check if a type can be compared (operator<, >, ==, etc.).
    bool isComparable(QualType T) const;
  
    /// Check if a type can be called (function type, class with operator()).
    bool isCallable(QualType T) const;
  };
  
  } // namespace blocktype
  ```

**Checkpoint：** 类型检查测试通过

---

### Task 4.5.2 访问控制检查

**目标：** 实现访问控制检查

**开发要点：**

- **E4.5.2.1** 创建 `include/blocktype/Sema/AccessControl.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/Decl.h"
  #include "blocktype/AST/DeclContext.h"
  #include "blocktype/Basic/SourceLocation.h"
  
  namespace blocktype {
  
  /// AccessControl - Performs C++ access control checking.
  ///
  /// Access levels (defined in Decl.h):
  /// - AS_public: accessible from anywhere
  /// - AS_protected: accessible from the class and its derived classes
  /// - AS_private: accessible only from the class itself and friends
  ///
  /// The AccessSpecifier enum is already defined in Decl.h.
  class AccessControl {
  public:
    /// Check if `Member` is accessible from `AccessingContext`.
    ///
    /// \param Member The member being accessed.
    /// \param MemberAccess The declared access level of the member.
    /// \param AccessingContext The context from which access is attempted.
    /// \param ClassContext The class that declares the member.
    /// \return true if access is allowed, false otherwise.
    static bool isAccessible(NamedDecl *Member,
                              AccessSpecifier MemberAccess,
                              DeclContext *AccessingContext,
                              CXXRecordDecl *ClassContext);
  
    /// Check access for a member access expression (obj.member or ptr->member).
    static bool CheckMemberAccess(NamedDecl *Member,
                                   AccessSpecifier Access,
                                   CXXRecordDecl *MemberClass,
                                   DeclContext *AccessingContext,
                                   SourceLocation AccessLoc);
  
    /// Check access for a base class specifier.
    static bool CheckBaseClassAccess(CXXRecordDecl *Base,
                                      AccessSpecifier Access,
                                      CXXRecordDecl *Derived,
                                      SourceLocation AccessLoc);
  
    /// Check access for a friend declaration.
    static bool CheckFriendAccess(NamedDecl *Friend,
                                   CXXRecordDecl *Class,
                                   DeclContext *AccessingContext);
  
    /// Get the effective access of a member.
    static AccessSpecifier getEffectiveAccess(NamedDecl *D);
  
    /// Check if DerivedClass is derived from BaseClass.
    static bool isDerivedFrom(CXXRecordDecl *Derived, CXXRecordDecl *Base);
  };
  
  } // namespace blocktype
  ```

**Checkpoint：** 访问控制测试通过

---

### Task 4.5.3 语义诊断系统

**目标：** 扩展诊断 ID 系统，添加语义分析所需的所有诊断

**开发要点：**

- **E4.5.3.1** 创建 `include/blocktype/Basic/DiagnosticSemaKinds.def`：
  ```cpp
  // Semantic analysis diagnostics
  // Format: DIAG(ID, Level, English_Message, Chinese_Message)
  
  //===----------------------------------------------------------------------===//
  // Name lookup errors
  //===----------------------------------------------------------------------===//
  DIAG(err_undeclared_var,       Error, "use of undeclared identifier '%0'",           "使用了未声明的标识符 '%0'")
  DIAG(err_undeclared_identifier, Error, "unknown identifier '%0'",                    "未知标识符 '%0'")
  DIAG(err_redefinition,         Error, "redefinition of '%0'",                       "'%0' 的重定义")
  DIAG(note_previous_definition, Note,  "previous definition is here",                 "前一个定义在此处")
  DIAG(err_not_a_type,           Error, "'%0' is not a type name",                    "'%0' 不是一个类型名")
  DIAG(err_not_a_template,       Error, "'%0' is not a template",                     "'%0' 不是一个模板")
  DIAG(err_not_a_namespace,      Error, "'%0' is not a namespace name",               "'%0' 不是一个命名空间名")
  
  //===----------------------------------------------------------------------===//
  // Type checking errors
  //===----------------------------------------------------------------------===//
  DIAG(err_type_mismatch,        Error, "cannot initialize a variable of type '%0' with an expression of type '%1'", "无法用类型为 '%1' 的表达式初始化类型为 '%0' 的变量")
  DIAG(err_assigning_to_const,   Error, "cannot assign to a variable that is const qualified", "不能对 const 限定的变量赋值")
  DIAG(err_incomplete_type,      Error, "incomplete type '%0' is not allowed",          "不允许使用不完整类型 '%0'")
  DIAG(err_void_expr_not_allowed, Error, "expression of type void is not allowed here", "此处不允许 void 类型的表达式")
  DIAG(err_non_constexpr_in_constant_context, Error, "expression is not a constant expression", "表达式不是常量表达式")
  
  //===----------------------------------------------------------------------===//
  // Overload resolution errors
  //===----------------------------------------------------------------------===//
  DIAG(err_ovl_no_viable_function, Error, "no matching function for call to '%0'",      "没有匹配的函数可调用 '%0'")
  DIAG(err_ovl_ambiguous_call,     Error, "call to '%0' is ambiguous",                   "对 '%0' 的调用是歧义的")
  DIAG(err_ovl_deleted_function,   Error, "call to deleted function '%0'",               "调用了已删除的函数 '%0'")
  DIAG(note_ovl_candidate,         Note, "candidate function",                           "候选函数")
  DIAG(note_ovl_candidate_not_viable, Note, "candidate function not viable: %0",         "候选函数不可行: %0")
  
  //===----------------------------------------------------------------------===//
  // Access control errors
  //===----------------------------------------------------------------------===//
  DIAG(err_access_private,   Error, "'%0' is a private member of '%1'",  "'%0' 是 '%1' 的 private 成员")
  DIAG(err_access_protected, Error, "'%0' is a protected member of '%1'","'%0' 是 '%1' 的 protected 成员")
  DIAG(note_access_declared, Note, "declared here",                       "在此处声明")
  
  //===----------------------------------------------------------------------===//
  // Statement checking errors
  //===----------------------------------------------------------------------===//
  DIAG(err_return_type_mismatch, Error, "return type mismatch: expected '%0', got '%1'", "返回类型不匹配: 期望 '%0'，实际 '%1'")
  DIAG(err_break_outside_loop,   Error, "'break' statement not in loop or switch",        "'break' 语句不在循环或 switch 中")
  DIAG(err_continue_outside_loop, Error, "'continue' statement not in loop",              "'continue' 语句不在循环中")
  DIAG(err_case_not_in_switch,   Error, "'case' statement not in switch",                 "'case' 语句不在 switch 中")
  
  //===----------------------------------------------------------------------===//
  // Warnings
  //===----------------------------------------------------------------------===//
  DIAG(warn_unused_variable,    Warning, "unused variable '%0'",            "未使用的变量 '%0'")
  DIAG(warn_unused_function,    Warning, "unused function '%0'",            "未使用的函数 '%0'")
  DIAG(warn_unreachable_code,   Warning, "unreachable code",               "不可达代码")
  DIAG(warn_implicit_conversion, Warning, "implicit conversion from '%0' to '%1'", "从 '%0' 到 '%1' 的隐式转换")
  ```

- **E4.5.3.2** 修改 `DiagnosticIDs.h` 中的 include，使其也包含 `DiagnosticSemaKinds.def`

**Checkpoint：** 诊断系统测试通过；错误信息中英双语显示正确

---

### Task 4.5.4 常量表达式求值

**目标：** 实现常量表达式求值

**开发要点：**

- **E4.5.4.1** 创建 `include/blocktype/Sema/ConstantExpr.h`：
  ```cpp
  #pragma once
  
  #include "blocktype/AST/Expr.h"
  #include "blocktype/AST/ASTContext.h"
  #include "blocktype/AST/Type.h"
  #include "llvm/ADT/APInt.h"
  #include "llvm/ADT/APFloat.h"
  
  namespace blocktype {
  
  /// EvalResult - Result of constant expression evaluation.
  class EvalResult {
  public:
    enum ResultKind {
      /// Successfully evaluated.
      Success,
  
      /// Expression is not a constant expression.
      NotConstantExpression,
  
      /// Expression has side effects.
      HasSideEffects,
  
      /// Expression depends on a template parameter.
      DependsOnTemplateParameter,
  
      /// Evaluation failed (overflow, division by zero, etc.).
      EvaluationFailed,
    };
  
  private:
    ResultKind Kind = NotConstantExpression;
  
    /// Integral result (for integer/boolean/enum types).
    llvm::APSInt IntVal;
  
    /// Float result (for floating-point types).
    llvm::APFloat FloatVal{llvm::APFloat::IEEEdouble()};
  
    /// Whether the result is integral or floating.
    bool IsIntegral = true;
  
    /// Diagnostic message (on failure).
    llvm::StringRef DiagMessage;
  
  public:
    EvalResult() = default;
  
    static EvalResult getSuccess(llvm::APSInt Val) {
      EvalResult R;
      R.Kind = Success;
      R.IntVal = Val;
      R.IsIntegral = true;
      return R;
    }
  
    static EvalResult getSuccess(llvm::APFloat Val) {
      EvalResult R;
      R.Kind = Success;
      R.FloatVal = Val;
      R.IsIntegral = false;
      return R;
    }
  
    static EvalResult getFailure(ResultKind K, llvm::StringRef Msg = "") {
      EvalResult R;
      R.Kind = K;
      R.DiagMessage = Msg;
      return R;
    }
  
    ResultKind getKind() const { return Kind; }
    bool isSuccess() const { return Kind == Success; }
    bool isIntegral() const { return IsIntegral; }
  
    const llvm::APSInt &getInt() const { return IntVal; }
    const llvm::APFloat &getFloat() const { return FloatVal; }
  
    llvm::StringRef getDiagMessage() const { return DiagMessage; }
  };
  
  /// ConstantExprEvaluator - Evaluates constant expressions.
  ///
  /// Used for:
  /// - Array bounds: int arr[10];
  /// - Template arguments: template<int N>
  /// - Case labels: case 1:
  /// - Enum values: enum { X = 10 };
  /// - constexpr variables
  /// - static_assert conditions
  class ConstantExprEvaluator {
    ASTContext &Context;
  
  public:
    explicit ConstantExprEvaluator(ASTContext &C) : Context(C) {}
  
    /// Evaluate an expression as a constant expression.
    EvalResult Evaluate(Expr *E);
  
    /// Evaluate as a boolean constant.
    llvm::Optional<bool> EvaluateAsBooleanCondition(Expr *E);
  
    /// Evaluate as an integer constant.
    llvm::Optional<llvm::APSInt> EvaluateAsInt(Expr *E);
  
    /// Evaluate as a floating-point constant.
    llvm::Optional<llvm::APFloat> EvaluateAsFloat(Expr *E);
  
    /// Check if an expression is a constant expression (without evaluating).
    bool isConstantExpr(Expr *E);
  
    /// Evaluate a constexpr function call.
    EvalResult EvaluateCall(FunctionDecl *F, llvm::ArrayRef<Expr *> Args);
  
  private:
    // Recursive evaluation dispatch
    EvalResult EvaluateExpr(Expr *E);
    EvalResult EvaluateIntegerLiteral(IntegerLiteral *E);
    EvalResult EvaluateFloatingLiteral(FloatingLiteral *E);
    EvalResult EvaluateBooleanLiteral(BooleanLiteral *E);
    EvalResult EvaluateBinaryOperator(BinaryOperator *E);
    EvalResult EvaluateUnaryOperator(UnaryOperator *E);
    EvalResult EvaluateConditionalOperator(ConditionalOperator *E);
    EvalResult EvaluateDeclRefExpr(DeclRefExpr *E);
    EvalResult EvaluateCastExpr(CastExpr *E);
  };
  
  } // namespace blocktype
  ```

**开发关键点提示：**
> **常量表达式场景**（参照 Clang ExprConstant.cpp）：
> - 字面量：整数、浮点数、布尔、字符、字符串
> - constexpr 变量引用
> - constexpr 函数调用
> - 算术运算符、比较运算符、逻辑运算符
> - 条件运算符 (? : )
> - 类型转换（static_cast 等）
>
> **不可求值的情况**：
> - 非 constexpr 函数调用
> - 非常量变量引用
> - dynamic_cast、reinterpret_cast
> - throw 表达式
> - new/delete 表达式
>
> **与已有 Expr 子类的交互**：
> - 使用 `dyn_cast<IntegerLiteral>(E)` 等进行分派
> - `IntegerLiteral` 有 `getValue()` 返回 `APInt`
> - `BooleanLiteral` 有 `getValue()` 返回 `bool`
> - 所有 Expr 子类的 classof 已实现

**Checkpoint：** 常量表达式求值测试通过

---

### Task 4.5.5 语义分析集成测试

**目标：** 建立语义分析的完整测试覆盖

**开发要点：**

- **E4.5.5.1** 创建单元测试文件：
  ```
  tests/unit/Sema/
  ├── SemaTest.cpp              # Sema 主类测试
  ├── SymbolTableTest.cpp       # 符号表测试
  ├── ScopeTest.cpp             # 作用域测试（如需扩展）
  ├── NameLookupTest.cpp        # 名字查找测试
  ├── OverloadResolutionTest.cpp # 重载决议测试
  ├── TypeCheckTest.cpp         # 类型检查测试
  ├── AccessControlTest.cpp     # 访问控制测试
  ├── ConstantExprTest.cpp      # 常量表达式求值测试
  └── TypeDeductionTest.cpp     # 类型推导测试
  ```

- **E4.5.5.2** 创建回归测试：
  ```
  tests/lit/Sema/
  ├── basic-var.test            # 变量声明和类型检查
  ├── function-decl.test        # 函数声明和调用
  ├── overload.test             # 重载决议
  ├── class-member.test         # 类成员和访问控制
  ├── template.test             # 模板基础
  ├── namespace.test            # 命名空间和名字查找
  ├── auto-deduction.test       # auto 类型推导
  ├── constexpr.test            # constexpr 求值
  └── error-recovery.test       # 错误恢复和诊断质量
  ```

**测试覆盖重点：**

| 模块 | 核心测试用例 |
|------|-------------|
| SymbolTable | 添加/查找/重定义检查 |
| NameLookup | 局部变量遮蔽、using namespace、类成员查找 |
| OverloadResolution | 精确匹配 > 提升 > 转换 > 歧义 |
| TypeCheck | 赋值兼容、初始化检查、条件类型 |
| AccessControl | public/protected/private、友元、继承 |
| ConstantExpr | 字面量、算术、constexpr 函数 |
| TypeDeduction | auto/decltype/decltype(auto) |

**Checkpoint：** 测试覆盖率 ≥ 80%；所有测试通过

---

## 📋 Phase 4 验收检查清单

```
Stage 4.1 — Sema 基础设施
[ ] Sema 主类实现完成（Sema.h + Sema.cpp）
[ ] ExprResult/StmtResult/DeclResult 包装类完成
[ ] 符号表实现完成（SymbolTable.h + SymbolTable.cpp）
[ ] Scope 集成验证（using 指令支持）
[ ] DeclContext 集成验证

Stage 4.2 — 类型系统完善
[ ] ASTContext 类型工厂补全（getRecordType, getEnumType 等）
[ ] 类型推导实现完成（TypeDeduction.h + TypeDeduction.cpp）
[ ] auto 推导规则正确
[ ] decltype 推导规则正确
[ ] 类型完整性检查实现

Stage 4.3 — 名字查找
[ ] LookupResult 和 NestedNameSpecifier 实现完成
[ ] Unqualified Lookup 实现完成
[ ] Qualified Lookup 实现完成
[ ] ADL 实现完成
[ ] 标签查找（class/struct/enum）正确

Stage 4.4 — 重载决议
[ ] OverloadCandidateSet 实现完成
[ ] ImplicitConversionSequence 实现完成
[ ] ConversionChecker 实现完成
[ ] 候选排序正确
[ ] 歧义检测正确
[ ] 最佳函数选择正确

Stage 4.5 — 语义检查
[ ] TypeCheck 实现完成
[ ] AccessControl 实现完成
[ ] 语义诊断 ID 全部定义（DiagnosticSemaKinds.def）
[ ] 常量表达式求值实现完成
[ ] 集成测试通过
[ ] 测试覆盖率 ≥ 80%
```

---

## 📁 Phase 4 新增文件清单

```
include/blocktype/Sema/
├── Sema.h                    # Sema 主类
├── SymbolTable.h             # 符号表
├── Lookup.h                  # 名字查找（LookupResult + NestedNameSpecifier）
├── Overload.h                # 重载决议
├── Conversion.h              # 隐式转换
├── TypeCheck.h               # 类型检查
├── AccessControl.h           # 访问控制
├── ConstantExpr.h            # 常量表达式求值
└── TypeDeduction.h           # 类型推导

src/Sema/
├── Sema.cpp                  # Sema 主类实现
├── SymbolTable.cpp           # 符号表实现
├── Lookup.cpp                # 名字查找实现
├── Overload.cpp              # 重载决议实现
├── Conversion.cpp            # 隐式转换实现
├── TypeCheck.cpp             # 类型检查实现
├── AccessControl.cpp         # 访问控制实现
├── ConstantExpr.cpp          # 常量表达式求值实现
├── TypeDeduction.cpp         # 类型推导实现
└── Scope.cpp                 # (已有) 作用域实现

include/blocktype/Basic/
└── DiagnosticSemaKinds.def   # 语义分析诊断 ID
```

---

*Phase 4 完成标志：语义分析框架完整实现；能对 C++ 程序进行完整的语义分析；符号表、类型系统、名字查找、重载决议、类型检查等核心功能全部就绪；所有接口完整（方法、enum、classof、构造函数），后续阶段的 AI 开发者可直接使用；测试通过，覆盖率 ≥ 80%。*
