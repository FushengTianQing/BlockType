# Phase 3：语法分析器（声明与类）
> **目标：** 完成声明解析和类/结构体定义解析，构建完整的声明 AST，支持模板声明
> **前置依赖：** Phase 2 完成（表达式/语句解析）
> **验收标准：** 能够正确解析 C++26 标准中的所有声明类型，包括类定义、模板声明、命名空间、using 声明等

---

## 📌 阶段总览

```
Phase 3 包含 4 个 Stage，共 14 个 Task，预计 8 周完成。
建议并行度：Stage 3.1 和 3.2 可部分并行，Stage 3.3 依赖 3.2，Stage 3.4 最后完成。
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 3.1** | 声明 AST 节点 | 所有声明 AST 节点定义 | 1.5 周 |
| **Stage 3.2** | 基础声明解析 | 变量、函数、类型别名声明 | 2 周 |
| **Stage 3.3** | 类与模板解析 | 类定义、模板声明、Concept | 3 周 |
| **Stage 3.4** | C++26 特性 + 集成测试 | C++26 新声明语法、完整测试 | 1.5 周 |

**Phase 3 架构图：**

```
Token 流 (from Preprocessor)
    │
    ▼
┌─────────────────────────────────────────────────────────┐
│                      Parser                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │ Declaration │  │    Class    │  │    Template     │  │
│  │   Parser    │  │   Parser    │  │     Parser      │  │
│  └──────┬──────┘  └──────┬──────┘  └────────┬────────┘  │
│         │                │                   │          │
│         ▼                ▼                   ▼          │
│  ┌──────────────────────────────────────────────────┐  │
│  │                Declaration AST                    │  │
│  │  VarDecl, FunctionDecl, RecordDecl,             │  │
│  │  TemplateDecl, ConceptDecl, NamespaceDecl...    │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

---

## Stage 3.1 — 声明 AST 节点定义

### Task 3.1.1 声明基类与基础声明节点

**目标：** 建立声明 AST 节点的层次结构

**开发要点：**

- **E1.1.1** 创建 `AST/Decl.h`，定义声明基类：
  ```cpp
  class Decl : public ASTNode {
  public:
    enum Kind {
      TranslationUnit, Namespace, NamespaceAlias,
      Var, VarTemplate, VarTemplateSpecialization, VarTemplatePartialSpecialization,
      Function, FunctionTemplate, CXXMethod, CXXConstructor, CXXDestructor, CXXConversion,
      Field, IndirectField,
      Record, CXXRecord, ClassTemplate, ClassTemplateSpecialization, ClassTemplatePartialSpecialization,
      Enum, EnumConstant,
      TemplateTypeParm, NonTypeTemplateParm, TemplateTemplateParm,
      TypeAlias, TypeAliasTemplate,
      Typedef, TypedefNameForLinkage,
      Using, UsingDirective, UsingPack, UsingEnum,
      UnresolvedUsing, UnresolvedUsingTypename, UnresolvedUsingValue,
      StaticAssert,
      Friend,
      Concept,  // C++20
      RequiresExpr,  // C++20
    };
    
  protected:
    DeclContext *Owner;  // 所属上下文
    SourceLocation BeginLoc, EndLoc;
    DeclarationName Name;  // 声明名称
    
  public:
    virtual Kind getKind() const = 0;
    DeclContext* getDeclContext() const { return Owner; }
    DeclarationName getName() const { return Name; }
    SourceRange getSourceRange() const { return {BeginLoc, EndLoc}; }
  };
  ```

- **E1.1.2** 实现 `DeclContext` — 声明上下文容器：
  ```cpp
  class DeclContext {
    Decl *Parent;  // 父声明
    std::vector<Decl*> Declarations;  // 子声明列表
    Decl::Kind ContextKind;  // 上下文类型
    
  public:
    void addDecl(Decl *D);
    Decl* lookup(DeclarationName Name);  // 名称查找
    DeclContext* getParent() const { return Parent ? Parent->getDeclContext() : nullptr; }
  };
  ```

- **E1.1.3** 实现 `TranslationUnitDecl` — 翻译单元根节点：
  ```cpp
  class TranslationUnitDecl : public Decl, public DeclContext {
    // 顶层声明的容器
  };
  ```

- **E1.1.4** 实现基础声明节点：
  ```cpp
  class VarDecl : public Decl {
    QualType Type;  // 变量类型
    Expr *Init;  // 初始化表达式
    StorageClass SC;  // 存储类（auto, register, static, extern）
    bool IsInline : 1;
    bool IsConstexpr : 1;
  };
  
  class FunctionDecl : public Decl, public DeclContext {
    QualType ReturnType;
    std::vector<ParmVarDecl*> Parameters;
    Stmt *Body;  // 函数体（复合语句或 = delete/ = default）
    bool IsInline : 1;
    bool IsConstexpr : 1;
    bool IsVirtual : 1;
    bool IsStatic : 1;
  };
  
  class ParmVarDecl : public VarDecl {
    // 函数参数
  };
  ```

**开发关键点提示：**
> 请实现声明 AST 基础架构 `include/zetacc/AST/Decl.h` 和 `src/AST/Decl.cpp`。
>
> **Decl 基类**：
> - 继承自 ASTNode
> - 包含 DeclContext* Owner（所属上下文）
> - 包含 DeclarationName Name（声明名称）
> - 包含 SourceLocation BeginLoc, EndLoc
> - 定义 Kind 枚举覆盖所有声明类型
> - 实现 getKind() 虚函数
> - 实现 classof() 静态方法
>
> **DeclContext**：
> - 作为声明的容器，管理子声明
> - 提供 addDecl(), lookup() 方法
> - 提供迭代器遍历子声明
> - 支持名称查找（简单实现，后续 Phase 优化）
>
> **基础声明类**：
> - TranslationUnitDecl：继承 Decl 和 DeclContext，作为 AST 根节点
> - VarDecl：变量声明，包含类型、初始化表达式、存储类
> - FunctionDecl：函数声明，继承 DeclContext，包含返回类型、参数列表、函数体
> - ParmVarDecl：继承 VarDecl，函数参数
>
> 所有类需要完整的 dump() 方法。

**Checkpoint：** 声明基类和基础声明节点编译通过；能创建 VarDecl 和 FunctionDecl

---

### Task 3.1.2 类型声明节点

**目标：** 定义类型相关的声明 AST 节点

**开发要点：**

- **E1.2.1** 实现类型声明基类：
  ```cpp
  class TypeDecl : public Decl {
  protected:
    Type *TypeForDecl;  // 类型信息（惰性创建）
  public:
    QualType getTypeForDecl() const;
  };
  ```

