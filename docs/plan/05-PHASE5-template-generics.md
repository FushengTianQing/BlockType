# Phase 5：模板与泛型
> **目标：** 实现完整的模板系统，包括类模板、函数模板、模板参数推导、Concepts、变参模板等
> **前置依赖：** Phase 0-4 完成（语义分析基础）
> **验收标准：** 能正确编译和实例化标准库级别的模板代码

---

## 📌 阶段总览

```
Phase 5 包含 5 个 Stage，共 15 个 Task，预计 6 周完成。
依赖链：Stage 5.1 → Stage 5.2 → Stage 5.3 → Stage 5.4 → Stage 5.5
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 5.1** | 模板基础 | 模板声明、参数、实例化框架 | 1.5 周 |
| **Stage 5.2** | 模板参数推导 | 类型推导、SFINAE、部分特化 | 1.5 周 |
| **Stage 5.3** | 变参模板 | 参数包、Pack Expansion | 1 周 |
| **Stage 5.4** | Concepts | 约束、requires 表达式 | 1.5 周 |
| **Stage 5.5** | 模板特化与测试 | 显式特化、偏特化、测试 | 1.5 周 |

**Phase 5 架构图：**

```
┌─────────────────────────────────────────────────────────────┐
│                    Template System                           │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Template Declarations                    │    │
│  │  - TemplateDecl (类模板、函数模板、变量模板)          │    │
│  │  - TemplateParameter (类型参数、非类型参数、模板参数) │    │
│  │  - TemplateArgument (实参)                           │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │            Template Argument Deduction               │    │
│  │  - DeduceTemplateArguments()                         │    │
│  │  - SFINAE 处理                                       │    │
│  │  - 部分排序                                          │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │             Template Instantiation                    │    │
│  │  - ClassTemplateInstantiation                         │    │
│  │  - FunctionTemplateInstantiation                      │    │
│  │  - 显式实例化控制                                     │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │                  Concepts (C++20)                     │    │
│  │  - ConceptDecl                                        │    │
│  │  - RequiresExpr                                       │    │
│  │  - Constraint Satisfaction                            │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │               Variadic Templates                      │    │
│  │  - ParameterPack                                      │    │
│  │  - PackExpansion                                      │    │
│  │  - Fold Expressions                                   │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

---

## Stage 5.1 — 模板基础

### Task 5.1.1 模板声明 AST

**目标：** 定义模板声明的 AST 节点

**开发要点：**

- **E5.1.1.1** 创建 `include/BlockType/AST/TemplateBase.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/Decl.h"
  #include "BlockType/AST/Type.h"
  #include "BlockType/Basic/LLVM.h"
  #include "llvm/ADT/SmallVector.h"
  #include "llvm/ADT/PointerUnion.h"
  
  namespace BlockType {
  
  // === 模板参数 ===
  
  class TemplateParameter {
  public:
    enum Kind {
      Type,      // typename T
      NonType,   // int N
      Template,  // template<typename> class T
    };
    
  private:
    Kind K;
    unsigned Index;    // 参数索引
    unsigned Depth;    // 嵌套深度
    
  public:
    TemplateParameter(Kind k, unsigned I, unsigned D) : K(k), Index(I), Depth(D) {}
    
    Kind getKind() const { return K; }
    unsigned getIndex() const { return Index; }
    unsigned getDepth() const { return Depth; }
    
    bool isTypeParameter() const { return K == Type; }
    bool isNonTypeParameter() const { return K == NonType; }
    bool isTemplateParameter() const { return K == Template; }
  };
  
  class TemplateTypeParmDecl : public TemplateParameter {
    IdentifierInfo *Name;
    QualType DefaultArg;  // 默认实参
    
  public:
    TemplateTypeParmDecl(unsigned I, unsigned D, IdentifierInfo *N)
      : TemplateParameter(Type, I, D), Name(N) {}
    
    IdentifierInfo* getIdentifier() const { return Name; }
    QualType getDefaultArgument() const { return DefaultArg; }
    void setDefaultArgument(QualType T) { DefaultArg = T; }
  };
  
  class NonTypeTemplateParmDecl : public TemplateParameter {
    IdentifierInfo *Name;
    QualType Type;
    Expr *DefaultArg;
    
  public:
    NonTypeTemplateParmDecl(unsigned I, unsigned D, IdentifierInfo *N, QualType T)
      : TemplateParameter(NonType, I, D), Name(N), Type(T) {}
    
    IdentifierInfo* getIdentifier() const { return Name; }
    QualType getType() const { return Type; }
    Expr* getDefaultArgument() const { return DefaultArg; }
  };
  
  class TemplateTemplateParmDecl : public TemplateParameter {
    IdentifierInfo *Name;
    TemplateParameterList *Params;
    TemplateDecl *DefaultArg;
    
  public:
    TemplateTemplateParmDecl(unsigned I, unsigned D, IdentifierInfo *N,
                              TemplateParameterList *P)
      : TemplateParameter(Template, I, D), Name(N), Params(P) {}
    
    IdentifierInfo* getIdentifier() const { return Name; }
    TemplateParameterList* getTemplateParameters() const { return Params; }
  };
  
  // === 模板参数列表 ===
  
  class TemplateParameterList {
    llvm::SmallVector<TemplateParameter*, 4> Params;
    SourceLocation LAngleLoc, RAngleLoc;
    
  public:
    TemplateParameterList(SourceLocation L, SourceLocation R)
      : LAngleLoc(L), RAngleLoc(R) {}
    
    void addParam(TemplateParameter *P) { Params.push_back(P); }
    ArrayRef<TemplateParameter*> getParams() const { return Params; }
    unsigned size() const { return Params.size(); }
    
    SourceLocation getLAngleLoc() const { return LAngleLoc; }
    SourceLocation getRAngleLoc() const { return RAngleLoc; }
  };
  
  // === 模板实参 ===
  
  class TemplateArgument {
  public:
    enum ArgKind {
      Type,        // 类型实参
      Declaration, // 声明实参
      Integral,    // 整型常量
      Template,    // 模板实参
      Pack,        // 参数包
      Null,        // 空实参
    };
    
  private:
    ArgKind Kind;
    union {
      QualType TypeArg;
      ValueDecl *DeclArg;
      llvm::APSInt *IntegralArg;
      TemplateDecl *TemplateArg;
      llvm::SmallVector<TemplateArgument, 4> *PackArg;
    };
    
  public:
    // 构造函数
    TemplateArgument(QualType T) : Kind(Type), TypeArg(T) {}
    TemplateArgument(ValueDecl *D) : Kind(Declaration), DeclArg(D) {}
    TemplateArgument(const llvm::APSInt &I);
    
    ArgKind getKind() const { return Kind; }
    bool isType() const { return Kind == Type; }
    bool isPack() const { return Kind == Pack; }
    
    QualType getAsType() const { assert(isType()); return TypeArg; }
  };
  
  } // namespace BlockType
  ```

