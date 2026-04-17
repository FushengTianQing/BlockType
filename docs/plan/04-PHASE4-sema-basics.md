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
| **Stage 4.1** | Sema 基础设施 | Sema 类、符号表、Scope 管理 | 1 周 |
| **Stage 4.2** | 类型系统 | 内建类型、类型推导、CV 限定符 | 1 周 |
| **Stage 4.3** | 名字查找 | Unqualified/Qualified Lookup、ADL | 1 周 |
| **Stage 4.4** | 重载决议 | 候选集、排序、最佳匹配 | 1 周 |
| **Stage 4.5** | 语义检查 | 类型检查、访问控制、诊断 | 1 周 |

**Phase 4 架构图：**

```
┌─────────────────────────────────────────────────────────────┐
│                        Sema                                  │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ SymbolTable │  │  TypeSystem │  │    ScopeChain       │  │
│  │             │  │             │  │                     │  │
│  │ - globals   │  │ - BuiltinTy │  │ - CurrentScope      │  │
│  │ - locals    │  │ - QualType  │  │ - PushScope()       │  │
│  │ - tags      │  │ - TypeDeriv │  │ - PopScope()        │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                 Name Lookup                           │   │
│  │  - UnqualifiedLookup(Name, Scope) → Decl*            │   │
│  │  - QualifiedLookup(Name, NestedNameSpecifier) → Decl*│   │
│  │  - ArgumentDependentLookup(Name, Args) → DeclSet     │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │               Overload Resolution                     │   │
│  │  - AddCandidates(Decls, Args)                         │   │
│  │  - RankCandidates() → BestMatch                       │   │
│  │  - ResolveOverload() → FunctionDecl*                  │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │               Type Checking                           │   │
│  │  - CheckTypes(Expr*, ExpectedType)                    │   │
│  │  - CheckCall(FunctionDecl*, Args)                     │   │
│  │  - CheckAccess(NamedDecl*, AccessSpec)                │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## Stage 4.1 — Sema 基础设施

### Task 4.1.1 Sema 主类

**目标：** 实现语义分析引擎的核心类

**开发要点：**

- **E4.1.1.1** 创建 `include/nova-cc/Sema/Sema.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/ASTContext.h"
  #include "nova-cc/AST/Decl.h"
  #include "nova-cc/AST/Expr.h"
  #include "nova-cc/AST/Stmt.h"
  #include "nova-cc/Basic/Diagnostics.h"
  #include "nova-cc/Sema/SymbolTable.h"
  #include "nova-cc/Sema/Scope.h"
  #include "llvm/ADT/DenseMap.h"
  #include <memory>
  
  namespace nova {
  
  class Sema {
    ASTContext &Context;
    DiagnosticsEngine &Diags;
    SymbolTable Symbols;
    ScopeChain Scopes;
    
    // 当前处理的声明上下文
    DeclContext *CurContext = nullptr;
    
    // 当前函数（用于 return 类型检查）
    FunctionDecl *CurFunction = nullptr;
    
  public:
    Sema(ASTContext &C, DiagnosticsEngine &D);
    ~Sema();
    
    // === 声明处理 ===
    Decl* ActOnDeclarator(Declarator &D);
    void ActOnFinishDecl(Decl *D);
    
    // === 表达式处理 ===
    ExprResult ActOnExpr(Expr *E);
    ExprResult ActOnCallExpr(Expr *Fn, ArrayRef<Expr*> Args);
    ExprResult ActOnMemberExpr(Expr *Base, IdentifierInfo *Member);
    
    // === 语句处理 ===
    StmtResult ActOnReturnStmt(Expr *RetVal);
    StmtResult ActOnIfStmt(Expr *Cond, Stmt *Then, Stmt *Else);
    StmtResult ActOnWhileStmt(Expr *Cond, Stmt *Body);
    
    // === 作用域管理 ===
    void PushScope(Scope::ScopeKind Kind);
    void PopScope();
    Scope* getCurrentScope() const { return Scopes.getCurrentScope(); }
    
    // === 上下文管理 ===
    DeclContext* getCurrentContext() const { return CurContext; }
    void PushDeclContext(DeclContext *DC);
    void PopDeclContext();
    
    // === 诊断辅助 ===
    DiagnosticsEngine& getDiagnostics() { return Diags; }
    bool hasErrorOccurred() const { return Diags.hasErrorOccurred(); }
  };
  
  } // namespace nova
  ```

- **E4.1.1.2** 实现 `src/Sema/Sema.cpp` 核心逻辑：
  ```cpp
  #include "nova-cc/Sema/Sema.h"
  
  namespace nova {
  
  Sema::Sema(ASTContext &C, DiagnosticsEngine &D)
    : Context(C), Diags(D), Symbols(C) {
    // 初始化全局作用域
    PushScope(Scope::SK_Global);
  }
  
  Sema::~Sema() {
    while (Scopes.getCurrentScope()) {
      PopScope();
    }
  }
  
  void Sema::PushScope(Scope::ScopeKind Kind) {
    Scope *Parent = Scopes.getCurrentScope();
    auto *NewScope = new Scope(Kind, Parent);
    Scopes.push(NewScope);
  }
  
  void Sema::PopScope() {
    Scope *S = Scopes.pop();
    if (S) delete S;
  }
  
  Decl* Sema::ActOnDeclarator(Declarator &D) {
    // 1. 创建声明节点
    // 2. 执行名字查找（检查重定义）
    // 3. 添加到符号表
    // 4. 添加到当前作用域
    // ... 详细实现见后续 Task
  }
  
  } // namespace nova
  ```

**开发关键点提示：**
> 请为 nova-cc 实现 Sema 主类。
>
> **核心职责**：
> 1. 协调符号表、作用域链、类型系统
> 2. 处理声明（变量、函数、类等）
> 3. 处理表达式（类型推导、重载决议）
> 4. 处理语句（控制流、跳转检查）
> 5. 发出语义错误诊断
>
> **关键数据结构**：
> - SymbolTable：全局符号存储
> - ScopeChain：作用域栈
> - CurContext：当前声明上下文
> - CurFunction：当前函数（用于 return 检查）
>
> **ActOnXXX 方法模式**：
> - ActOnDeclarator：处理声明符，创建 Decl
> - ActOnExpr：处理表达式，进行类型检查
> - ActOnCallExpr：处理函数调用，执行重载决议
> - ActOnReturnStmt：处理 return 语句，检查类型匹配
>
> **错误处理**：
> - 使用 ExprResult/StmtResult 包装结果
> - 错误时返回 Invalid，不抛异常
> - 通过 Diags 报告错误

**Checkpoint：** Sema 类编译通过；PushScope/PopScope 测试通过

---

### Task 4.1.2 符号表实现

**目标：** 实现高效的符号表数据结构

**开发要点：**

- **E4.1.2.1** 创建 `include/nova-cc/Sema/SymbolTable.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Decl.h"
  #include "nova-cc/Basic/LLVM.h"
  #include "llvm/ADT/DenseMap.h"
  #include "llvm/ADT/StringMap.h"
  #include <vector>
  
  namespace nova {
  
  class SymbolTable {
    ASTContext &Context;
    
    // 全局符号：名字 → 声明列表（允许多个重载）
    llvm::StringMap<std::vector<NamedDecl*>> GlobalSymbols;
    
    // 标签（类、结构、联合、枚举）
    llvm::StringMap<TagDecl*> Tags;
    
    // 类型定义
    llvm::StringMap<TypedefDecl*> Typedefs;
    
    // 命名空间
    llvm::StringMap<NamespaceDecl*> Namespaces;
    
  public:
    SymbolTable(ASTContext &C) : Context(C) {}
    
    // === 符号添加 ===
    void addGlobalDecl(NamedDecl *D);
    void addTagDecl(TagDecl *D);
    void addTypedefDecl(TypedefDecl *D);
    
    // === 符号查找 ===
    std::vector<NamedDecl*> lookup(StringRef Name) const;
    TagDecl* lookupTag(StringRef Name) const;
    NamespaceDecl* lookupNamespace(StringRef Name) const;
    
    // === 符号遍历 ===
    void dump() const;
  };
  
  } // namespace nova
  ```

- **E4.1.2.2** 实现 `src/Sema/SymbolTable.cpp`：
  ```cpp
  void SymbolTable::addGlobalDecl(NamedDecl *D) {
    StringRef Name = D->getIdentifier()->getName();
    
    // 检查是否已存在同名声明
    auto &Decls = GlobalSymbols[Name];
    
    // 允许函数重载
    if (isa<FunctionDecl>(D)) {
      Decls.push_back(D);
      return;
    }
    
    // 非函数声明，检查重定义
    if (!Decls.empty()) {
      // 报告重定义错误
      return;
    }
    
    Decls.push_back(D);
  }
  
  std::vector<NamedDecl*> SymbolTable::lookup(StringRef Name) const {
    auto It = GlobalSymbols.find(Name);
    if (It != GlobalSymbols.end()) {
      return It->second;
    }
    return {};
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现符号表。
>
> **符号分类**：
> - 普通符号：变量、函数、枚举值
> - 标签：类、结构、联合、枚举
> - 类型定义：typedef、using 别名
> - 命名空间
>
> **重载处理**：
> - 函数允许同名（重载）
> - 变量不允许重名（同一作用域）
> - 类和变量可以同名（不同名字空间）
>
> **查找效率**：
> - 使用 llvm::StringMap（哈希表）
> - O(1) 平均查找时间
>
> **错误诊断**：
> - 重定义错误：指出第一个定义的位置
> - 使用 SourceLocation 定位

**Checkpoint：** 符号表能正确添加和查找符号；重定义错误能正确报告

---

### Task 4.1.3 Scope 与作用域链

**目标：** 实现作用域管理和作用域链

**开发要点：**

- **E4.1.3.1** 创建 `include/nova-cc/Sema/Scope.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Decl.h"
  #include "llvm/ADT/SmallPtrSet.h"
  #include "llvm/ADT/StringMap.h"
  
  namespace nova {
  
  class Scope {
  public:
    enum ScopeKind {
      SK_Global,      // 全局作用域
      SK_Namespace,   // 命名空间作用域
      SK_Class,       // 类作用域
      SK_Function,    // 函数作用域
      SK_Block,       // 块作用域
      SK_Template,    // 模板参数作用域
      SK_Control,     // 控制语句作用域（for 的初始化）
    };
    
  private:
    ScopeKind Kind;
    Scope *Parent;
    
    // 当前作用域的声明
    llvm::StringMap<NamedDecl*> Decls;
    
    // 当前作用域的 using 指令
    llvm::SmallVector<NamespaceDecl*, 4> UsingDirectives;
    
  public:
    Scope(ScopeKind K, Scope *P) : Kind(K), Parent(P) {}
    
    ScopeKind getKind() const { return Kind; }
    Scope* getParent() const { return Parent; }
    
    // === 声明管理 ===
    bool addDecl(NamedDecl *D);
    NamedDecl* lookup(StringRef Name) const;
    
    // === Using 指令 ===
    void addUsingDirective(NamespaceDecl *NS);
    ArrayRef<NamespaceDecl*> getUsingDirectives() const { return UsingDirectives; }
    
    // === 作用域类型判断 ===
    bool isGlobalScope() const { return Kind == SK_Global; }
    bool isClassScope() const { return Kind == SK_Class; }
    bool isFunctionScope() const { return Kind == SK_Function; }
    bool isBlockScope() const { return Kind == SK_Block; }
  };
  
  class ScopeChain {
    llvm::SmallVector<Scope*, 16> Chain;
    
  public:
    void push(Scope *S) { Chain.push_back(S); }
    Scope* pop() {
      if (Chain.empty()) return nullptr;
      Scope *S = Chain.back();
      Chain.pop_back();
      return S;
    }
    Scope* getCurrentScope() const {
      return Chain.empty() ? nullptr : Chain.back();
    }
    bool empty() const { return Chain.empty(); }
  };
  
  } // namespace nova
  ```

- **E4.1.3.2** 实现作用域查找算法：
  ```cpp
  NamedDecl* Scope::lookup(StringRef Name) const {
    // 在当前作用域查找
    auto It = Decls.find(Name);
    if (It != Decls.end()) {
      return It->second;
    }
    
    // 在父作用域递归查找
    if (Parent) {
      return Parent->lookup(Name);
    }
    
    return nullptr;
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现作用域管理。
>
> **作用域类型**：
> - Global：文件作用域，包含全局变量、函数
> - Namespace：命名空间作用域
> - Class：类作用域，成员查找规则特殊
> - Function：函数作用域，参数和局部变量
> - Block：块作用域，{} 内的变量
> - Template：模板参数作用域
> - Control：控制语句作用域（for 的 init）
>
> **查找规则**：
> - 先在当前作用域查找
> - 找不到则向父作用域递归
> - 到达全局作用域仍未找到则报错
>
> **Using 指令**：
> - using namespace X；将 X 的成员添加到查找候选
> - 在父作用域查找时也要考虑 using 指令
>
> **特殊情况**：
> - 类成员查找要考虑访问控制
> - 模板参数作用域在模板实例化时特殊处理

**Checkpoint：** 作用域链能正确模拟 C++ 的块作用域规则；嵌套查找测试通过

---

### Task 4.1.4 DeclContext 实现

**目标：** 实现声明上下文，管理声明的层级关系

**开发要点：**

- **E4.1.4.1** 扩展 `include/nova-cc/AST/Decl.h`：
  ```cpp
  class DeclContext {
    DeclContext *Parent = nullptr;
    llvm::SmallVector<Decl*, 16> Members;
    
  public:
    DeclContext(DeclContext *P = nullptr) : Parent(P) {}
    
    // === 成员管理 ===
    void addDecl(Decl *D);
    ArrayRef<Decl*> decls() const { return Members; }
    
    // === 上下文导航 ===
    DeclContext* getParent() const { return Parent; }
    DeclContext* getLookupParent() const;
    
    // === 上下文类型判断 ===
    bool isNamespace() const;
    bool isRecord() const;
    bool isFunction() const;
    bool isTranslationUnit() const;
    
    // === 查找辅助 ===
    NamedDecl* lookup(StringRef Name);
  };
  ```

- **E4.1.4.2** 实现上下文查找：
  ```cpp
  NamedDecl* DeclContext::lookup(StringRef Name) {
    // 在当前上下文的成员中查找
    for (Decl *D : Members) {
      if (auto *ND = dyn_cast<NamedDecl>(D)) {
        if (ND->getIdentifier()->getName() == Name) {
          return ND;
        }
      }
    }
    
    // 在父上下文中查找
    if (Parent) {
      return Parent->lookup(Name);
    }
    
    return nullptr;
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现 DeclContext。
>
> **DeclContext 的作用**：
> - 表示可以包含声明的实体（命名空间、类、函数等）
> - 管理成员声明
> - 提供查找接口
>
> **DeclContext 继承关系**：
> - TranslationUnitDecl：翻译单元
> - NamespaceDecl：命名空间
> - RecordDecl：类/结构/联合
> - FunctionDecl：函数
> - EnumDecl：枚举
>
> **关键方法**：
> - addDecl：添加成员
> - lookup：在当前上下文查找
> - getLookupParent：获取查找父上下文
>
> **与 Scope 的关系**：
> - Scope 是编译时的作用域栈
> - DeclContext 是 AST 中的声明层级
> - 两者配合使用

**Checkpoint：** DeclContext 能正确管理声明层级；类成员查找测试通过

---

## Stage 4.2 — 类型系统

### Task 4.2.1 内建类型实现

**目标：** 实现所有 C++ 内建类型

**开发要点：**

- **E4.2.1.1** 创建 `include/nova-cc/AST/Type.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/Basic/LLVM.h"
  #include "llvm/ADT/FoldingSet.h"
  
  namespace nova {
  
  class Type {
  public:
    enum TypeClass {
      // 基础类型
      TC_Builtin,
      TC_Pointer,
      TC_Reference,
      TC_Array,
      TC_Function,
      TC_Record,
      TC_Enum,
      
      // 类型包装
      TC_Qualified,    // const/volatile
      TC_Elaborated,   // class/struct/enum 前缀
      
      // 模板相关
      TC_Template,
      TC_TemplateSpecialization,
      
      // 特殊类型
      TC_Auto,
      TC_Decltype,
      TC_Void,
    };
    
  private:
    TypeClass TC;
    
  protected:
    Type(TypeClass tc) : TC(tc) {}
    
  public:
    TypeClass getTypeClass() const { return TC; }
    virtual void dump(raw_ostream &OS) const = 0;
  };
  
  class BuiltinType : public Type {
  public:
    enum Kind {
      Void,
      Bool,
      Char, SChar, UChar, Char8, Char16, Char32, WChar,
      Short, UShort,
      Int, UInt,
      Long, ULong,
      LongLong, ULongLong,
      Float, Double, LongDouble,
      NullPtr,
    };
    
  private:
    Kind K;
    
  public:
    BuiltinType(Kind k) : Type(TC_Builtin), K(k) {}
    Kind getKind() const { return K; }
    
    bool isInteger() const;
    bool isFloatingPoint() const;
    bool isSignedInteger() const;
    bool isUnsignedInteger() const;
    unsigned getWidth() const;
    
    void dump(raw_ostream &OS) const override;
  };
  
  } // namespace nova
  ```

- **E4.2.1.2** 创建 `include/nova-cc/AST/QualType.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Type.h"
  #include "llvm/ADT/PointerIntPair.h"
  
  namespace nova {
  
  class QualType {
    // 低 3 位存储 CV 限定符
    llvm::PointerIntPair<Type*, 3, unsigned> Value;
    
  public:
    enum CVRQualifiers {
      Const    = 0x1,
      Volatile = 0x2,
      Restrict = 0x4,
    };
    
    QualType() = default;
    QualType(Type *T, unsigned Quals) : Value(T, Quals) {}
    
    Type* getTypePtr() const { return Value.getPointer(); }
    unsigned getQualifiers() const { return Value.getInt(); }
    
    bool isConstQualified() const { return getQualifiers() & Const; }
    bool isVolatileQualified() const { return getQualifiers() & Volatile; }
    bool isRestrictQualified() const { return getQualifiers() & Restrict; }
    
    QualType withConst() const { return QualType(getTypePtr(), getQualifiers() | Const); }
    QualType withoutConst() const { return QualType(getTypePtr(), getQualifiers() & ~Const); }
    
    bool isNull() const { return getTypePtr() == nullptr; }
    bool isValid() const { return getTypePtr() != nullptr; }
    
    // 类型比较
    bool operator==(QualType RHS) const;
    bool operator!=(QualType RHS) const { return !(*this == RHS); }
  };
  
  } // namespace nova
  ```

**开发关键点提示：**
> 请为 nova-cc 实现类型系统基础。
>
> **内建类型分类**：
> - Void：void 类型
> - 布尔：bool
> - 字符：char, signed char, unsigned char, char8_t, char16_t, char32_t, wchar_t
> - 整数：short, int, long, long long 及其无符号版本
> - 浮点：float, double, long double
> - 空指针：nullptr_t
>
> **CV 限定符**：
> - const：不可修改
> - volatile：每次访问都从内存读取
> - restrict：指针不重叠（C 扩展）
>
> **QualType 设计**：
> - 使用 PointerIntPair 将 CV 限定符编码到指针低位
> - 节省内存，提高缓存效率
> - 提供 withConst/withoutConst 等便捷方法
>
> **类型宽度**：
> - char：8 位
> - short：16 位
> - int：32 位（平台相关）
> - long：64 位（Linux/macOS）
> - long long：64 位
> - float：32 位
> - double：64 位

**Checkpoint：** 所有内建类型定义完成；QualType 测试通过

---

### Task 4.2.2 派生类型（指针、引用、数组）

**目标：** 实现指针、引用、数组等派生类型

**开发要点：**

- **E4.2.2.1** 扩展 `include/nova-cc/AST/Type.h`：
  ```cpp
  class PointerType : public Type {
    QualType Pointee;
    
  public:
    PointerType(QualType T) : Type(TC_Pointer), Pointee(T) {}
    QualType getPointeeType() const { return Pointee; }
    
    void dump(raw_ostream &OS) const override;
  };
  
  class ReferenceType : public Type {
    QualType Referent;
    
  public:
    ReferenceType(QualType T) : Type(TC_Reference), Referent(T) {}
    QualType getReferentType() const { return Referent; }
    
    void dump(raw_ostream &OS) const override;
  };
  
  class ArrayType : public Type {
    QualType ElementType;
    unsigned Size;  // 0 表示未知大小
    
  public:
    ArrayType(QualType T, unsigned N) : Type(TC_Array), ElementType(T), Size(N) {}
    QualType getElementType() const { return ElementType; }
    unsigned getSize() const { return Size; }
    bool isIncomplete() const { return Size == 0; }
    
    void dump(raw_ostream &OS) const override;
  };
  
  class FunctionType : public Type {
    QualType ReturnType;
    llvm::SmallVector<QualType, 8> ParamTypes;
    bool IsVariadic;
    
  public:
    FunctionType(QualType Ret, ArrayRef<QualType> Params, bool VarArgs)
      : Type(TC_Function), ReturnType(Ret), ParamTypes(Params), IsVariadic(VarArgs) {}
    
    QualType getReturnType() const { return ReturnType; }
    ArrayRef<QualType> getParamTypes() const { return ParamTypes; }
    bool isVariadic() const { return IsVariadic; }
    
    void dump(raw_ostream &OS) const override;
  };
  ```

**开发关键点提示：**
> 请为 nova-cc 实现派生类型。
>
> **指针类型**：
> - 存储被指向类型
> - 支持 const int* 和 int* const 的区别
> - 支持多级指针（int**）
>
> **引用类型**：
> - 左值引用（int&）
> - 右值引用（int&&）
> - 注意：C++ 中引用类型在类型系统中统一表示
>
> **数组类型**：
> - 常量大小数组（int[10]）
> - 不完整数组（int[]）
> - 多维数组（int[10][20]）
> - 数组到指针衰减
>
> **函数类型**：
> - 返回类型
> - 参数类型列表
> - 是否变参（...）
> - cv-qualifier（成员函数的 const/volatile）
>
> **类型规范化**：
> - typedef 被还原为底层类型
> - using 别名被还原
> - 用于类型比较

**Checkpoint：** 派生类型测试通过；int*, int&, int[10] 能正确表示

---

### Task 4.2.3 类型推导（auto, decltype）

**目标：** 实现 auto 和 decltype 类型推导

**开发要点：**

- **E4.2.3.1** 创建 `include/nova-cc/Sema/TypeDeduction.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Type.h"
  #include "nova-cc/AST/Expr.h"
  
  namespace nova {
  
  class TypeDeduction {
    ASTContext &Context;
    
  public:
    TypeDeduction(ASTContext &C) : Context(C) {}
    
    // === auto 推导 ===
    QualType deduceAutoType(Expr *Init);
    
    // === decltype 推导 ===
    QualType deduceDecltypeType(Expr *E);
    
    // === 模板参数推导 ===
    QualType deduceTemplateArgument(TemplateDecl *Template, 
                                     ArrayRef<TemplateArgument> Args,
                                     QualType Target);
  };
  
  } // namespace nova
  ```

- **E4.2.3.2** 实现 auto 推导：
  ```cpp
  QualType TypeDeduction::deduceAutoType(Expr *Init) {
    // auto 推导规则：
    // 1. 从初始化表达式推导
    // 2. 忽略顶层 const/volatile（除非声明为 const auto）
    // 3. 数组衰减为指针
    // 4. 函数衰减为函数指针
    
    QualType T = Init->getType();
    
    // 处理引用
    if (auto *Ref = dyn_cast<ReferenceType>(T.getTypePtr())) {
      T = Ref->getReferentType();
    }
    
    // 处理数组衰减
    if (auto *Arr = dyn_cast<ArrayType>(T.getTypePtr())) {
      return Context.getPointerType(Arr->getElementType());
    }
    
    return T;
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现类型推导。
>
> **auto 推导规则**（C++11 起）：
> 1. auto x = expr；推导为 expr 的类型（去除引用）
> 2. auto& x = expr；推导为 expr 的引用类型
> 3. auto* x = &expr；推导为指针类型
> 4. const auto x = expr；推导为 const T
>
> **decltype 推导规则**（C++11 起）：
> 1. decltype(id) → id 的声明类型
> 2. decltype(expr) → expr 的类型
> 3. decltype((id)) → id 的引用类型
>
> **特殊情况**：
> - auto&& 转发引用
> - 初始化列表推导（auto x = {1, 2, 3}）
> - 返回类型推导（auto f() { return 1; }）
>
> **实现策略**：
> - 先处理初始化表达式的类型
> - 根据声明符的修饰符调整
> - 生成最终类型

**Checkpoint：** auto 和 decltype 推导测试通过

---

### Task 4.2.4 用户定义类型（类、枚举）

**目标：** 实现类类型和枚举类型

**开发要点：**

- **E4.2.4.1** 扩展 `include/nova-cc/AST/Type.h`：
  ```cpp
  class RecordType : public Type {
    RecordDecl *Decl;
    
  public:
    RecordType(RecordDecl *D) : Type(TC_Record), Decl(D) {}
    RecordDecl* getDecl() const { return Decl; }
    
    bool isStructure() const;
    bool isClass() const;
    bool isUnion() const;
    
    void dump(raw_ostream &OS) const override;
  };
  
  class EnumType : public Type {
    EnumDecl *Decl;
    
  public:
    EnumType(EnumDecl *D) : Type(TC_Enum), Decl(D) {}
    EnumDecl* getDecl() const { return Decl; }
    
    bool isScoped() const;
    QualType getUnderlyingType() const;
    
    void dump(raw_ostream &OS) const override;
  };
  ```

**开发关键点提示：**
> 请为 nova-cc 实现用户定义类型。
>
> **RecordType**：
> - 表示类、结构、联合
> - 关联到 RecordDecl
> - 提供成员访问接口
>
> **EnumType**：
> - 表示枚举
> - 支持有作用域枚举（enum class）
> - 支持无作用域枚举（enum）
> - 关联到 EnumDecl
>
> **不完整类型**：
> - 前向声明的类：class Foo;
> - 在定义前只能声明指针和引用
> - 定义完成后补全类型信息
>
> **类型完整性检查**：
> - isCompleteType()：是否完整
> - 需要完整类型的操作：sizeof、变量定义、基类

**Checkpoint：** 类类型和枚举类型测试通过；前向声明处理正确

---

### Task 4.2.5 ASTContext 类型工厂

**目标：** 实现类型对象的工厂方法，确保类型唯一性

**开发要点：**

- **E4.2.5.1** 扩展 `include/nova-cc/AST/ASTContext.h`：
  ```cpp
  class ASTContext {
    // 内建类型缓存
    BuiltinType *BuiltinTypes[BuiltinType::LongDouble + 1];
    
    // 派生类型缓存（使用 FoldingSet）
    llvm::FoldingSet<PointerType> PointerTypes;
    llvm::FoldingSet<ReferenceType> ReferenceTypes;
    llvm::FoldingSet<ArrayType> ArrayTypes;
    
  public:
    // === 内建类型 ===
    QualType getVoidType();
    QualType getBoolType();
    QualType getIntType();
    QualType getLongType();
    QualType getFloatType();
    QualType getDoubleType();
    
    // === 派生类型 ===
    QualType getPointerType(QualType T);
    QualType getReferenceType(QualType T);
    QualType getArrayType(QualType T, unsigned Size);
    QualType getFunctionType(QualType Ret, ArrayRef<QualType> Params, bool VarArgs);
    
    // === 用户定义类型 ===
    QualType getRecordType(RecordDecl *D);
    QualType getEnumType(EnumDecl *D);
  };
  ```

- **E4.2.5.2** 实现类型唯一化：
  ```cpp
  QualType ASTContext::getPointerType(QualType T) {
    llvm::FoldingSetNodeID ID;
    PointerType::Profile(ID, T);
    
    void *InsertPos;
    if (PointerType *PT = PointerTypes.FindNodeOrInsertPos(ID, InsertPos)) {
      return QualType(PT, 0);
    }
    
    // 创建新类型
    auto *PT = new (*this) PointerType(T);
    PointerTypes.InsertNode(PT, InsertPos);
    return QualType(PT, 0);
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现类型工厂。
>
> **类型唯一化**：
> - 相同的类型只创建一个实例
> - 使用 FoldingSet 实现（哈希 + 链表）
> - 指针比较即可判断类型相等
>
> **内存管理**：
> - 使用 BumpPtrAllocator 分配
> - 不需要手动释放
> - 编译结束时统一释放
>
> **类型 Profile**：
> - 为每种类型计算哈希值
> - 用于 FoldingSet 查找
> - 包含所有影响类型相等的信息
>
> **初始化时机**：
> - ASTContext 构造时创建所有内建类型
> - 派生类型按需创建并缓存

**Checkpoint：** 类型工厂测试通过；相同类型指针相同

---

## Stage 4.3 — 名字查找

### Task 4.3.1 Unqualified Lookup

**目标：** 实现非限定名字查找

**开发要点：**

- **E4.3.1.1** 创建 `include/nova-cc/Sema/Lookup.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Decl.h"
  #include "nova-cc/Sema/Scope.h"
  #include "llvm/ADT/SmallVector.h"
  
  namespace nova {
  
  enum LookupNameKind {
    LNK_LookupOrdinaryName,    // 普通名字查找
    LNK_LookupTagName,          // 标签查找（class/struct/enum）
    LNK_LookupMemberName,       // 成员查找
    LNK_LookupOperatorName,     // 操作符查找
    LNK_LookupNamespaceName,    // 命名空间查找
  };
  
  class LookupResult {
    llvm::SmallVector<NamedDecl*, 4> Decls;
    bool Ambiguous = false;
    
  public:
    LookupResult() = default;
    
    void addDecl(NamedDecl *D) { Decls.push_back(D); }
    ArrayRef<NamedDecl*> getDecls() const { return Decls; }
    
    bool empty() const { return Decls.empty(); }
    bool isSingleResult() const { return Decls.size() == 1; }
    bool isAmbiguous() const { return Ambiguous; }
    
    NamedDecl* getFoundDecl() const {
      return Decls.empty() ? nullptr : Decls.front();
    }
  };
  
  class Sema {
  public:
    // 非限定查找
    LookupResult LookupUnqualifiedName(IdentifierInfo *Name, Scope *S,
                                        LookupNameKind Kind = LNK_LookupOrdinaryName);
  };
  
  } // namespace nova
  ```

- **E4.3.1.2** 实现查找算法：
  ```cpp
  LookupResult Sema::LookupUnqualifiedName(IdentifierInfo *Name, Scope *S,
                                             LookupNameKind Kind) {
    LookupResult Result;
    
    // 从当前作用域向上查找
    for (Scope *Cur = S; Cur; Cur = Cur->getParent()) {
      if (NamedDecl *D = Cur->lookup(Name->getName())) {
        Result.addDecl(D);
        
        // 如果不是重载查找，找到第一个就返回
        if (Kind == LNK_LookupOrdinaryName && !isa<FunctionDecl>(D)) {
          return Result;
        }
      }
      
      // 处理 using 指令
      for (NamespaceDecl *NS : Cur->getUsingDirectives()) {
        if (NamedDecl *D = NS->lookup(Name->getName())) {
          Result.addDecl(D);
        }
      }
    }
    
    return Result;
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现非限定名字查找。
>
> **查找顺序**：
> 1. 当前块作用域
> 2. 外围块作用域
> 3. 函数参数作用域
> 4. 类作用域（如果是成员函数）
> 5. 命名空间作用域
> 6. 全局作用域
>
> **Using 指令处理**：
> - using namespace X；将 X 的成员纳入查找
> - using X::name；直接引入名字
>
> **标签查找**：
> - class/struct/enum 的查找略有不同
> - 允许变量和类型同名
> - 使用 LNK_LookupTagName 区分
>
> **查找结果**：
> - 空结果：未找到
> - 单一结果：找到唯一声明
> - 多结果：重载函数集
> - 歧义：多个非函数声明

**Checkpoint：** 名字查找测试通过；作用域链正确

---

### Task 4.3.2 Qualified Lookup

**目标：** 实现限定名字查找（X::name）

**开发要点：**

- **E4.3.2.1** 扩展 `include/nova-cc/Sema/Lookup.h`：
  ```cpp
  class NestedNameSpecifier {
  public:
    enum SpecifierKind {
      Identifier,     // 未解析的标识符
      Namespace,      // 命名空间
      Type,           // 类类型
      Global,         // ::（全局）
    };
    
  private:
    SpecifierKind Kind;
    union {
      IdentifierInfo *Identifier;
      NamespaceDecl *Namespace;
      Type *TypeSpec;
    };
    NestedNameSpecifier *Prefix;
    
  public:
    // ...
  };
  
  class Sema {
  public:
    // 限定查找
    LookupResult LookupQualifiedName(IdentifierInfo *Name,
                                       NestedNameSpecifier *NNS);
  };
  ```

- **E4.3.2.2** 实现限定查找：
  ```cpp
  LookupResult Sema::LookupQualifiedName(IdentifierInfo *Name,
                                           NestedNameSpecifier *NNS) {
    LookupResult Result;
    
    // 解析 NestedNameSpecifier
    DeclContext *DC = resolveNestedNameSpecifier(NNS);
    if (!DC) return Result;
    
    // 在指定上下文中查找
    if (NamedDecl *D = DC->lookup(Name->getName())) {
      Result.addDecl(D);
    }
    
    return Result;
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现限定名字查找。
>
> **NestedNameSpecifier**：
> - 表示 A::B::C 中的 A::B:: 部分
> - 可以包含命名空间、类、模板
> - 逐层解析
>
> **查找规则**：
> - 只在指定的上下文中查找
> - 不考虑 using 指令
> - 访问控制检查（private/protected）
>
> **特殊情况**：
> - ::name：全局名字查找
> - typename T::type：依赖类型
> - template T::template X：依赖模板
>
> **错误处理**：
> - 上下文不存在
> - 名字未找到
> - 访问权限不足

**Checkpoint：** 限定查找测试通过；访问控制正确

---

### Task 4.3.3 ADL（参数依赖查找）

**目标：** 实现参数依赖查找

**开发要点：**

- **E4.3.3.1** 创建 `include/nova-cc/Sema/ADL.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/Sema/Lookup.h"
  
  namespace nova {
  
  class ADL {
  public:
    // 执行 ADL 查找
    static void ArgumentDependentLookup(IdentifierInfo *Name,
                                          ArrayRef<Expr*> Args,
                                          LookupResult &Result);
    
  private:
    // 收集参数的关联命名空间
    static void CollectAssociatedNamespaces(QualType T,
                                             llvm::SmallPtrSet<NamespaceDecl*, 4> &Namespaces);
  };
  
  } // namespace nova
  ```

- **E4.3.3.2** 实现 ADL 算法：
  ```cpp
  void ADL::ArgumentDependentLookup(IdentifierInfo *Name,
                                      ArrayRef<Expr*> Args,
                                      LookupResult &Result) {
    llvm::SmallPtrSet<NamespaceDecl*, 4> Namespaces;
    
    // 收集所有参数的关联命名空间
    for (Expr *Arg : Args) {
      CollectAssociatedNamespaces(Arg->getType(), Namespaces);
    }
    
    // 在每个关联命名空间中查找
    for (NamespaceDecl *NS : Namespaces) {
      if (NamedDecl *D = NS->lookup(Name->getName())) {
        Result.addDecl(D);
      }
    }
  }
  
  void ADL::CollectAssociatedNamespaces(QualType T,
                                          llvm::SmallPtrSet<NamespaceDecl*, 4> &Namespaces) {
    // 类类型：类定义所在的命名空间
    if (auto *RD = dyn_cast<RecordType>(T.getTypePtr())) {
      if (NamespaceDecl *NS = dyn_cast<NamespaceDecl>(RD->getDecl()->getDeclContext())) {
        Namespaces.insert(NS);
      }
    }
    
    // 指针/引用：底层类型的命名空间
    // 枚举：枚举定义所在的命名空间
    // ...
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现 ADL。
>
> **ADL 规则**：
> - 函数调用时，除了普通查找，还在参数类型所在的命名空间中查找
> - 类类型：类定义所在的命名空间
> - 枚举类型：枚举定义所在的命名空间
> - 模板特化：模板参数和模板定义所在的命名空间
>
> **关联命名空间收集**：
> 1. 基本类型：无关联命名空间
> 2. 类类型：类定义所在的命名空间 + 基类的命名空间
> 3. 类模板：模板参数的关联命名空间 + 模板定义的命名空间
> 4. 枚举：枚举定义所在的命名空间
>
> **ADL 与 using**：
> - using 声明不影响 ADL
> - ADL 只考虑定义所在的命名空间
>
> **实例**：
> ```cpp
> namespace N {
>   struct X {};
>   void f(X);
> }
> N::X x;
> f(x);  // ADL 找到 N::f
> ```

**Checkpoint：** ADL 测试通过；std::swap 等典型用例正确

---

## Stage 4.4 — 重载决议

### Task 4.4.1 重载候选收集

**目标：** 收集重载函数候选集

**开发要点：**

- **E4.4.1.1** 创建 `include/nova-cc/Sema/Overload.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Decl.h"
  #include "nova-cc/AST/Expr.h"
  #include "nova-cc/AST/Type.h"
  #include "llvm/ADT/SmallVector.h"
  
  namespace nova {
  
  class OverloadCandidate {
    FunctionDecl *Function;
    llvm::SmallVector<QualType, 4> ConvertedArgs;
    bool Viable = false;
    
  public:
    OverloadCandidate(FunctionDecl *F) : Function(F) {}
    
    FunctionDecl* getFunction() const { return Function; }
    bool isViable() const { return Viable; }
    
    // 参数转换
    bool checkArgumentConversion(ArrayRef<Expr*> Args);
  };
  
  class OverloadCandidateSet {
    llvm::SmallVector<OverloadCandidate, 16> Candidates;
    
  public:
    void addCandidate(FunctionDecl *F);
    ArrayRef<OverloadCandidate> getCandidates() const { return Candidates; }
    
    // 执行重载决议
    FunctionDecl* resolve(ArrayRef<Expr*> Args);
  };
  
  } // namespace nova
  ```

**开发关键点提示：**
> 请为 nova-cc 实现重载候选收集。
>
> **候选来源**：
> 1. 普通查找结果
> 2. ADL 查找结果
> 3. 成员函数
> 4. 模板函数实例化
>
> **候选筛选**：
> 1. 参数数量匹配
> 2. 参数类型可转换
> 3. 模板参数可推导
> 4. 约束满足（C++20 Concepts）
>
> **候选分类**：
> - Viable：可行候选
> - Non-viable：不可行候选（用于错误诊断）
>
> **模板处理**：
> - 函数模板需要先实例化
> - 部分排序决定模板特化
> - SFINAE 剔除无效候选

**Checkpoint：** 候选收集测试通过；模板实例化正确

---

### Task 4.4.2 参数转换与排序

**目标：** 实现参数转换检查和候选排序

**开发要点：**

- **E4.4.2.1** 创建 `include/nova-cc/Sema/Conversion.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Type.h"
  
  namespace nova {
  
  enum ConversionRank {
    CR_ExactMatch,      // 完全匹配
    CR_Promotion,       // 整型提升
    CR_Conversion,      // 标准转换
    CR_UserDefined,     // 用户定义转换
    CR_Ellipsis,        // 省略号匹配
    CR_BadConversion,   // 无法转换
  };
  
  class ImplicitConversion {
    ConversionRank Rank;
    
  public:
    static ImplicitConversion getConversion(QualType From, QualType To);
    
    ConversionRank getRank() const { return Rank; }
    bool isBad() const { return Rank == CR_BadConversion; }
    
    bool operator<(const ImplicitConversion &RHS) const;
  };
  
  } // namespace nova
  ```

- **E4.4.2.2** 实现候选排序：
  ```cpp
  bool OverloadCandidate::checkArgumentConversion(ArrayRef<Expr*> Args) {
    if (Function->getNumParams() > Args.size()) {
      // 参数不足，不可行
      return Viable = false;
    }
    
    for (unsigned I = 0; I < Args.size(); ++I) {
      QualType ParamType = Function->getParamType(I);
      QualType ArgType = Args[I]->getType();
      
      ImplicitConversion Conv = ImplicitConversion::getConversion(ArgType, ParamType);
      if (Conv.isBad()) {
        return Viable = false;
      }
    }
    
    return Viable = true;
  }
  
  bool OverloadCandidate::operator<(const OverloadCandidate &RHS) const {
    // 比较转换序列
    // 更好的转换序列 → 更好的候选
    // ...
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现参数转换和候选排序。
>
> **转换序列**：
> 1. 标准转换序列：左值转换 → 数值提升/转换 → 限定符调整
> 2. 用户定义转换：转换构造函数或转换运算符
> 3. 省略号转换：...
>
> **转换排序**：
> 1. Exact Match > Promotion > Conversion
> 2. 标准转换 > 用户定义转换 > 省略号
> 3. 更短的转换序列更好
>
> **候选比较**：
> 1. 逐参数比较转换序列
> 2. 至少一个参数更好，且没有参数更差
> 3. 如果无法区分，则歧义
>
> **特殊情况**：
> - 指针转换：派生类指针 → 基类指针
> - 引用绑定：临时量绑定到 const 引用
> - 列表初始化：{1, 2, 3} 转换为 std::vector

**Checkpoint：** 转换排序测试通过；歧义检测正确

---

### Task 4.4.3 最佳函数选择

**目标：** 实现最佳匹配函数的选择

**开发要点：**

- **E4.4.3.1** 实现重载决议：
  ```cpp
  FunctionDecl* OverloadCandidateSet::resolve(ArrayRef<Expr*> Args) {
    // 1. 检查所有候选的参数转换
    for (auto &C : Candidates) {
      C.checkArgumentConversion(Args);
    }
    
    // 2. 筛选可行候选
    llvm::SmallVector<OverloadCandidate*, 4> Viable;
    for (auto &C : Candidates) {
      if (C.isViable()) {
        Viable.push_back(&C);
      }
    }
    
    // 3. 无可行候选
    if (Viable.empty()) {
      // 报告错误：无匹配函数
      return nullptr;
    }
    
    // 4. 单一可行候选
    if (Viable.size() == 1) {
      return Viable[0]->getFunction();
    }
    
    // 5. 多个可行候选：排序
    OverloadCandidate *Best = Viable[0];
    bool Ambiguous = false;
    
    for (unsigned I = 1; I < Viable.size(); ++I) {
      if (*Viable[I] < *Best) {
        Best = Viable[I];
        Ambiguous = false;
      } else if (!(*Best < *Viable[I])) {
        Ambiguous = true;
      }
    }
    
    // 6. 歧义检查
    if (Ambiguous) {
      // 报告错误：调用歧义
      return nullptr;
    }
    
    return Best->getFunction();
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现最佳函数选择。
>
> **决议流程**：
> 1. 收集候选
> 2. 检查可行性
> 3. 排序候选
> 4. 选择最佳
> 5. 检查歧义
>
> **歧义情况**：
> - 多个候选同样好
> - 无法区分最佳候选
> - 报告所有歧义候选
>
> **错误诊断**：
> - 无匹配函数：列出所有候选和失败原因
> - 歧义调用：列出所有最佳候选
> - 详细指出参数不匹配的原因
>
> **模板特化**：
> - 更特化的模板优先
> - 偏特化排序规则
> - SFINAE 处理

**Checkpoint：** 重载决议测试通过；错误诊断完整

---

## Stage 4.5 — 语义检查

### Task 4.5.1 类型检查

**目标：** 实现完整的类型检查系统

**开发要点：**

- **E4.5.1.1** 创建 `include/nova-cc/Sema/TypeCheck.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Expr.h"
  #include "nova-cc/AST/Type.h"
  
  namespace nova {
  
  class TypeCheck {
    DiagnosticsEngine &Diags;
    
  public:
    TypeCheck(DiagnosticsEngine &D) : Diags(D) {}
    
    // === 表达式类型检查 ===
    bool CheckAssignment(QualType LHS, QualType RHS, SourceLocation Loc);
    bool CheckInitialization(QualType Dest, Expr *Init);
    bool CheckCall(FunctionDecl *F, ArrayRef<Expr*> Args);
    
    // === 语句类型检查 ===
    bool CheckCondition(Expr *Cond);
    bool CheckReturn(Expr *RetVal, QualType FuncRet);
    
    // === 类型兼容性 ===
    bool isTypeCompatible(QualType From, QualType To);
    QualType getCommonType(QualType T1, QualType T2);
  };
  
  } // namespace nova
  ```

**开发关键点提示：**
> 请为 nova-cc 实现类型检查。
>
> **赋值检查**：
> - const 对象不可赋值
> - 类型必须兼容
> - 引用必须绑定到正确类型
>
> **初始化检查**：
> - 直接初始化 vs 拷贝初始化
> - 列表初始化
> - 聚合初始化
> - 引用初始化
>
> **调用检查**：
> - 参数数量和类型
> - 默认参数
> - 可变参数
>
> **条件检查**：
> - 必须可转换为 bool
> - 整数、指针、类类型

**Checkpoint：** 类型检查测试通过

---

### Task 4.5.2 访问控制检查

**目标：** 实现访问控制检查

**开发要点：**

- **E4.5.2.1** 创建 `include/nova-cc/Sema/AccessControl.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Decl.h"
  
  namespace nova {
  
  enum AccessSpecifier {
    AS_public,
    AS_protected,
    AS_private,
  };
  
  class AccessControl {
  public:
    // 检查访问权限
    static bool CheckAccess(NamedDecl *Member, AccessSpecifier AS,
                             DeclContext *Context);
    
    // 获取成员访问级别
    static AccessSpecifier GetAccess(NamedDecl *D);
  };
  
  } // namespace nova
  ```

**开发关键点提示：**
> 请为 nova-cc 实现访问控制检查。
>
> **访问级别**：
> - public：任何代码可访问
> - protected：类本身和派生类可访问
> - private：仅类本身可访问
>
> **访问上下文**：
> - 成员函数可访问类的所有成员
> - 友元可访问类的所有成员
> - 派生类可访问基类的 protected 成员
>
> **检查时机**：
> - 成员访问（obj.member）
> - 继承检查
> - 友元声明

**Checkpoint：** 访问控制测试通过

---

### Task 4.5.3 语义诊断系统

**目标：** 实现完整的语义错误诊断

**开发要点：**

- **E4.5.3.1** 创建 `include/nova-Basic/DiagnosticSemaKinds.td`：
  ```tablegen
  // 语义分析错误
  
  def err_undeclared_var : Error<"use of undeclared identifier %0">;
  def err_redefinition : Error<"redefinition of %0">;
  def err_type_mismatch : Error<"cannot initialize a variable of type %0 with an %select{lvalue|rvalue}1 of type %2">;
  def err_ovl_no_viable_function : Error<"no matching function for call to %0">;
  def err_ovl_ambiguous_call : Error<"call to %0 is ambiguous">;
  def err_access_private : Error<"%0 is a private member of %1">;
  def err_access_protected : Error<"%0 is a protected member of %1">;
  def warn_unused_variable : Warning<"unused variable %0">;
  ```

- **E4.5.3.2** 实现诊断格式化：
  ```cpp
  void DiagnosticsEngine::report(SourceLocation Loc, DiagID ID, 
                                   ArrayRef<DiagnosticArg> Args) {
    // 获取诊断信息模板
    const DiagnosticInfo &Info = getDiagnosticInfo(ID);
    
    // 格式化消息
    std::string Message = formatDiagnostic(Info, Args);
    
    // 输出
    emitDiagnostic(Loc, Info.Level, Message);
  }
  ```

**开发关键点提示：**
> 请为 nova-cc 实现语义诊断系统。
>
> **诊断级别**：
> - Error：必须修复
> - Warning：建议修复
> - Note：附加信息
> - Remark：备注
>
> **诊断内容**：
> - 使用占位符：%0, %1, %2
> - 选择器：%select{a|b|c}0
> - 类型格式化
>
> **诊断位置**：
> - 主诊断：错误发生位置
> - 附注：相关位置（声明、定义等）
>
> **常见错误**：
> - 未声明标识符
> - 重定义
> - 类型不匹配
> - 无匹配函数
> - 访问权限

**Checkpoint：** 诊断系统测试通过；错误信息清晰

---

### Task 4.5.4 常量表达式求值

**目标：** 实现常量表达式求值

**开发要点：**

- **E4.5.4.1** 创建 `include/nova-cc/Sema/ConstantExpr.h`：
  ```cpp
  #pragma once
  
  #include "nova-cc/AST/Expr.h"
  #include "llvm/ADT/APInt.h"
  #include "llvm/ADT/APFloat.h"
  
  namespace nova {
  
  class ConstantExprEvaluator {
    ASTContext &Context;
    
  public:
    ConstantExprEvaluator(ASTContext &C) : Context(C) {}
    
    // 求值入口
    bool Evaluate(Expr *E, llvm::APInt &Result);
    bool Evaluate(Expr *E, llvm::APFloat &Result);
    
    // 检查是否常量表达式
    bool isConstantExpr(Expr *E);
    
    // constexpr 函数求值
    bool EvaluateCall(FunctionDecl *F, ArrayRef<Expr*> Args, llvm::APInt &Result);
  };
  
  } // namespace nova
  ```

**开发关键点提示：**
> 请为 nova-cc 实现常量表达式求值。
>
> **常量表达式场景**：
> - 数组大小：int arr[10];
> - 模板参数：template<int N>
> - case 标签：case 1:
> - 枚举值：enum { X = 10 };
> - constexpr 变量
>
> **可求值的表达式**：
> - 字面量
> - constexpr 变量
> - constexpr 函数调用
> - 运算符表达式
>
> **不可求值的情况**：
> - 函数调用（非 constexpr）
> - 非常量变量
> - 不允许的操作（如 dynamic_cast）
>
> **求值过程**：
> - 递归求值子表达式
> - 执行运算
> - 返回结果

**Checkpoint：** 常量表达式求值测试通过

---

### Task 4.5.5 语义分析集成测试

**目标：** 建立语义分析的完整测试覆盖

**开发要点：**

- **E4.5.5.1** 创建测试文件：
  ```
  tests/unit/Sema/
  ├── SymbolTableTest.cpp
  ├── ScopeTest.cpp
  ├── TypeSystemTest.cpp
  ├── NameLookupTest.cpp
  ├── OverloadResolutionTest.cpp
  └── TypeCheckTest.cpp
  ```

- **E4.5.5.2** 创建回归测试：
  ```
  tests/lit/Sema/
  ├── basic-var.test           # 变量声明
  ├── function-decl.test       # 函数声明
  ├── overload.test            # 重载决议
  ├── class-member.test        # 类成员
  ├── template.test            # 模板
  └── cpp26-features.test      # C++26 特性
  ```

**开发关键点提示：**
> 请为 nova-cc 建立语义分析测试。
>
> **单元测试重点**：
> - 符号表增删查
> - 作用域链正确性
> - 类型推导正确性
> - 名字查找边界情况
> - 重载决议排序
>
> **回归测试重点**：
> - 实际 C++ 代码解析
> - 错误诊断质量
> - 边界情况处理
> - 与 Clang 行为对比
>
> **测试覆盖率目标**：
> - 核心功能 ≥ 80%
> - 错误路径 ≥ 60%

**Checkpoint：** 测试覆盖率 ≥ 80%；所有测试通过

---

## 📋 Phase 4 验收检查清单

```
[ ] Sema 主类实现完成
[ ] 符号表实现完成
[ ] Scope 与作用域链实现完成
[ ] DeclContext 实现完成
[ ] 内建类型实现完成
[ ] 派生类型实现完成
[ ] 类型推导实现完成
[ ] 用户定义类型实现完成
[ ] ASTContext 类型工厂实现完成
[ ] Unqualified Lookup 实现完成
[ ] Qualified Lookup 实现完成
[ ] ADL 实现完成
[ ] 重载候选收集实现完成
[ ] 参数转换与排序实现完成
[ ] 最佳函数选择实现完成
[ ] 类型检查实现完成
[ ] 访问控制检查实现完成
[ ] 语义诊断系统实现完成
[ ] 常量表达式求值实现完成
[ ] 测试覆盖率 ≥ 80%
```

---

*Phase 4 完成标志：语义分析框架完整实现；能对 C++ 程序进行完整的语义分析；符号表、类型系统、名字查找、重载决议、类型检查等核心功能全部就绪；测试通过，覆盖率 ≥ 80%。*