- **E1.2.2** 实现类型别名声明：
  ```cpp
  class TypedefDecl : public TypeDecl {
    QualType UnderlyingType;  // 底层类型
  };
  
  class TypeAliasDecl : public TypeDecl {
    QualType UnderlyingType;
    // using Name = Type;
  };
  ```

- **E1.2.3** 实现枚举声明：
  ```cpp
  class EnumDecl : public TypeDecl, public DeclContext {
    QualType IntegerType;  // 底层整数类型（可选）
    bool IsScoped : 1;  // enum class
    bool IsScopedUsingClassTag : 1;
    std::vector<EnumConstantDecl*> Enumerators;
  };
  
  class EnumConstantDecl : public Decl {
    QualType Type;
    Expr *InitExpr;  // 初始化表达式
    llvm::APSInt Value;  // 枚举值
  };
  ```

- **E1.2.4** 实现命名空间声明：
  ```cpp
  class NamespaceDecl : public Decl, public DeclContext {
    bool IsInline : 1;  // inline namespace
    NamespaceDecl *OriginalNamespace;  // 原始命名空间（用于扩展）
  };
  
  class NamespaceAliasDecl : public Decl {
    NamespaceDecl *AliasedNamespace;
  };
  ```

- **E1.2.5** 实现 using 声明：
  ```cpp
  class UsingDecl : public Decl {
    NestedNameSpecifier *Qualifier;  // 限定符
    DeclarationName Name;
    std::vector<UsingShadowDecl*> Shadows;
  };
  
  class UsingDirectiveDecl : public Decl {
    NamespaceDecl *NominatedNamespace;
  };
  ```

**开发关键点提示：**
> 请实现类型声明 AST 节点，追加到 `AST/Decl.h`。
>
> **TypedefDecl**：typedef 声明，包含底层类型
> **TypeAliasDecl**：using 别名声明（C++11）
> **EnumDecl**：枚举声明，继承 DeclContext，包含枚举器列表
> **EnumConstantDecl**：枚举常量，包含值和初始化表达式
> **NamespaceDecl**：命名空间声明，继承 DeclContext，支持 inline namespace
> **NamespaceAliasDecl**：命名空间别名
> **UsingDecl**：using 声明（引入名称）
> **UsingDirectiveDecl**：using namespace 指令
>
> 每个类需要 getKind()、classof()、dump() 方法。

**Checkpoint：** 类型声明节点编译通过；能创建枚举、命名空间、using 声明

---

### Task 3.1.3 RecordDecl 与 CXXRecordDecl

**目标：** 定义类和结构体的 AST 节点

**开发要点：**

- **E1.3.1** 实现记录声明基类：
  ```cpp
  class RecordDecl : public TypeDecl, public DeclContext {
  public:
    enum TagKind { Struct, Class, Union };
  protected:
    TagKind Tag;
    std::vector<FieldDecl*> Fields;
    bool IsCompleteDefinition : 1;
  public:
    TagKind getTagKind() const { return Tag; }
    void setCompleteDefinition(bool Complete) { IsCompleteDefinition = Complete; }
    bool isCompleteDefinition() const { return IsCompleteDefinition; }
    void addField(FieldDecl *F);
  };
  ```

- **E1.3.2** 实现字段声明：
  ```cpp
  class FieldDecl : public Decl {
    QualType Type;
    Expr *BitWidth;  // 位域宽度（可选）
    Expr *InClassInitializer;  // 类内初始化器（C++11）
    bool IsMutable : 1;
    unsigned BitFieldWidth;  // 位域宽度值
  };
  ```

- **E1.3.3** 实现 C++ 类声明：
  ```cpp
  class CXXRecordDecl : public RecordDecl {
    std::vector<CXXBaseSpecifier*> Bases;  // 基类列表
    CXXConstructorDecl *DefaultConstructor;
    CXXConstructorDecl *CopyConstructor;
    CXXConstructorDecl *MoveConstructor;
    CXXDestructorDecl *Destructor;
    CXXMethodDecl *CopyAssignment;
    CXXMethodDecl *MoveAssignment;
    
    // 特殊成员函数标记
    bool HasTrivialDefaultConstructor : 1;
    bool HasTrivialCopyConstructor : 1;
    bool HasTrivialMoveConstructor : 1;
    bool HasTrivialDestructor : 1;
    bool IsAggregate : 1;
    bool IsPOD : 1;
    bool IsLiteral : 1;
    
    // C++20/26 特性
    bool IsStructuralType : 1;  // C++20 structural type
  public:
    void addBase(CXXBaseSpecifier *Base);
    bool isDerivedFrom(CXXRecordDecl *Base);
    std::vector<CXXBaseSpecifier*> getBases() const { return Bases; }
  };
  ```

- **E1.3.4** 实现基类说明符：
  ```cpp
  class CXXBaseSpecifier {
    SourceRange Range;
    bool IsVirtual : 1;
    AccessSpecifier Access;  // public, protected, private
    QualType BaseType;
    SourceLocation EllipsisLoc;  // 包展开（可变参数模板）
  };
  ```

- **E1.3.5** 实现成员函数声明：
  ```cpp
  class CXXMethodDecl : public FunctionDecl {
    CXXRecordDecl *ParentClass;
    bool IsVirtual : 1;
    bool IsOverride : 1;
    bool IsFinal : 1;
    bool IsConst : 1;
    bool IsVolatile : 1;
    bool IsRefQualified : 1;  // & 或 && 限定
  };
  
  class CXXConstructorDecl : public CXXMethodDecl {
    std::vector<CXXCtorInitializer*> Initializers;  // 成员初始化列表
    bool IsExplicit : 1;
  };
  
  class CXXDestructorDecl : public CXXMethodDecl {};
  
  class CXXConversionDecl : public CXXMethodDecl {
    bool IsExplicit : 1;
  };
  ```

- **E1.3.6** 实现成员初始化器：
  ```cpp
  class CXXCtorInitializer {
    bool IsBaseInitializer : 1;
    bool IsMemberInitializer : 1;
    bool IsDelegatingInitializer : 1;
    FieldDecl *Member;  // 成员初始化
    TypeSourceInfo *BaseType;  // 基类初始化
    Expr *Init;
    SourceLocation LParenLoc, RParenLoc;
  };
  ```