- **E5.1.1.2** 创建 `include/BlockType/AST/DeclTemplate.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/Decl.h"
  #include "BlockType/AST/TemplateBase.h"
  
  namespace BlockType {
  
  // === 模板声明基类 ===
  
  class TemplateDecl : public NamedDecl {
    TemplateParameterList *Params;
    NamedDecl *TemplatedDecl;  // 被模板包装的声明
    
  protected:
    TemplateDecl(Kind DK, DeclContext *DC, SourceLocation L, 
                 IdentifierInfo *Id, TemplateParameterList *P)
      : NamedDecl(DK, DC, L, Id), Params(P) {}
    
  public:
    TemplateParameterList* getTemplateParameters() const { return Params; }
    NamedDecl* getTemplatedDecl() const { return TemplatedDecl; }
    
    // 获取模板参数数量
    unsigned getNumTemplateParameters() const { return Params->size(); }
  };
  
  // === 类模板 ===
  
  class ClassTemplateDecl : public TemplateDecl {
    CXXRecordDecl *TemplatedClass;
    
    // 特化列表
    llvm::SmallVector<ClassTemplateSpecializationDecl*, 4> Specializations;
    
  public:
    ClassTemplateDecl(DeclContext *DC, SourceLocation L, 
                       IdentifierInfo *Id, TemplateParameterList *Params,
                       CXXRecordDecl *RD)
      : TemplateDecl(ClassTemplate, DC, L, Id, Params), 
        TemplatedClass(RD) {}
    
    CXXRecordDecl* getTemplatedDecl() const { return TemplatedClass; }
    
    // 获取或创建特化
    ClassTemplateSpecializationDecl* findSpecialization(ArrayRef<TemplateArgument> Args);
    ClassTemplateSpecializationDecl* getSpecialization(ArrayRef<TemplateArgument> Args);
  };
  
  // === 函数模板 ===
  
  class FunctionTemplateDecl : public TemplateDecl {
    FunctionDecl *TemplatedFunction;
    
  public:
    FunctionTemplateDecl(DeclContext *DC, SourceLocation L,
                          IdentifierInfo *Id, TemplateParameterList *Params,
                          FunctionDecl *FD)
      : TemplateDecl(FunctionTemplate, DC, L, Id, Params),
        TemplatedFunction(FD) {}
    
    FunctionDecl* getTemplatedDecl() const { return TemplatedFunction; }
  };
  
  // === 类模板特化 ===
  
  class ClassTemplateSpecializationDecl : public CXXRecordDecl {
    ClassTemplateDecl *SpecializedTemplate;
    llvm::SmallVector<TemplateArgument, 4> TemplateArgs;
    
  public:
    ClassTemplateSpecializationDecl(DeclContext *DC, SourceLocation L,
                                     ClassTemplateDecl *TD,
                                     ArrayRef<TemplateArgument> Args);
    
    ClassTemplateDecl* getSpecializedTemplate() const { return SpecializedTemplate; }
    ArrayRef<TemplateArgument> getTemplateArgs() const { return TemplateArgs; }
  };
  
  } // namespace BlockType
  ```

**开发关键点提示：**
> 请为 BlockType 定义模板声明的 AST 节点。
>
> **模板参数分类**：
> 1. 类型参数：typename T, class T
> 2. 非类型参数：int N, auto X
> 3. 模板参数：template<typename> class T
>
> **模板声明分类**：
> 1. 类模板：template<typename T> class vector { ... };
> 2. 函数模板：template<typename T> void swap(T& a, T& b);
> 3. 变量模板：template<typename T> constexpr T pi = T(3.14159);
> 4. 别名模板：template<typename T> using Vec = vector<T>;
>
> **模板实参**：
> - 类型：vector<int> 中的 int
> - 常量：array<int, 10> 中的 10
> - 模板：vector<vector<int>> 中的 vector<int>
> - 参数包：tuple<int, double, char> 中的 int, double, char
>
> **AST 关系**：
> - TemplateDecl 包含 TemplateParameterList
> - TemplateDecl 包含 TemplatedDecl（实际的类/函数声明）
> - ClassTemplateSpecializationDecl 引用 ClassTemplateDecl 和 TemplateArguments

**Checkpoint：** 模板 AST 定义完成；能表示 std::vector<int> 等模板

---

### Task 5.1.2 模板解析

**目标：** 实现模板声明的解析

**开发要点：**