**开发关键点提示：**
> 请实现类声明 AST 节点，追加到 `AST/Decl.h` 和 `AST/DeclCXX.h`。
>
> **RecordDecl**：
> - 继承 TypeDecl 和 DeclContext
> - 支持 struct/class/union 三种 tag
> - 管理字段列表
> - 跟踪是否完成定义
>
> **FieldDecl**：
> - 字段声明，包含类型
> - 支持位域（BitWidth）
> - 支持类内初始化器（C++11）
> - 支持 mutable 关键字
>
> **CXXRecordDecl**：
> - 继承 RecordDecl
> - 管理基类列表
> - 跟踪特殊成员函数（构造/析构/赋值）
> - 跟踪类型特性（POD、aggregate、literal）
>
> **CXXMethodDecl**：
> - 继承 FunctionDecl
> - 支持 virtual/override/final
> - 支持 const/volatile/引用限定符
>
> **CXXConstructorDecl**：
> - 继承 CXXMethodDecl
> - 管理成员初始化列表
> - 支持 explicit
>
> **CXXBaseSpecifier**：
> - 基类说明符
> - 支持 virtual 继承
> - 支持访问控制
>
> 所有类需要完整的 dump() 方法。

**Checkpoint：** CXXRecordDecl 编译通过；能表示带基类的类定义

---

### Task 3.1.4 模板声明节点

**目标：** 定义模板相关的 AST 节点

**开发要点：**

- **E1.4.1** 实现模板参数声明：
  ```cpp
  class TemplateTypeParmDecl : public TypeDecl {
    bool IsParameterPack : 1;  // typename... T
    bool HasTypeConstraint : 1;
    Expr *DefaultArgument;  // 默认模板参数
    unsigned Index;  // 参数索引
    unsigned Depth;  // 模板深度
  };
  
  class NonTypeTemplateParmDecl : public Decl {
    QualType Type;
    Expr *DefaultArgument;
    bool IsParameterPack : 1;
    unsigned Index, Depth;
  };
  
  class TemplateTemplateParmDecl : public TemplateDecl {
    bool IsParameterPack : 1;
    Expr *DefaultArgument;
    unsigned Index, Depth;
  };
  ```

- **E1.4.2** 实现模板声明基类：
  ```cpp
  class TemplateDecl : public Decl {
  protected:
    Decl *TemplatedDecl;  // 被模板化的声明
    TemplateParameterList *Parameters;  // 模板参数列表
  public:
    TemplateParameterList* getTemplateParameters() const { return Parameters; }
    Decl* getTemplatedDecl() const { return TemplatedDecl; }
  };
  
  class TemplateParameterList {
    SourceLocation TemplateLoc;
    SourceLocation LAngleLoc, RAngleLoc;
    std::vector<NamedDecl*> Params;
  public:
    unsigned size() const { return Params.size(); }
    NamedDecl* getParam(unsigned Idx) { return Params[Idx]; }
  };
  ```

- **E1.4.3** 实现具体模板声明：
  ```cpp
  class FunctionTemplateDecl : public TemplateDecl {
    // 模板函数
  };
  
  class ClassTemplateDecl : public TemplateDecl {
    std::vector<ClassTemplateSpecializationDecl*> Specializations;
  };
  
  class VarTemplateDecl : public TemplateDecl {
    std::vector<VarTemplateSpecializationDecl*> Specializations;
  };
  
  class TypeAliasTemplateDecl : public TemplateDecl {
    // template<typename T> using Name = Type;
  };
  ```

- **E1.4.4** 实现模板特化声明：
  ```cpp
  class ClassTemplateSpecializationDecl : public CXXRecordDecl {
    ClassTemplateDecl *SpecializedTemplate;
    std::vector<TemplateArgument> TemplateArgs;
    bool IsExplicitSpecialization : 1;
  };
  
  class ClassTemplatePartialSpecializationDecl : public ClassTemplateSpecializationDecl {
    TemplateParameterList *TemplateParams;  // 偏特化参数
  };
  ```

- **E1.4.5** 实现模板参数：
  ```cpp
  class TemplateArgument {
  public:
    enum ArgKind {
      Null, Type, Declaration, NullPtr, 
      Integral, Template, TemplateExpansion, Expression, Pack
    };
  private:
    ArgKind Kind;
    union {
      QualType TypeArg;
      ValueDecl *DeclArg;
      llvm::APSInt IntegralArg;
      // ...
    };
  public:
    ArgKind getKind() const { return Kind; }
    QualType getAsType() const;
    llvm::APSInt getAsIntegral() const;
    // ...
  };
  
  class TemplateArgumentLoc {
    TemplateArgument Argument;
    SourceLocation Location;
  };
  ```

**开发关键点提示：**
> 请实现模板声明 AST 节点 `include/zetacc/AST/DeclTemplate.h`。
>
> **TemplateParameterList**：
> - 管理模板参数列表
> - 包含 <> 位置信息
> - 支持遍历参数
>
> **TemplateDecl**：
> - 模板声明基类
> - 包含 TemplatedDecl（被模板化的声明）
> - 包含 TemplateParameterList
>
> **模板参数声明**：
> - TemplateTypeParmDecl：typename T
> - NonTypeTemplateParmDecl：int N
> - TemplateTemplateParmDecl：template<typename> class T
> - 支持 parameter pack（...）
> - 支持默认参数
>
> **具体模板声明**：
> - FunctionTemplateDecl
> - ClassTemplateDecl
> - VarTemplateDecl
> - TypeAliasTemplateDecl
>
> **模板特化**：
> - ClassTemplateSpecializationDecl
> - ClassTemplatePartialSpecializationDecl
>
> **TemplateArgument**：
> - 表示模板实参
> - 支持类型、整数、模板、表达式、包等多种类型

**Checkpoint：** 模板声明节点编译通过；能表示函数模板、类模板、模板特化

---

### Task 3.1.5 Concept 声明（C++20）

**目标：** 定义 Concept 的 AST 节点

**开发要点：**

- **E1.5.1** 实现 Concept 声明：
  ```cpp
  class ConceptDecl : public TemplateDecl {
    Expr *ConstraintExpression;  // 约束表达式
  public:
    Expr* getConstraintExpr() const { return ConstraintExpression; }
  };
  ```

- **E1.5.2** 实现类型约束：
  ```cpp
  class TypeConstraint {
    ConceptDecl *NamedConcept;
    std::vector<TemplateArgument> TemplateArgs;
    bool IsExpanded : 1;
  };
  ```

- **E1.5.3** 实现 requires 子句：
  ```cpp
  class RequiresClause {
    Expr *ConstraintExpr;
  };
  ```

**开发关键点提示：**
> 请实现 C++20 Concept 相关 AST 节点，追加到 `AST/DeclTemplate.h` 或新建 `AST/Concept.h`。
>
> **ConceptDecl**：
> - 继承 TemplateDecl
> - 包含约束表达式
> - 示例：template<typename T> concept Sortable = requires(T t) { t.sort(); };
>
> **TypeConstraint**：
> - 模板类型参数的约束
> - 示例：template<Sortable T> void f(T);
>
> 所有类需要完整的 dump() 方法。

**Checkpoint：** Concept 声明编译通过；能表示 concept 定义和类型约束

---

## Stage 3.2 — 基础声明解析

### Task 3.2.1 声明说明符解析

**目标：** 实现声明说明符（decl-specifier-seq）的解析

**开发要点：**

- **E2.1.1** 创建 `Parse/ParseDecl.h`，定义声明解析器：
  ```cpp
  class Parser {
    // 声明解析
    Decl* parseDeclaration(DeclContext *Owner);
    Decl* parseBlockDeclaration(DeclContext *Owner);
    Decl* parseTopLevelDeclaration(DeclContext *Owner);
  };
  ```

- **E2.1.2** 实现声明说明符解析：
  ```cpp
  struct DeclSpec {
    // 存储类型说明符
    TypeSpecifier TypeSpec;
    StorageClass SC;
    FunctionSpecifiers FS;
    bool IsFriend : 1;
    bool IsConstexpr : 1;
    bool IsInline : 1;
    bool IsVirtual : 1;
    bool IsExplicit : 1;
  };
  
  void parseDeclSpecifiers(DeclSpec &DS);
  ```

- **E2.1.3** 实现类型说明符解析：
  ```cpp
  void parseTypeSpecifier(DeclSpec &DS);
  void parseBuiltinTypeSpecifier(DeclSpec &DS);  // int, char, void, ...
  void parseElaboratedTypeSpecifier(DeclSpec &DS);  // class X, struct X, enum X, typename X
  void parseTypenameSpecifier(DeclSpec &DS);  // typename T::type
  ```

- **E2.1.4** 实现存储类说明符解析：
  ```cpp
  void parseStorageClassSpecifier(DeclSpec &DS);  // auto, register, static, extern, mutable, thread_local
  ```

- **E2.1.5** 实现函数说明符解析：
  ```cpp
  void parseFunctionSpecifier(DeclSpec &DS);  // inline, virtual, explicit, friend, constexpr, consteval, constinit
  ```

**开发关键点提示：**
> 请实现声明说明符解析 `src/Parse/ParseDeclSpec.cpp`。
>
> **DeclSpec 结构**：
> - 存储解析过程中的声明说明符
> - 类型说明符（int, char, class X, typename T::type 等）
> - 存储类
> - 函数说明符（inline, virtual, explicit, friend, constexpr）
>
> **解析函数**：
> - `parseDeclSpecifiers(DeclSpec &DS)`：解析完整的 decl-specifier-seq
> - `parseTypeSpecifier(DeclSpec &DS)`：解析类型说明符
> - `parseStorageClassSpecifier(DeclSpec &DS)`：解析存储类
> - `parseFunctionSpecifier(DeclSpec &DS)`：解析函数说明符
>
> **类型说明符分类**：
> - 简单类型：int, char, void, bool, float, double, wchar_t, char8_t, char16_t, char32_t
> - 复杂类型：class X, struct X, enum X, typename T::type
> - CV 限定符：const, volatile
> - 签名类型：signed, unsigned, short, long, long long
>
> 处理说明符顺序和组合的合法性检查。

**Checkpoint：** 声明说明符正确解析；能识别 int, const int, static int, inline void 等组合

---

### Task 3.2.2 声明符解析

**目标：** 实现声明符（declarator）的解析

**开发要点：**

- **E2.2.1** 定义声明符数据结构：
  ```cpp
  struct Declarator {
    DeclSpec DS;  // 声明说明符
    std::vector<DeclaratorChunk> Chunks;  // 声明符片段
    DeclarationName Name;  // 声明名称
    SourceLocation IdentifierLoc;
    bool IsFunctionDeclarator : 1;
    bool IsArrayDeclarator : 1;
  };
  
  struct DeclaratorChunk {
    enum Kind { Pointer, Reference, Array, Function, MemberPointer };
    Kind ChunkKind;
    union {
      PointerInfo Pointer;
      ReferenceInfo Reference;
      ArrayInfo Array;
      FunctionInfo Function;
      MemberPointerInfo MemberPointer;
    };
  };
  
  struct FunctionInfo {
    std::vector<ParamInfo> Params;
    bool IsVariadic : 1;
    QualType ReturnType;
    ExceptionSpec ES;
    bool IsConst : 1;
    bool IsVolatile : 1;
    bool IsRefQualified : 1;
    bool IsLValueRef : 1;  // & vs &&
  };
  ```

- **E2.2.2** 实现声明符解析：
  ```cpp
  void parseDeclarator(Declarator &D);
  void parseDirectDeclarator(Declarator &D);
  void parseDeclaratorInternal(Declarator &D);
  ```

- **E2.2.3** 实现指针和引用声明符解析：
  ```cpp
  void parsePointerDeclarator(Declarator &D);  // *ptr, *const ptr, *volatile ptr
  void parseReferenceDeclarator(Declarator &D);  // &ref, &&rref
  void parseMemberPointerDeclarator(Declarator &D);  // Class::*ptr
  ```

- **E2.2.4** 实现数组和函数声明符解析：
  ```cpp
  void parseArrayDeclarator(Declarator &D);  // arr[10], arr[]
  void parseFunctionDeclarator(Declarator &D);  // func(int, char), func()
  ```

- **E2.2.5** 实现参数列表解析：
  ```cpp
  void parseParameterList(FunctionInfo &FI);
  ParamInfo parseParameter();
  ```

**开发关键点提示：**
> 请实现声明符解析 `src/Parse/ParseDeclarator.cpp`。
>
> **Declarator 结构**：
> - 包含 DeclSpec（声明说明符）
> - 包含 DeclaratorChunk 列表（声明符片段栈）
> - 包含声明名称和位置
>
> **DeclaratorChunk 类型**：
> - Pointer：*、*const、*volatile
> - Reference：&、&&
> - MemberPointer：Class::*
> - Array：[N]、[]
> - Function：(params)、()、(params) const/ volatile/&
>
> **解析函数**：
> - `parseDeclarator()`：入口，调用 parseDirectDeclarator 和 parsePointerDeclarator
> - `parseDirectDeclarator()`：直接声明符（标识符、数组、函数）
> - `parsePointerDeclarator()`：指针声明符
> - `parseFunctionDeclarator()`：函数声明符
> - `parseParameterList()`：参数列表
>
> **处理复杂声明符**：
> - int *p; — 指针
> - int &r; — 引用
> - int (*pf)(int); — 函数指针
> - int (*arr)[10]; — 数组指针
> - int *ap[10]; — 指针数组
> - int (Class::*pmf)(int); — 成员函数指针
> - void f(int x, int y = 10); — 默认参数
> - void f(int...); — 可变参数
>
> 声明符片段按从内到外的顺序入栈，构建时需要反向处理。