- **E5.1.2.1** 扩展 Parser 以解析模板声明：
  ```cpp
  // src/Parse/ParseTemplate.cpp
  
  Decl* Parser::ParseTemplateDeclaration() {
    // 1. 解析 template 关键字
    if (!ConsumeToken(tok::kw_template)) {
      return nullptr;
    }
    
    // 2. 解析模板参数列表
    TemplateParameterList *Params = ParseTemplateParameterList();
    if (!Params) {
      return nullptr;
    }
    
    // 3. 解析被模板包装的声明
    Decl *D = ParseDeclaration();
    if (!D) {
      return nullptr;
    }
    
    // 4. 创建模板声明
    if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
      return Actions.ActOnClassTemplateDecl(RD, Params);
    } else if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      return Actions.ActOnFunctionTemplateDecl(FD, Params);
    }
    
    return D;
  }
  
  TemplateParameterList* Parser::ParseTemplateParameterList() {
    if (!ConsumeToken(tok::less)) {
      Diag(tok::less) << "expected '<' after 'template'";
      return nullptr;
    }
    
    auto *Params = new TemplateParameterList(Tok.getLocation(), SourceLocation());
    
    do {
      TemplateParameter *P = ParseTemplateParameter();
      if (P) Params->addParam(P);
      
      if (!TryConsumeToken(tok::comma)) {
        break;
      }
    } while (!Tok.is(tok::greater));
    
    if (!ConsumeToken(tok::greater)) {
      Diag(tok::greater) << "expected '>' at end of template parameter list";
      return nullptr;
    }
    
    return Params;
  }
  
  TemplateParameter* Parser::ParseTemplateParameter() {
    // typename T
    // class T
    // int N
    // template<typename> class T
    
    if (Tok.is(tok::kw_typename) || Tok.is(tok::kw_class)) {
      // 类型参数
      return ParseTypeParameter();
    } else if (Tok.is(tok::kw_template)) {
      // 模板参数
      return ParseTemplateTemplateParameter();
    } else {
      // 非类型参数
      return ParseNonTypeParameter();
    }
  }
  ```