**Checkpoint：** 复杂声明符正确解析；能解析函数指针、成员指针、数组指针等

---

### Task 3.2.3 变量和函数声明解析

**目标：** 实现变量和函数声明的完整解析

**开发要点：**

- **E2.3.1** 实现变量声明解析：
  ```cpp
  VarDecl* parseVarDeclaration(DeclSpec &DS, Declarator &D, DeclContext *Owner);
  ```

- **E2.3.2** 处理变量初始化：
  ```cpp
  Expr* parseInitializer();  // = expr, { list }, ( exprs )
  void parseBraceInitList();  // C++11 统一初始化
  void parseParenInitList();
  ```

- **E2.3.3** 实现函数声明解析：
  ```cpp
  FunctionDecl* parseFunctionDeclaration(DeclSpec &DS, Declarator &D, DeclContext *Owner);
  ```

- **E2.3.4** 解析函数体：
  ```cpp
  Stmt* parseFunctionBody(FunctionDecl *FD);  // { ... }, = delete, = default
  ```

- **E2.3.5** 处理默认参数：
  ```cpp
  Expr* parseDefaultArgument();
  ```

**开发关键点提示：**
> 请实现变量和函数声明解析 `src/Parse/ParseDecl.cpp`。
>
> **变量声明**：
> - 解析类型、名称、初始化器
> - 支持三种初始化语法：= expr, { list }, ( exprs )
> - 处理 auto 类型推导（C++11）
> - 处理 constexpr 变量
>
> **函数声明**：
> - 解析返回类型、参数列表
> - 处理函数体：复合语句、= delete、= default
> - 处理 noexcept 说明符
> - 处理尾置返回类型（C++11）：auto f() -> int
>
> **初始化器解析**：
> - `parseInitializer()`：统一入口
> - `parseBraceInitList()`：C++11 列表初始化 { 1, 2, 3 }
> - `parseParenInitList()`：构造函数风格 (1, 2, 3)
>
> 示例：
> ```cpp
> int x = 10;
> int y{20};
> int z(30);
> auto a = 1;
> constexpr int N = 10;
> void f(int x = 0);
> ```

**Checkpoint：** 变量和函数声明正确解析；初始化器语法正确处理

---

### Task 3.2.4 命名空间和 using 声明解析

**目标：** 实现命名空间和 using 相关声明的解析

**开发要点：**

- **E2.4.1** 实现命名空间声明解析：
  ```cpp
  NamespaceDecl* parseNamespaceDeclaration(DeclContext *Owner);
  void parseNamespaceBody(NamespaceDecl *NS);
  ```

- **E2.4.2** 实现命名空间别名解析：
  ```cpp
  NamespaceAliasDecl* parseNamespaceAlias();
  ```

- **E2.4.3** 实现 using 声明解析：
  ```cpp
  UsingDecl* parseUsingDeclaration();
  UsingDirectiveDecl* parseUsingDirective();
  ```

- **E2.4.4** 实现类型别名解析：
  ```cpp
  TypeAliasDecl* parseTypeAliasDeclaration();  // using Name = Type;
  ```

**开发关键点提示：**
> 请实现命名空间和 using 声明解析，追加到 `src/Parse/ParseDecl.cpp`。
>
> **命名空间**：
> - `namespace Name { ... }`
> - `namespace { ... }` — 匿名命名空间
> - `inline namespace Name { ... }` — 内联命名空间
> - 嵌套命名空间定义（C++17）：`namespace A::B::C { ... }`
>
> **命名空间别名**：
> - `namespace Alias = Original::Namespace;`
>
> **using 声明**：
> - `using Name;` — 引入名称
> - `using typename T::type;` — 引入类型
>
> **using 指令**：
> - `using namespace Name;`
>
> **类型别名（C++11）**：
> - `using Name = Type;`
> - 区别于 typedef

**Checkpoint：** 命名空间和 using 声明正确解析

---

### Task 3.2.5 枚举声明解析

**目标：** 实现枚举声明的解析

**开发要点：**

- **E2.5.1** 实现枚举声明解析：
  ```cpp
  EnumDecl* parseEnumDeclaration(DeclSpec &DS, DeclContext *Owner);
  void parseEnumBody(EnumDecl *ED);
  EnumConstantDecl* parseEnumerator(EnumDecl *ED);
  ```

- **E2.5.2** 处理枚举类型和底层类型：
  ```cpp
  QualType parseEnumBase();  // enum Name : int { ... }
  ```

- **E2.5.3** 处理 scoped enum（C++11）：
  ```cpp
  // enum class Name { ... }
  // enum struct Name { ... }
  ```

**开发关键点提示：**
> 请实现枚举声明解析，追加到 `src/Parse/ParseDecl.cpp`。
>
> **枚举语法**：
> ```
> enum-specifier:
>   'enum' identifier? enum-base? '{' enumerator-list '}'
>   'enum' 'class'? 'struct'? identifier enum-base? '{' enumerator-list '}'
> 
> enum-base:
>   ':' type-specifier-seq
> 
> enumerator-list:
>   enumerator (',' enumerator)* ','?
> 
> enumerator:
>   identifier ('=' constant-expression)?
> ```
>
> **处理要点**：
> - 区分 unscoped enum 和 scoped enum（enum class/struct）
> - 处理底层类型说明（: int）
> - 处理枚举器的值和初始化
> - 处理枚举器列表的尾随逗号

**Checkpoint：** 枚举声明正确解析；scoped enum 和底层类型正确处理

---

## Stage 3.3 — 类与模板解析

### Task 3.3.1 类定义解析框架

**目标：** 建立类定义解析的框架

**开发要点：**

- **E3.1.1** 实现类声明入口：
  ```cpp
  RecordDecl* parseClassDeclaration(DeclSpec &DS, DeclContext *Owner);
  CXXRecordDecl* parseCXXClassDeclaration(DeclSpec &DS, DeclContext *Owner);
  ```

- **E3.1.2** 实现类体解析：
  ```cpp
  void parseClassBody(CXXRecordDecl *Class);
  void parseMemberSpecification(CXXRecordDecl *Class);
  ```

- **E3.1.3** 实现访问说明符解析：
  ```cpp
  void parseAccessSpecifier(CXXRecordDecl *Class);  // public:, private:, protected:
  ```

- **E3.1.4** 实现类成员解析：
  ```cpp
  Decl* parseMemberDeclaration(CXXRecordDecl *Class);
  ```

**开发关键点提示：**
> 请实现类定义解析框架 `src/Parse/ParseClass.cpp`。
>
> **类声明入口**：
> - `parseClassDeclaration()`：解析 class/struct/union
> - 区分简单声明（class X;）和定义（class X { ... };）
>
> **类体解析**：
> - `{` → 解析成员列表 → `}`
> - 处理访问说明符（public:, private:, protected:）
> - 处理成员声明（数据成员、成员函数、嵌套类型、友元等）
>
> **成员解析**：
> - 数据成员：`int x;`, `static int y;`, `mutable int z;`
> - 成员函数：`void f();`, `virtual void g();`, `static void h();`
> - 嵌套类型：`class Nested;`, `enum Enum {};`
> - 友元：`friend class X;`, `friend void f();`
> - 访问控制：根据当前访问级别设置成员可见性
>
> **处理要点**：
> - 跟踪当前访问级别（默认 private for class, public for struct）
> - 处理成员初始化器（C++11）：`int x = 10;`
> - 处理位域：`int x : 4;`

**Checkpoint：** 类定义框架建立；能解析简单的类定义

---

### Task 3.3.2 基类和虚基类解析

**目标：** 实现基类说明符的解析

**开发要点：**

- **E3.2.1** 实现基类列表解析：
  ```cpp
  void parseBaseClause(CXXRecordDecl *Class);  // : public Base, private Base2, virtual Base3
  CXXBaseSpecifier* parseBaseSpecifier();
  ```

- **E3.2.2** 处理访问控制和虚拟继承：
  ```cpp
  AccessSpecifier parseAccessSpecifier();  // public, protected, private
  bool parseVirtualSpecifier();  // virtual
  ```

- **E3.2.3** 处理包展开（可变参数模板）：
  ```cpp
  void parsePackExpansion();  // Base<Ts>...
  ```

**开发关键点提示：**
> 请实现基类解析，追加到 `src/Parse/ParseClass.cpp`。
>
> **基类子句语法**：
> ```
> base-clause:
>   ':' base-specifier-list
> 
> base-specifier-list:
>   base-specifier (',' base-specifier)*
> 
> base-specifier:
>   attribute-specifier-seq? base-type-specifier
>   'virtual' access-specifier? base-type-specifier
>   access-specifier 'virtual'? base-type-specifier
> ```
>
> **处理要点**：
> - 访问控制：public, protected, private（默认 private for class）
> - 虚拟继承：virtual
> - 基类类型：可以是模板实例、依赖类型
> - 包展开：`template<typename... Ts> class Derived : public Base<Ts>... {};`
>
> 示例：
> ```cpp
> class Derived : public Base1, private Base2, virtual Base3 {};
> template<typename... Ts> class Multi : public Ts... {};
> ```

**Checkpoint：** 基类和虚基类正确解析；包展开基类正确处理

---

### Task 3.3.3 成员函数和构造函数解析

**目标：** 实现成员函数和构造函数的完整解析

**开发要点：**

- **E3.3.1** 实现成员函数解析：
  ```cpp
  CXXMethodDecl* parseCXXMethodDeclaration(DeclSpec &DS, Declarator &D, CXXRecordDecl *Class);
  ```

- **E3.3.2** 实现构造函数解析：
  ```cpp
  CXXConstructorDecl* parseCXXConstructorDeclaration(Declarator &D, CXXRecordDecl *Class);
  ```

- **E3.3.3** 解析成员初始化列表：
  ```cpp
  std::vector<CXXCtorInitializer*> parseCtorInitializers(CXXConstructorDecl *Ctor);
  CXXCtorInitializer* parseMemInitializer(CXXConstructorDecl *Ctor);
  ```

- **E3.3.4** 实现析构函数解析：
  ```cpp
  CXXDestructorDecl* parseCXXDestructorDeclaration(Declarator &D, CXXRecordDecl *Class);
  ```

- **E3.3.5** 实现转换函数解析：
  ```cpp
  CXXConversionDecl* parseCXXConversionDeclaration(DeclSpec &DS, CXXRecordDecl *Class);
  ```

**开发关键点提示：**
> 请实现成员函数解析，追加到 `src/Parse/ParseClass.cpp`。
>
> **成员函数**：
> - 普通：`void f();`, `void f() const;`, `void f() &;`, `void f() &&;`
> - 虚函数：`virtual void f();`, `virtual void f() = 0;`
> - 覆盖：`void f() override;`, `void f() final;`
> - 静态：`static void f();`
> - 内联：`inline void f();`
>
> **构造函数**：
> - 名称与类名相同
> - 无返回类型
> - 支持初始化列表：`: member(value), base(args) { ... }`
> - 支持 = default, = delete
> - 支持 explicit
> - 支持 constexpr
>
> **析构函数**：
> - 名称：`~ClassName()`
> - 无参数、无返回类型
> - 支持 virtual, = default, = delete
>
> **转换函数**：
> - `operator int();`, `operator const char*();`
> - 支持 explicit
>
> **成员初始化列表**：
> ```
> ctor-initializer:
>   ':' mem-initializer-list
> 
> mem-initializer-list:
>   mem-initializer (',' mem-initializer)*
> 
> mem-initializer:
>   mem-initializer-id '(' expression-list? ')'
>   mem-initializer-id braced-init-list
> 
> mem-initializer-id:
>   class-or-decltype
>   identifier
> ```

**Checkpoint：** 成员函数、构造函数、析构函数、转换函数正确解析

---

### Task 3.3.4 模板声明解析

**目标：** 实现模板声明的解析

**开发要点：**

- **E3.4.1** 实现模板声明入口：
  ```cpp
  TemplateDecl* parseTemplateDeclaration(DeclContext *Owner);
  void parseTemplateParameters(TemplateParameterList *&Params);
  ```

- **E3.4.2** 实现模板参数解析：
  ```cpp
  NamedDecl* parseTemplateParameter();
  TemplateTypeParmDecl* parseTemplateTypeParameter();
  NonTypeTemplateParmDecl* parseNonTypeTemplateParameter();
  TemplateTemplateParmDecl* parseTemplateTemplateParameter();
  ```

- **E3.4.3** 实现默认模板参数解析：
  ```cpp
  Expr* parseTemplateArgumentDefault();
  ```

- **E3.4.4** 实现模板参数包解析：
  ```cpp
  bool parseTemplateParameterPack();  // ...
  ```