- **E5.1.2.2** 解析模板 ID 和实参：
  ```cpp
  TypeSpecifier* Parser::ParseTemplateId(IdentifierInfo *Name) {
    // vector<int>
    
    if (!TryConsumeToken(tok::less)) {
      // 不是模板 ID
      return Actions.ActOnIdentifier(Name);
    }
    
    llvm::SmallVector<TemplateArgument, 4> Args;
    
    do {
      TemplateArgument Arg = ParseTemplateArgument();
      Args.push_back(Arg);
    } while (TryConsumeToken(tok::comma));
    
    ConsumeToken(tok::greater);
    
    return Actions.ActOnTemplateId(Name, Args);
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现模板解析。
>
> **解析流程**：
> 1. template 关键字
> 2. < 参数列表 >
> 3. 声明（类/函数/变量/别名）
>
> **参数解析**：
> - typename T：类型参数
> - typename T = int：带默认实参
> - int N：非类型参数
> - auto N：C++17 非类型参数推导
> - template<typename> class T：模板参数
>
> **实参解析**：
> - vector<int>：类型实参
> - array<int, 10>：混合实参
> - tuple<int, double, char>：参数包
>
> **嵌套模板处理**：
> - vector<vector<int>>
> - 注意 >> 歧义（C++11 前需要 > >）
>
> **错误恢复**：
> - 缺少 >
> - 参数类型不正确
> - 默认实参位置错误

**Checkpoint：** 模板解析测试通过；能解析 std::vector<std::map<int, double>>

---

### Task 5.1.3 模板实例化框架

**目标：** 实现模板实例化的核心框架

**开发要点：**

- **E5.1.3.1** 创建 `include/BlockType/Sema/TemplateInstantiation.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/DeclTemplate.h"
  #include "BlockType/AST/TemplateBase.h"
  
  namespace BlockType {
  
  class TemplateInstantiator {
    Sema &SemaRef;
    
  public:
    TemplateInstantiator(Sema &S) : SemaRef(S) {}
    
    // === 类模板实例化 ===
    ClassTemplateSpecializationDecl* 
    InstantiateClassTemplate(ClassTemplateDecl *Template,
                               ArrayRef<TemplateArgument> Args);
    
    // === 函数模板实例化 ===
    FunctionDecl* InstantiateFunctionTemplate(FunctionTemplateDecl *Template,
                                               ArrayRef<TemplateArgument> Args);
    
  private:
    // 替换模板参数为实参
    QualType SubstituteTemplateArguments(QualType T, 
                                          const TemplateArgumentList &Args);
    Expr* SubstituteTemplateArguments(Expr *E,
                                       const TemplateArgumentList &Args);
    Decl* SubstituteTemplateArguments(Decl *D,
                                       const TemplateArgumentList &Args);
  };
  
  } // namespace BlockType
  ```

- **E5.1.3.2** 实现类模板实例化：
  ```cpp
  ClassTemplateSpecializationDecl*
  TemplateInstantiator::InstantiateClassTemplate(ClassTemplateDecl *Template,
                                                   ArrayRef<TemplateArgument> Args) {
    // 1. 检查是否已经实例化
    if (auto *Spec = Template->findSpecialization(Args)) {
      return Spec;
    }
    
    // 2. 创建特化声明
    auto *Spec = new (SemaRef.Context) 
      ClassTemplateSpecializationDecl(/* DC */, Template->getLocation(),
                                       Template, Args);
    
    // 3. 替换模板参数
    CXXRecordDecl *Pattern = Template->getTemplatedDecl();
    
    // 复制成员并替换模板参数
    for (Decl *M : Pattern->decls()) {
      Decl *InstM = SubstituteTemplateArguments(M, Args);
      Spec->addDecl(InstM);
    }
    
    // 4. 注册特化
    Template->addSpecialization(Spec);
    
    return Spec;
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现模板实例化框架。
>
> **实例化时机**：
> - 隐式实例化：使用时触发（vector<int> v;）
> - 显式实例化：template class vector<int>;
> - 显式特化：template<> class vector<bool> { ... };
>
> **实例化过程**：
> 1. 查找或创建特化声明
> 2. 创建模板实参映射
> 3. 递归替换模板参数
> 4. 实例化成员（延迟到需要时）
>
> **替换规则**：
> - T 被替换为实参类型
> - N 被替换为实参值
> - T::type 被替换为实参类型的成员
>
> **依赖类型**：
> - 模板参数仍存在的类型
> - 需要延迟实例化
> - typename 关键字标记依赖类型
>
> **错误处理**：
> - 实参数量不匹配
> - 实参类型不满足约束
> - SFINAE：替换失败不是错误

**Checkpoint：** 模板实例化框架测试通过；能实例化简单类模板

---

### Task 5.1.4 模板名称查找

**目标：** 实现模板相关的名称查找

**开发要点：**

- **E5.1.4.1** 扩展名字查找以处理模板名称：
  ```cpp
  // src/Sema/SemaTemplate.cpp
  
  LookupResult Sema::LookupTemplateName(IdentifierInfo *Name, Scope *S) {
    LookupResult Result;
    
    // 1. 普通查找
    Result = LookupUnqualifiedName(Name, S);
    
    // 2. 如果找到模板名称
    if (Result.isSingleResult()) {
      NamedDecl *D = Result.getFoundDecl();
      
      if (auto *TD = dyn_cast<TemplateDecl>(D)) {
        // 找到模板
        return Result;
      }
    }
    
    // 3. 模板 ID 的情况：查找未特化的模板
    // ...
    
    return Result;
  }
  ```

- **E5.1.4.2** 处理依赖名称查找：
  ```cpp
  // T::type - 依赖名称
  // T::template X<int> - 依赖模板名称
  
  QualType Sema::HandleDependentNameType(IdentifierInfo *Name,
                                           NestedNameSpecifier *NNS) {
    // 1. 检查是否依赖
    if (!isDependent(NNS)) {
      // 非依赖：立即查找
      LookupResult R = LookupQualifiedName(Name, NNS);
      return R.getFoundDecl()->getType();
    }
    
    // 2. 依赖：创建 DependentNameType
    return Context.getDependentNameType(NNS, Name);
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现模板名称查找。
>
> **模板名称查找场景**：
> 1. 模板 ID：vector<int> 中的 vector
> 2. 成员模板：obj.template method<int>()
> 3. 依赖名称：T::type、T::template X<int>
>
> **依赖名称**：
> - 包含模板参数的名称
> - 无法在定义时解析
> - 需要在实例化时查找
>
> **typename 和 template 关键字**：
> - typename T::type：标记依赖类型名
> - T.template method<int>()：标记依赖模板名
> - 用于消除歧义
>
> **两阶段查找**：
> 1. 定义阶段：查找非依赖名称
> 2. 实例化阶段：查找依赖名称

**Checkpoint：** 模板名称查找测试通过；依赖名称正确处理

---

## Stage 5.2 — 模板参数推导

### Task 5.2.1 类型推导引擎

**目标：** 实现模板参数推导引擎

**开发要点：**

- **E5.2.1.1** 创建 `include/BlockType/Sema/TemplateDeduction.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/TemplateBase.h"
  #include "llvm/ADT/DenseMap.h"
  
  namespace BlockType {
  
  class TemplateDeductionInfo {
    // 推导结果
    llvm::DenseMap<unsigned, TemplateArgument> DeducedArgs;
    
  public:
    void addDeducedArg(unsigned Index, TemplateArgument Arg) {
      DeducedArgs[Index] = Arg;
    }
    
    TemplateArgument getDeducedArg(unsigned Index) const {
      auto It = DeducedArgs.find(Index);
      return It != DeducedArgs.end() ? It->second : TemplateArgument();
    }
    
    bool hasDeducedArg(unsigned Index) const {
      return DeducedArgs.find(Index) != DeducedArgs.end();
    }
    
    // 获取所有推导结果
    llvm::SmallVector<TemplateArgument, 4> getDeducedArgs() const;
  };
  
  class TemplateDeduction {
    Sema &SemaRef;
    
  public:
    TemplateDeduction(Sema &S) : SemaRef(S) {}
    
    // === 函数模板参数推导 ===
    bool DeduceFunctionTemplateArguments(FunctionTemplateDecl *FT,
                                          ArrayRef<Expr*> CallArgs,
                                          TemplateDeductionInfo &Info);
    
    // === 类型推导 ===
    bool DeduceTemplateArguments(QualType ParamType, QualType ArgType,
                                  TemplateDeductionInfo &Info);
    
    // === 部分推导 ===
    bool DeducePartialOrdering(TemplateDecl *P1, TemplateDecl *P2);
  };
  
  } // namespace BlockType
  ```

- **E5.2.1.2** 实现类型推导算法：
  ```cpp
  bool TemplateDeduction::DeduceTemplateArguments(QualType ParamType,
                                                    QualType ArgType,
                                                    TemplateDeductionInfo &Info) {
    // 1. 相同类型：无需推导
    if (ParamType == ArgType) {
      return true;
    }
    
    // 2. 模板参数类型：P 是 T
    if (auto *TTP = dyn_cast<TemplateTypeParmType>(ParamType.getTypePtr())) {
      // 检查是否已推导
      if (Info.hasDeduceArg(TTP->getIndex())) {
        // 检查一致性
        return Info.getDeducedArg(TTP->getIndex()).getAsType() == ArgType;
      }
      
      // 推导新值
      Info.addDeducedArg(TTP->getIndex(), TemplateArgument(ArgType));
      return true;
    }
    
    // 3. 指针类型：P 是 T*
    if (auto *P = dyn_cast<PointerType>(ParamType.getTypePtr())) {
      if (auto *A = dyn_cast<PointerType>(ArgType.getTypePtr())) {
        return DeduceTemplateArguments(P->getPointeeType(), 
                                        A->getPointeeType(), Info);
      }
      return false;
    }
    
    // 4. 引用类型
    if (auto *P = dyn_cast<ReferenceType>(ParamType.getTypePtr())) {
      if (auto *A = dyn_cast<ReferenceType>(ArgType.getTypePtr())) {
        return DeduceTemplateArguments(P->getReferentType(),
                                        A->getReferentType(), Info);
      }
      // 非 P 的引用可以绑定到 A
      return DeduceTemplateArguments(P->getReferentType(), ArgType, Info);
    }
    
    // 5. 模板 ID：P 是 X<T>
    if (auto *P = dyn_cast<TemplateSpecializationType>(ParamType.getTypePtr())) {
      if (auto *A = dyn_cast<TemplateSpecializationType>(ArgType.getTypePtr())) {
        // 检查模板相同
        if (P->getTemplateName() != A->getTemplateName()) {
          return false;
        }
        
        // 递归推导模板实参
        for (unsigned I = 0; I < P->getNumArgs(); ++I) {
          if (!DeduceTemplateArguments(P->getArg(I).getAsType(),
                                        A->getArg(I).getAsType(), Info)) {
            return false;
          }
        }
        return true;
      }
      return false;
    }
    
    // 6. 无法推导
    return false;
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现模板参数推导。
>
> **推导场景**：
> 1. 函数调用：f(1, 2) 推导 f 的模板参数
> 2. 函数指针：&f 推导 f 的模板参数
> 3. 类模板：make_pair(1, 2.0) 推导 pair 的模板参数
>
> **推导规则**：
> 1. P（参数类型）和 A（实参类型）必须匹配结构
> 2. 递归推导嵌套类型
> 3. 已推导的参数必须一致
> 4. 默认实参填补未推导的参数
>
> **特殊情况**：
> - 引用折叠：T& & → T&
> - cv 限定符：const T 可以推导为 const int
> - 数组到指针衰减：int[] 匹配 T*
>
> **推导失败**：
> - 类型结构不匹配
> - 已推导参数不一致
> - 非推导上下文（如 T::type）

**Checkpoint：** 模板参数推导测试通过

---

### Task 5.2.2 SFINAE 实现

**目标：** 实现 SFINAE（替换失败不是错误）

**开发要点：**

- **E5.2.2.1** 创建 `include/BlockType/Sema/SFINAE.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/TemplateBase.h"
  
  namespace BlockType {
  
  enum class SFINAEResult {
    Success,    // 替换成功
    Failure,    // 替换失败（SFINAE）
    Error,      // 硬错误
  };
  
  class SFINAEContext {
    bool InSFINAE = false;
    llvm::SmallVector<std::string, 4> FailureReasons;
    
  public:
    void enterSFINAE() { InSFINAE = true; }
    void exitSFINAE() { InSFINAE = false; }
    
    bool isSFINAE() const { return InSFINAE; }
    
    void addFailureReason(StringRef Reason) {
      FailureReasons.push_back(Reason.str());
    }
  };
  
  } // namespace BlockType
  ```

- **E5.2.2.2** 实现 SFINAE 检查：
  ```cpp
  SFINAEResult TemplateInstantiator::CheckSFINAE(QualType T, 
                                                   const TemplateArgumentList &Args) {
    // 尝试替换模板参数
    QualType Result = SubstituteTemplateArguments(T, Args);
    
    if (Result.isNull()) {
      // 替换失败
      return SFINAEResult::Failure;  // SFINAE，不是错误
    }
    
    return SFINAEResult::Success;
  }
  
  // 典型 SFINAE 场景
  // template<typename T>
  // auto f(T t) -> decltype(t.size()) { ... }  // T 没有 size() 时失败
  //
  // template<typename T, typename = void>
  // struct has_size : std::false_type {};
  //
  // template<typename T>
  // struct has_size<T, std::void_t<decltype(std::declval<T>().size())>> 
  //   : std::true_type {};
  ```

**开发关键点提示：**
> 请为 BlockType 实现 SFINAE。
>
> **SFINAE 场景**：
> 1. 无效类型：typename T::type 当 T 没有 type 成员
> 2. 无效表达式：decltype(x.y) 当 x 没有 y 成员
> 3. 无效模板实参：模板参数不满足约束
> 4. 无效函数签名：返回类型无效
>
> **立即上下文**：
> - SFINAE 只在"立即上下文"中有效
> - 函数体中的错误是硬错误
> - 类定义中的错误是硬错误（除非是成员声明）
>
> **SFINAE 用法**：
> 1. std::enable_if：条件启用函数
> 2. std::void_t：检测类型成员
> 3. decltype 陷阱：延迟检查
> 4. Concepts（C++20）：更清晰的语法
>
> **错误 vs 失败**：
> - Failure：替换失败，移除候选
> - Error：编译错误，停止编译

**Checkpoint：** SFINAE 测试通过；std::enable_if 模式正确

---

### Task 5.2.3 部分排序

**目标：** 实现模板部分排序

**开发要点：**

- **E5.2.3.1** 创建 `include/BlockType/Sema/TemplatePartialOrdering.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/DeclTemplate.h"
  
  namespace BlockType {
  
  class TemplatePartialOrdering {
  public:
    // 判断 P1 是否比 P2 更特化
    static bool isMoreSpecialized(TemplateDecl *P1, TemplateDecl *P2);
    
  private:
    // 生成虚拟实参用于排序
    static QualType generateDeducedType(TemplateParameter *P);
    static TemplateArgument generateDeducedArg(TemplateParameter *P);
  };
  
  } // namespace BlockType
  ```

- **E5.2.3.2** 实现排序算法：
  ```cpp
  bool TemplatePartialOrdering::isMoreSpecialized(TemplateDecl *P1, 
                                                    TemplateDecl *P2) {
    // 部分排序算法：
    // 1. 为 P1 的参数生成虚拟类型
    // 2. 尝试用虚拟类型推导 P2 的参数
    // 3. 反过来，用 P2 的虚拟类型推导 P1
    // 4. 如果 P1 能推导 P2 但反之不然，P1 更特化
    
    // P1 更特化：P1 是 P2 的子集
    // 例如：vector<T> vs vector<T*>
    //      vector<T*> 更特化
    
    TemplateDeductionInfo Info1, Info2;
    
    // 用 P1 推导 P2
    bool P1ToP2 = canDeduce(P1, P2, Info1);
    
    // 用 P2 推导 P1
    bool P2ToP1 = canDeduce(P2, P1, Info2);
    
    // P1 更特化：P2 可以推导 P1，但 P1 不能推导 P2
    return P2ToP1 && !P1ToP2;
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现模板部分排序。
>
> **部分排序规则**：
> 1. 更特化的模板优先
> 2. 特化程度：具体类型 > 指针/引用 > 模板参数
>
> **排序示例**：
> - template<typename T> void f(T)        // 最不特化
> - template<typename T> void f(T*)       // 较特化
> - template<typename T> void f(int*)     // 最特化
>
> **排序算法**：
> 1. 生成虚拟实参
> 2. 尝试互相推导
> 3. 判断特化程度
>
> **歧义情况**：
> - 两个模板同样特化
> - 报告歧义错误

**Checkpoint：** 部分排序测试通过；重载决议正确选择更特化模板

---

## Stage 5.3 — 变参模板

### Task 5.3.1 参数包解析

**目标：** 实现参数包的解析

**开发要点：**

- **E5.3.1.1** 扩展 `include/BlockType/AST/TemplateBase.h`：
  ```cpp
  class TemplateTypeParmDecl : public TemplateParameter {
    bool IsParameterPack = false;
    
  public:
    bool isParameterPack() const { return IsParameterPack; }
    void setParameterPack(bool P = true) { IsParameterPack = P; }
  };
  
  class NonTypeTemplateParmDecl : public TemplateParameter {
    bool IsParameterPack = false;
    
  public:
    bool isParameterPack() const { return IsParameterPack; }
    void setParameterPack(bool P = true) { IsParameterPack = P; }
  };
  ```

- **E5.3.1.2** 解析参数包：
  ```cpp
  TemplateParameter* Parser::ParseTypeParameter() {
    // typename T
    // typename... Ts (参数包)
    
    SourceLocation TypeNameLoc = ConsumeToken();
    
    bool IsPack = TryConsumeToken(tok::ellipsis);
    
    IdentifierInfo *Name = nullptr;
    if (Tok.is(tok::identifier)) {
      Name = Tok.getIdentifierInfo();
      ConsumeToken();
    }
    
    auto *P = new TemplateTypeParmDecl(/* index */, /* depth */, Name);
    P->setParameterPack(IsPack);
    
    // 默认实参
    if (TryConsumeToken(tok::equal)) {
      QualType Default = ParseTypeName();
      P->setDefaultArgument(Default);
    }
    
    return P;
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现参数包解析。
>
> **参数包语法**：
> - typename... Ts：类型参数包
> - int... Ns：非类型参数包
> - template<typename> class... Ts：模板参数包
>
> **参数包位置**：
> - 模板参数列表末尾
> - 可以在函数参数中使用
>
> **解析要点**：
> - ... 在参数名之前
> - 参数包可以有默认实参
> - 参数包展开在实例化时处理

**Checkpoint：** 参数包解析测试通过

---

### Task 5.3.2 Pack Expansion

**目标：** 实现参数包展开

**开发要点：**

- **E5.3.2.1** 创建 `include/BlockType/AST/PackExpansion.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/Expr.h"
  #include "BlockType/AST/Type.h"
  
  namespace BlockType {
  
  class PackExpansionExpr : public Expr {
    Expr *Pattern;        // 展开模式
    SourceLocation EllipsisLoc;
    
  public:
    PackExpansionExpr(Expr *P, SourceLocation L)
      : Expr(EK_PackExpansion), Pattern(P), EllipsisLoc(L) {}
    
    Expr* getPattern() const { return Pattern; }
    SourceLocation getEllipsisLoc() const { return EllipsisLoc; }
  };
  
  class PackExpansionType : public Type {
    QualType Pattern;
    
  public:
    PackExpansionType(QualType P) : Type(TC_PackExpansion), Pattern(P) {}
    QualType getPattern() const { return Pattern; }
  };
  
  } // namespace BlockType
  ```

- **E5.3.2.2** 实现包展开实例化：
  ```cpp
  llvm::SmallVector<Expr*, 4>
  TemplateInstantiator::ExpandPack(PackExpansionExpr *PE,
                                     const TemplateArgumentList &Args) {
    llvm::SmallVector<Expr*, 4> Result;
    
    // 获取参数包实参
    TemplateArgument PackArg = Args.getPackArgument();
    
    // 对包中的每个元素展开模式
    for (TemplateArgument Arg : PackArg.getPackElements()) {
      // 创建单元素实参列表
      TemplateArgumentList SingleArg(Arg);
      
      // 实例化模式
      Expr *Inst = SubstituteTemplateArguments(PE->getPattern(), SingleArg);
      Result.push_back(Inst);
    }
    
    return Result;
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现参数包展开。
>
> **展开场景**：
> 1. 函数参数：void f(Ts... args);
> 2. 继承：class D : public Bases... { };
> 3. 初始化：tuple<Ts...> t(args...);
> 4. 表达式：(args + ...)：折叠表达式
>
> **展开规则**：
> - 所有参数包同时展开
> - 参数包长度必须相同
> - 未展开的参数包是错误
>
> **实例化过程**：
> 1. 确定参数包长度
> 2. 复制模式 N 次
> 3. 每次替换对应的元素

**Checkpoint：** 参数包展开测试通过；std::tuple 模式正确

---

### Task 5.3.3 Fold Expressions

**目标：** 实现 C++17 折叠表达式

**开发要点：**

- **E5.3.3.1** 创建 `include/BlockType/AST/FoldExpr.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/Expr.h"
  
  namespace BlockType {
  
  enum class FoldOperator {
    Add, Sub, Mul, Div, Mod,
    And, Or, Xor,
    LShift, RShift,
    LAnd, LOr,
    Comma,
  };
  
  class FoldExpr : public Expr {
  public:
    enum Kind {
      UnaryLeft,   // (... op pack)
      UnaryRight,  // (pack op ...)
      Binary,      // (init op ... op pack)
    };
    
  private:
    Kind K;
    FoldOperator Op;
    Expr *LHS;  // init 或 pack
    Expr *RHS;  // pack 或 init
    
  public:
    FoldExpr(Kind K, FoldOperator Op, Expr *L, Expr *R)
      : Expr(EK_Fold), K(K), Op(Op), LHS(L), RHS(R) {}
    
    Kind getFoldKind() const { return K; }
    FoldOperator getOperator() const { return Op; }
    Expr* getLHS() const { return LHS; }
    Expr* getRHS() const { return RHS; }
  };
  
  } // namespace BlockType
  ```

- **E5.3.3.2** 实现折叠表达式求值：
  ```cpp
  Expr* TemplateInstantiator::InstantiateFoldExpr(FoldExpr *FE,
                                                    const TemplateArgumentList &Args) {
    // (args + ...) 展开为 args[0] + args[1] + ... + args[n-1]
    
    llvm::SmallVector<Expr*, 4> Elements = ExpandPack(FE->getLHS(), Args);
    
    if (Elements.empty()) {
      // 空包：返回单位元
      return getIdentityElement(FE->getOperator());
    }
    
    // 构建折叠表达式
    Expr *Result = Elements[0];
    for (unsigned I = 1; I < Elements.size(); ++I) {
      Result = new BinaryExpr(FE->getOperator(), Result, Elements[I]);
    }
    
    return Result;
  }
  
  Expr* getIdentityElement(FoldOperator Op) {
    // + 的单位元是 0
    // * 的单位元是 1
    // && 的单位元是 true
    // || 的单位元是 false
    // ...
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现折叠表达式。
>
> **折叠表达式语法**：
> - 一元右折叠：(pack op ...)
> - 一元左折叠：(... op pack)
> - 二元折叠：(init op ... op pack)
>
> **支持的操作符**：
> - 算术：+ - * / %
> - 位运算：& | ^ << >>
> - 逻辑：&& ||
> - 逗号：,
>
> **空包处理**：
> - + → 0
> - * → 1
> - && → true
> - || → false
> - , → void()
>
> **实例化过程**：
> 1. 展开参数包
> 2. 构建二元表达式链
> 3. 处理空包情况

**Checkpoint：** 折叠表达式测试通过；(args + ...) 正确展开

---

## Stage 5.4 — Concepts

### Task 5.4.1 Concept 声明

**目标：** 实现 C++20 Concept 声明

**开发要点：**

- **E5.4.1.1** 创建 `include/BlockType/AST/DeclConcept.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/Decl.h"
  #include "BlockType/AST/Expr.h"
  
  namespace BlockType {
  
  class ConceptDecl : public TemplateDecl {
    Expr *Constraint;  // 约束表达式
    
  public:
    ConceptDecl(DeclContext *DC, SourceLocation L,
                IdentifierInfo *Id, TemplateParameterList *Params,
                Expr *C)
      : TemplateDecl(Concept, DC, L, Id, Params), Constraint(C) {}
    
    Expr* getConstraintExpr() const { return Constraint; }
    
    // 检查类型是否满足 Concept
    bool isSatisfied(QualType T) const;
  };
  
  } // namespace BlockType
  ```

- **E5.4.1.2** 解析 Concept 声明：
  ```cpp
  Decl* Parser::ParseConceptDefinition() {
    // template<typename T>
    // concept Integral = is_integral<T>::value;
    
    // 1. 解析 template
    if (!ConsumeToken(tok::kw_template)) {
      return nullptr;
    }
    
    TemplateParameterList *Params = ParseTemplateParameterList();
    
    // 2. 解析 concept
    ConsumeToken(tok::kw_concept);
    
    IdentifierInfo *Name = Tok.getIdentifierInfo();
    ConsumeToken();
    
    // 3. 解析约束
    ConsumeToken(tok::equal);
    Expr *Constraint = ParseConstraintExpression();
    ConsumeToken(tok::semi);
    
    return Actions.ActOnConceptDecl(Name, Params, Constraint);
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现 Concept 声明。
>
> **Concept 语法**：
> ```cpp
> template<typename T>
> concept Integral = is_integral<T>::value;
> 
> template<typename T>
> concept Addable = requires(T a, T b) { a + b; };
> ```
>
> **Concept 用途**：
> 1. 约束模板参数
> 2. 简化 SFINAE
> 3. 更清晰的错误信息
>
> **预定义 Concepts**：
> - std::integral
> - std::floating_point
> - std::same_as
> - std::convertible_to
> - std::derived_from
> - std::invocable

**Checkpoint：** Concept 声明测试通过

---

### Task 5.4.2 Requires 表达式

**目标：** 实现 requires 表达式

**开发要点：**

- **E5.4.2.1** 创建 `include/BlockType/AST/ExprConcept.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/Expr.h"
  #include "BlockType/AST/Decl.h"
  
  namespace BlockType {
  
  class Requirement {
  public:
    enum Kind {
      Simple,      // expr;
      Type,        // typename T;
      Compound,    // { expr; }
    };
    
  private:
    Kind K;
    
  public:
    Requirement(Kind K) : K(K) {}
    Kind getKind() const { return K; }
  };
  
  class SimpleRequirement : public Requirement {
    Expr *Expression;
    
  public:
    SimpleRequirement(Expr *E) : Requirement(Simple), Expression(E) {}
    Expr* getExpression() const { return Expression; }
  };
  
  class TypeRequirement : public Requirement {
    QualType Type;
    
  public:
    TypeRequirement(QualType T) : Requirement(Type), Type(T) {}
    QualType getType() const { return Type; }
  };
  
  class CompoundRequirement : public Requirement {
    Expr *Expression;
    bool Noexcept;
    QualType ExpectedType;
    
  public:
    CompoundRequirement(Expr *E, bool NE, QualType T)
      : Requirement(Compound), Expression(E), Noexcept(NE), ExpectedType(T) {}
  };
  
  class RequiresExpr : public Expr {
    TemplateParameterList *Params;
    llvm::SmallVector<Requirement*, 4> Requirements;
    
  public:
    RequiresExpr(TemplateParameterList *P, ArrayRef<Requirement*> R)
      : Expr(EK_Requires), Params(P), Requirements(R) {}
    
    ArrayRef<Requirement*> getRequirements() const { return Requirements; }
  };
  
  } // namespace BlockType
  ```

**开发关键点提示：**
> 请为 BlockType 实现 requires 表达式。
>
> **Requires 表达式语法**：
> ```cpp
> requires (T a, T b) {
>   a + b;              // 简单要求
>   typename T::type;   // 类型要求
>   { a + b } -> same_as<T>;  // 复合要求
>   noexcept(a + b);    // noexcept 要求
> }
> ```
>
> **要求类型**：
> 1. 简单要求：表达式必须有效
> 2. 类型要求：类型必须存在
> 3. 复合要求：表达式结果必须满足约束
>
> **求值**：
> - requires 表达式在编译时求值
> - 返回 bool 值
> - 用于约束模板

**Checkpoint：** Requires 表达式测试通过

---

### Task 5.4.3 约束满足检查

**目标：** 实现约束满足检查

**开发要点：**

- **E5.4.3.1** 创建 `include/BlockType/Sema/ConstraintSatisfaction.h`：
  ```cpp
  #pragma once
  
  #include "BlockType/AST/DeclConcept.h"
  #include "BlockType/AST/ExprConcept.h"
  
  namespace BlockType {
  
  class ConstraintSatisfaction {
    Sema &SemaRef;
    
  public:
    ConstraintSatisfaction(Sema &S) : SemaRef(S) {}
    
    // 检查约束是否满足
    bool CheckConstraintSatisfaction(Expr *Constraint,
                                       const TemplateArgumentList &Args);
    
    // 检查 Concept 是否满足
    bool CheckConceptSatisfaction(ConceptDecl *Concept,
                                   ArrayRef<TemplateArgument> Args);
    
  private:
    bool EvaluateRequirement(Requirement *R, const TemplateArgumentList &Args);
  };
  
  } // namespace BlockType
  ```

**开发关键点提示：**
> 请为 BlockType 实现约束满足检查。
>
> **检查流程**：
> 1. 实例化约束表达式
> 2. 求值约束表达式
> 3. 返回 bool 结果
>
> **原子约束**：
> - 表达式求值为 bool
> - 类型必须有效
>
> **约束合取/析取**：
> - C1 && C2：两者都满足
> - C1 || C2：至少一个满足
>
> **部分排序**：
> - 更严格约束的模板更特化
> - 用于重载决议

**Checkpoint：** 约束满足检查测试通过

---

## Stage 5.5 — 模板特化与测试

### Task 5.5.1 显式特化

**目标：** 实现模板显式特化

**开发要点：**

- **E5.5.1.1** 扩展 `include/BlockType/AST/DeclTemplate.h`：
  ```cpp
  class ClassTemplateSpecializationDecl : public CXXRecordDecl {
  public:
    enum SpecializationKind {
      ExplicitInstantiationDeclaration,
      ExplicitInstantiationDefinition,
      ExplicitSpecialization,
    };
    
  private:
    SpecializationKind Kind;
    
  public:
    bool isExplicitSpecialization() const { return Kind == ExplicitSpecialization; }
  };
  ```

- **E5.5.1.2** 解析显式特化：
  ```cpp
  Decl* Parser::ParseExplicitSpecialization() {
    // template<> class vector<bool> { ... };
    
    ConsumeToken(tok::kw_template);
    ConsumeToken(tok::less);
    ConsumeToken(tok::greater);
    
    return ParseClassSpecifier();
  }
  ```

**开发关键点提示：**
> 请为 BlockType 实现模板显式特化。
>
> **显式特化语法**：
> ```cpp
> template<>
> class vector<bool> {
>   // 完全不同的实现
> };
> ```
>
> **函数模板特化**：
> ```cpp
> template<>
> void f<int>(int x) { /* ... */ }
> ```
>
> **成员特化**：
> ```cpp
> template<>
> int MyClass<int>::value = 42;
> ```

**Checkpoint：** 显式特化测试通过

---

### Task 5.5.2 偏特化

**目标：** 实现模板偏特化

**开发要点：**

- **E5.5.2.1** 创建 `include/BlockType/AST/DeclTemplate.h`：
  ```cpp
  class ClassTemplatePartialSpecializationDecl : public ClassTemplateSpecializationDecl {
    TemplateParameterList *TemplateParams;
    
  public:
    ClassTemplatePartialSpecializationDecl(DeclContext *DC, SourceLocation L,
                                            ClassTemplateDecl *TD,
                                            ArrayRef<TemplateArgument> Args,
                                            TemplateParameterList *Params);
    
    TemplateParameterList* getTemplateParameters() const { return TemplateParams; }
  };
  ```

**开发关键点提示：**
> 请为 BlockType 实现模板偏特化。
>
> **偏特化语法**：
> ```cpp
> template<typename T>
> class vector<T*> { /* 指针特化 */ };
> 
> template<typename T, size_t N>
> class array<T, 0> { /* 零大小特化 */ };
> ```
>
> **偏特化匹配**：
> - 选择最特化的偏特化
> - 使用部分排序规则
>
> **偏特化 vs 显式特化**：
> - 偏特化仍有模板参数
> - 显式特化没有模板参数

**Checkpoint：** 偏特化测试通过

---

### Task 5.5.3 模板测试

**目标：** 建立模板系统的完整测试覆盖

**开发要点：**

- **E5.5.3.1** 创建测试文件：
  ```
  tests/unit/Template/
  ├── TemplateParameterTest.cpp
  ├── TemplateDeductionTest.cpp
  ├── TemplateInstantiationTest.cpp
  ├── VariadicTemplateTest.cpp
  └── ConceptsTest.cpp
  ```

- **E5.5.3.2** 创建回归测试：
  ```
  tests/lit/Template/
  ├── basic-template.test
  ├── specialization.test
  ├── variadic.test
  ├── sfinae.test
  ├── concepts.test
  └── std-library.test
  ```

**开发关键点提示：**
> 请为 BlockType 建立模板测试。
>
> **测试重点**：
> 1. 模板参数推导正确性
> 2. SFINAE 行为正确性
> 3. 参数包展开正确性
> 4. Concepts 约束正确性
> 5. 与 std 库兼容性
>
> **覆盖率目标**：≥ 80%

**Checkpoint：** 测试覆盖率 ≥ 80%；所有测试通过

---

## 📋 Phase 5 验收检查清单

```
[ ] 模板声明 AST 定义完成
[ ] 模板解析实现完成
[ ] 模板实例化框架实现完成
[ ] 模板名称查找实现完成
[ ] 模板参数推导实现完成
[ ] SFINAE 实现完成
[ ] 部分排序实现完成
[ ] 参数包解析实现完成
[ ] Pack Expansion 实现完成
[ ] Fold Expressions 实现完成
[ ] Concept 声明实现完成
[ ] Requires 表达式实现完成
[ ] 约束满足检查实现完成
[ ] 显式特化实现完成
[ ] 偏特化实现完成
[ ] 测试覆盖率 ≥ 80%
```

---

*Phase 5 完成标志：模板系统完整实现；能编译标准库级别的模板代码；Concepts、变参模板、SFINAE 等核心功能就绪；测试通过，覆盖率 ≥ 80%。*