**开发关键点提示：**
> 请实现模板声明解析 `src/Parse/ParseTemplate.cpp`。
>
> **模板声明语法**：
> ```
> template-declaration:
>   'template' '<' template-parameter-list '>' declaration
> 
> template-parameter-list:
>   template-parameter (',' template-parameter)*
> 
> template-parameter:
>   type-parameter
>   parameter-declaration
> 
> type-parameter:
>   'typename' identifier? '=' type-id
>   'typename' '...' identifier?
>   'template' '<' template-parameter-list '>' 'class' identifier? '=' id-expression
>   'template' '<' template-parameter-list '>' 'class' '...' identifier?
> ```
>
> **模板参数类型**：
> - 类型参数：`typename T`, `class T`, `typename... Ts`
> - 非类型参数：`int N`, `size_t Size`, `auto X`
> - 模板参数：`template<typename> class T`
>
> **默认参数**：
> - `template<typename T = int>`
> - `template<int N = 10>`
>
> **Parameter Pack**：
> - `template<typename... Ts>`
> - `template<int... Ns>`
>
> **解析后调用声明解析器**：
> - 函数模板：调用函数声明解析
> - 类模板：调用类声明解析
> - 变量模板：调用变量声明解析
> - 别名模板：调用类型别名解析

**Checkpoint：** 模板声明正确解析；模板参数、默认参数、参数包正确处理

---

### Task 3.3.5 模板特化解析

**目标：** 实现模板特化和偏特化的解析

**开发要点：**

- **E3.5.1** 实现模板实参列表解析：
  ```cpp
  std::vector<TemplateArgument> parseTemplateArgumentList();
  TemplateArgument parseTemplateArgument();
  ```

- **E3.5.2** 实现显式特化解析：
  ```cpp
  Decl* parseExplicitSpecialization(DeclContext *Owner);  // template<> class X<int> { ... };
  ```

- **E3.5.3** 实现偏特化解析：
  ```cpp
  ClassTemplatePartialSpecializationDecl* parsePartialSpecialization(ClassTemplateDecl *PrimaryTemplate);
  ```

- **E3.5.4** 处理模板实参推导指引（C++17）：
  ```cpp
  CXXDeductionGuideDecl* parseDeductionGuide();  // X(int) -> X<int>;
  ```

**开发关键点提示：**
> 请实现模板特化解析，追加到 `src/Parse/ParseTemplate.cpp`。
>
> **显式特化**：
> ```
> explicit-specialization:
>   'template' '<' '>' declaration
> ```
> 示例：
> ```cpp
> template<> class Vector<int> { /* int特化 */ };
> template<> void f<int>(int x);
> ```
>
> **偏特化**：
> ```
> partial-specialization:
>   'template' '<' template-parameter-list '>' declaration
> ```
> 示例：
> ```cpp
> template<typename T> class Vector<T*> { /* 指针偏特化 */ };
> template<typename T, size_t N> class Array<T, N> { /* ... */ };
> template<typename T, size_t 0> class Array<T, 0> { /* 空数组偏特化 */ };
> ```
>
> **模板实参解析**：
> ```
> template-argument-list:
>   template-argument (',' template-argument)*
> 
> template-argument:
>   type-id
>   constant-expression
>   id-expression
> ```
>
> **推导指引（C++17）**：
> ```
> deduction-guide:
>   template-name '(' parameter-declaration-clause ')' '->' type-id
> ```
> 示例：
> ```cpp
> template<typename T> Array(size_t, T) -> Array<T>;
> ```

**Checkpoint：** 模板特化和偏特化正确解析；推导指引正确处理

---

### Task 3.3.6 Concept 解析（C++20）

**目标：** 实现 Concept 定义的解析

**开发要点：**

- **E3.6.1** 实现 Concept 声明解析：
  ```cpp
  ConceptDecl* parseConceptDeclaration(DeclContext *Owner);
  ```

- **E3.6.2** 解析约束表达式：
  ```cpp
  Expr* parseConstraintExpression();
  ```

- **E3.6.3** 实现类型约束解析：
  ```cpp
  TypeConstraint* parseTypeConstraint();
  ```

- **E3.6.4** 实现 requires 子句解析：
  ```cpp
  Expr* parseRequiresClause();
  ```

**开发关键点提示：**
> 请实现 C++20 Concept 解析，追加到 `src/Parse/ParseTemplate.cpp` 或新建 `ParseConcept.cpp`。
>
> **Concept 定义**：
> ```
> concept-definition:
>   'template' '<' template-parameter-list '>' 'concept' identifier '=' constraint-expression ';'
> ```
> 示例：
> ```cpp
> template<typename T>
> concept Sortable = requires(T t) { t.sort(); };
> 
> template<typename T>
> concept Integral = std::is_integral_v<T>;
> ```
>
> **类型约束**：
> - 模板类型参数的约束
> - `template<Sortable T> void f(T);`
> - `template<typename T> requires Sortable<T> void f(T);`
>
> **requires 子句**：
> ```
> requires-clause:
>   'requires' constraint-logical-or-expression
> ```
>
> **约束表达式**：
> - 简单约束：`Integral<T>`
> - 合取：`Integral<T> && Sortable<T>`
> - 析取：`Integral<T> || Floating<T>`
> - 原子约束：`requires (T t) { ... }`

**Checkpoint：** Concept 定义和类型约束正确解析

---

## Stage 3.4 — C++26 特性 + 集成测试

### Task 3.4.1 C++26 声明新特性

**目标：** 实现 C++26 新增的声明语法

**开发要点：**

- **E4.1.1** 实现静态反射类型声明：
  ```cpp
  // reflexpr 返回的元信息类型
  // <meta::info> 类型（编译器内置）
  ```

- **E4.1.2** 实现占位符变量（Placeholder Variables）：
  ```cpp
  auto _ = getValue();  // _ 作为未使用变量的占位符
  ```

- **E4.1.3** 实现弃置函数增强（Deducing this 参数）：
  ```cpp
  void f(this auto& self);  // 显式对象参数（C++23/26）
  ```

- **E4.1.4** 处理 Contracts 属性：
  ```cpp
  void f(int x) [[pre: x > 0]] [[post: result > 0]];
  ```

**开发关键点提示：**
> 请实现 C++26 声明新特性的解析。
>
> **Deducing this（显式对象参数）**：
> ```cpp
> struct S {
>   void f(this S& self);  // C++23
>   void g(this auto& self);  // 泛型 Lambda 风格
> };
> ```
>
> **占位符变量**：
> - `_` 作为特殊标识符，表示未使用变量
> - 可多次定义：`auto _ = f(); auto _ = g();`
>
> **Contracts 属性（P2900）**：
> ```cpp
> void f(int x) 
>   [[pre: x > 0]]
>   [[post: result > 0]]
>   [[assert: y != 0]];
> ```
> - pre：前置条件
> - post：后置条件
> - assert：断言

**Checkpoint：** C++26 新声明语法正确解析

---

### Task 3.4.2 完整翻译单元解析

**目标：** 实现完整翻译单元的解析

**开发要点：**

- **E4.2.1** 实现翻译单元解析入口：
  ```cpp
  TranslationUnitDecl* Parser::parseTranslationUnit();
  ```

- **E4.2.2** 循环解析顶层声明：
  ```cpp
  while (!Tok.is(tok::eof)) {
    Decl *D = parseTopLevelDeclaration(Context.getTranslationUnitDecl());
    if (D) Context.getTranslationUnitDecl()->addDecl(D);
  }
  ```

- **E4.2.3** 处理链接说明：
  ```cpp
  LinkageSpecDecl* parseLinkageSpecification();  // extern "C" { ... }
  ```

- **E4.2.4** 处理属性：
  ```cpp
  void parseAttributes();  // [[attr]], __attribute__((attr)), __declspec(attr)
  ```

**开发关键点提示：**
> 请实现完整翻译单元解析，追加到 `src/Parse/Parser.cpp`。
>
> **翻译单元结构**：
> ```
> translation-unit:
>   top-level-declaration-seq? eof
> 
> top-level-declaration-seq:
>   top-level-declaration
>   top-level-declaration-seq top-level-declaration
> 
> top-level-declaration:
>   declaration
>   module-import-declaration  // C++20 模块
>   module-declaration         // C++20 模块
> ```
>
> **链接说明**：
> ```cpp
> extern "C" { /* C 代码 */ }
> extern "C++" { /* C++ 代码 */ }
> ```
>
> **属性**：
> - C++11 属性：`[[noreturn]], [[nodiscard]], [[deprecated]]`
> - GNU 属性：`__attribute__((deprecated))`
> - MSVC 属性：`__declspec(dllexport)`

**Checkpoint：** 完整翻译单元正确解析

---

### Task 3.4.3 Parser 错误恢复增强

**目标：** 增强错误恢复机制，处理更多错误场景

**开发要点：**

- **E4.3.1** 实现声明级别的错误恢复：
  ```cpp
  void skipUntilNextDeclaration();  // 跳到下一个声明
  ```

- **E4.3.2** 实现括号/花括号匹配恢复：
  ```cpp
  void skipUntilBalanced(TokenKind Open, TokenKind Close);
  ```

- **E4.3.3** 处理缺失分号：
  ```cpp
  void tryRecoverMissingSemicolon();
  ```

- **E4.3.4** 生成有意义的错误消息

**开发关键点提示：**
> 请增强 Parser 错误恢复机制。
>
> **错误恢复策略**：
> 1. 声明级别：遇到严重错误时跳到下一个顶层声明
> 2. 语句级别：跳到下一个语句或闭合花括号
> 3. 表达式级别：跳到表达式结束点（分号、逗号、右括号）
>
> **错误消息**：
> - 具体描述错误位置和原因
> - 提示可能的修复建议
> - 避免级联错误（抑制后续相关错误）
>
> **常见错误场景**：
> - 缺失分号
> - 不匹配的括号/花括号
> - 无效的类型说明符
> - 重复的声明符
> - 错误的模板参数数量

**Checkpoint：** 错误恢复机制能正确处理常见错误场景

---

### Task 3.4.4 Parser 单元测试

**目标：** 建立 Parser 的完整测试覆盖

**开发要点：**

- **E4.4.1** 创建测试目录结构：
  ```
  tests/unit/Parse/
  ├── ParseDeclTest.cpp
  ├── ParseExprTest.cpp
  ├── ParseStmtTest.cpp
  ├── ParseClassTest.cpp
  └── ParseTemplateTest.cpp
  ```

- **E4.4.2** 编写声明解析测试：
  ```cpp
  TEST(ParseDeclTest, VarDecl) {
    EXPECT_TRUE(parseDecl("int x = 10;", result));
    EXPECT_TRUE(isa<VarDecl>(result));
  }
  
  TEST(ParseDeclTest, FunctionDecl) {
    EXPECT_TRUE(parseDecl("void f(int x, int y = 0);", result));
    EXPECT_TRUE(isa<FunctionDecl>(result));
  }
  ```

- **E4.4.3** 编写类解析测试：
  ```cpp
  TEST(ParseClassTest, SimpleClass) {
    EXPECT_TRUE(parseDecl("class X { int a; void f(); };", result));
    EXPECT_TRUE(isa<CXXRecordDecl>(result));
  }
  ```

- **E4.4.4** 编写模板解析测试
- **E4.4.5** 测试覆盖率目标：≥ 80%

**Checkpoint：** 所有测试通过；覆盖率 ≥ 80%

---

### Task 3.4.5 LLVM Lit 回归测试

**目标：** 建立 LLVM Lit 回归测试套件

**开发要点：**

- **E4.5.1** 创建测试目录：
  ```
  tests/lit/
  ├── lit.cfg
  ├── parse/
  │   ├── basic.test
  │   ├── class.test
  │   ├── template.test
  │   └── errors.test
  └── ast-dump/
      ├── expressions.test
      └── declarations.test
  ```

- **E4.5.2** 编写测试用例：
  ```lit
  // RUN: zetacc -ast-dump %s | FileCheck %s
  
  int x = 10;
  // CHECK: VarDecl {{.*}} x 'int' cinit
  // CHECK-NEXT: IntegerLiteral {{.*}} 'int' 10
  ```

- **E4.5.3** 配置 Lit 测试驱动

**Checkpoint：** Lit 测试套件运行通过

---

## 📋 Phase 3 验收检查清单

```
[ ] 所有声明 AST 节点定义完成
[ ] DeclContext 实现完成
[ ] TranslationUnitDecl 作为 AST 根节点
[ ] 声明说明符（decl-specifier-seq）正确解析
[ ] 声明符（declarator）正确解析
[ ] 复杂声明符（函数指针、成员指针等）正确解析
[ ] 变量声明和初始化正确解析
[ ] 函数声明和定义正确解析
[ ] 命名空间声明正确解析
[ ] using 声明和指令正确解析
[ ] 枚举声明正确解析
[ ] 类定义正确解析
[ ] 基类和虚基类正确解析
[ ] 成员函数、构造函数、析构函数正确解析
[ ] 成员初始化列表正确解析
[ ] 模板声明正确解析
[ ] 模板参数（类型/非类型/模板）正确解析
[ ] 模板特化和偏特化正确解析
[ ] Concept 定义正确解析（C++20）
[ ] 类型约束正确解析
[ ] 链接说明正确解析
[ ] 属性正确解析
[ ] C++26 新声明语法正确解析
[ ] 完整翻译单元正确解析
[ ] 错误恢复机制健壮
[ ] 单元测试覆盖率 ≥ 80%
[ ] Lit 回归测试通过
```

---

*Phase 3 完成标志：能够将预处理后的 Token 流完整解析为 AST，支持所有 C++26 声明语法，测试通过，覆盖率 ≥ 80%。*
