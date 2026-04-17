# Phase 3 开发任务完成情况核对报告

> **分析日期:** 2026-04-17
> **分析依据:** `/docs/plan/03-PHASE3-parser-declaration.md`
> **当前状态:** 项目已成功构建,但存在部分未完成功能

---

## 📊 总体完成度评估

| Stage | 名称 | 完成度 | 关键问题 |
|-------|------|--------|----------|
| **Stage 3.1** | 声明 AST 节点定义 | **95%** | ~~DeclContext 基础设施~~ ✅、~~模板特化声明类~~ ✅ |
| **Stage 3.2** | 基础声明解析 | **85%** | 核心功能完整,结构化设计不足 |
| **Stage 3.3** | 类与模板解析 | **85%** | ~~模板特化/偏特化解析~~ ✅、类型约束 ✅ |
| **Stage 3.4** | C++26 特性 + 测试 | **70%** | ~~Lit 回归测试~~ ✅、缺少 C++23/26 新特性 |
| **总计** | **Phase 3** | **~84%** | **关键路径功能已基本完成,高级特性待实现** |

---

## ✅ 已完成的核心功能

### Stage 3.1 - 声明 AST 节点 (75%)

#### ✅ 已实现的声明类型

**基础声明:**
- ✅ `Decl` 基类及完整继承体系 (`NamedDecl`, `ValueDecl`, `TypeDecl`, `TagDecl`)
- ✅ `TranslationUnitDecl` - 翻译单元根节点
- ✅ `VarDecl` - 变量声明 (含初始化、constexpr、inline)
- ✅ `FunctionDecl` - 函数声明 (含参数、函数体、noexcept)
- ✅ `ParmVarDecl` - 函数参数
- ✅ `FieldDecl` - 字段声明 (含位域、mutable、类内初始化)
- ✅ `EnumConstantDecl` - 枚举常量

**类型声明:**
- ✅ `TypedefDecl` - typedef 声明
- ✅ `TypeAliasDecl` - using 别名 (C++11)
- ✅ `EnumDecl` - 枚举声明 (支持 scoped enum、底层类型)
- ✅ `RecordDecl` - 记录声明 (struct/class/union)
- ✅ `CXXRecordDecl` - C++ 类声明 (含基类、特殊成员函数)

**C++ 特定声明:**
- ✅ `CXXMethodDecl` - 成员函数 (virtual/override/final/const/volatile/ref-qualifier)
- ✅ `CXXConstructorDecl` - 构造函数 (含成员初始化列表、explicit)
- ✅ `CXXDestructorDecl` - 析构函数
- ✅ `CXXConversionDecl` - 转换函数
- ✅ `CXXCtorInitializer` - 成员初始化器
- ✅ `AccessSpecDecl` - 访问说明符
- ✅ `FriendDecl` - 友元声明

**命名空间与 using:**
- ✅ `NamespaceDecl` - 命名空间 (支持 inline namespace)
- ✅ `NamespaceAliasDecl` - 命名空间别名
- ✅ `UsingDecl` - using 声明
- ✅ `UsingDirectiveDecl` - using namespace 指令
- ✅ `UsingEnumDecl` - using enum (C++20)

**模板相关:**
- ✅ `TemplateDecl` - 模板声明基类
- ✅ `TemplateTypeParmDecl` - 模板类型参数 (含参数包、默认参数)
- ✅ `NonTypeTemplateParmDecl` - 非类型模板参数
- ✅ `TemplateTemplateParmDecl` - 模板模板参数 (含约束)
- ✅ `ConceptDecl` - Concept 声明 (C++20)

**其他:**
- ✅ `StaticAssertDecl` - static_assert
- ✅ `LinkageSpecDecl` - extern "C"
- ✅ `ModuleDecl`, `ImportDecl`, `ExportDecl` - 模块 (C++20)
- ✅ `CXXDeductionGuideDecl` - 推导指引 (C++17)
- ✅ `AttributeDecl`, `AttributeListDecl` - 属性

**TemplateArgument 类:**
- ✅ 支持 9 种类型 (Null, Type, Declaration, NullPtr, Integral, Template, TemplateExpansion, Expression, Pack)
- ✅ `TemplateArgumentLoc` 类 (带 SourceLocation)
- ✅ 参数包展开支持 (`isPackExpansion`/`setPackExpansion`)
- ✅ 完整的访问方法和拷贝/移动/析构支持

---

### ❌ Stage 3.1 缺失功能 (25%)

#### 1. ~~DeclContext 基础设施缺失~~ ✅ 已实现

**已完成 (commit: 874ecf3):**
- ✅ 创建独立的 `DeclContext` 类 (`include/blocktype/AST/DeclContext.h`)
  - 父子上下文层级关系
  - 名称查找 (`lookup` / `lookupInContext`)
  - 声明迭代器
  - `DeclContextKind` 枚举
- ✅ `TranslationUnitDecl` 继承 `DeclContext`
- ✅ `NamespaceDecl` 继承 `DeclContext`
- ✅ `CXXRecordDecl` 继承 `DeclContext`
- ✅ `EnumDecl` 继承 `DeclContext`
- ✅ `LinkageSpecDecl` 继承 `DeclContext`

---

#### 2. ~~模板特化声明类缺失~~ ✅ 已实现

**已实现的类 (commit: 1ca9ee2 + 874ecf3):**
- ✅ `ClassTemplateSpecializationDecl` - 类模板全特化
- ✅ `ClassTemplatePartialSpecializationDecl` - 类模板偏特化
- ✅ `VarTemplateDecl` - 变量模板
- ✅ `VarTemplateSpecializationDecl` - 变量模板全特化
- ✅ `VarTemplatePartialSpecializationDecl` - 变量模板偏特化
- ✅ `FunctionTemplateDecl` - 函数模板
- ✅ `ClassTemplateDecl` - 类模板
- ✅ `TypeAliasTemplateDecl` - 别名模板

**解析支持 (commit: 874ecf3):**
- ✅ 显式特化检测 (`template<>`)
- ✅ 偏特化检测（通过作用域查找主模板）
- ✅ 创建正确的特化声明节点

---

#### 3. TemplateArgument 类型 ✅ 已补全

**所有类型已实现 (commit: 待提交):**
```cpp
enum class TemplateArgumentKind {
  Null,              // 空参数，错误恢复
  Type,              // 类型参数
  Declaration,       // 非类型参数解析为声明 (Phase 4 Sema 产出)
  NullPtr,           // nullptr 参数
  Integral,          // 编译期整数值 (Phase 4 Sema 产出，用 APSInt 存储)
  Template,          // 模板模板参数
  TemplateExpansion, // 带包展开的模板模板参数
  Expression,        // 未求值表达式 (Phase 3 Parser 产出)
  Pack,              // 参数包 (内含 TemplateArgument 数组)
};
```

**额外已实现:**
- ✅ `TemplateArgumentLoc` 类 (带 SourceLocation 的模板参数，Phase 4 Sema 诊断用)
- ✅ 完整的拷贝/移动/析构支持 (因 APSInt 有非平凡特殊成员)
- ✅ 向后兼容: `isNonType()` / `getAsNonType()` 映射到 Expression

**设计决策 (Phase 4 开发者必读):**
- **Expression → Integral**: Phase 3 Parser 产出 `Expression` 类型的参数。Phase 4 Sema
  应在求值后将其替换为 `Integral`（用 `getAsIntegral()` 获取 `APSInt`）。不要用 `Expr*`
  表示已求值的整常数。
- **Expression → Declaration**: 当非类型参数解析到具体声明时，Phase 4 Sema 应创建
  `Declaration` 类型的参数。不要用 `Expr*` 指向 DeclRefExpr 来代替。
- **Pack vs IsPackExpansion**: `IsPackExpansion` 标记单个参数后有 `...`（如 `Ts...`）。
  当需要表示多个参数组成的包时，使用 `Pack` kind（通过 `getAsPack()` 获取数组）。
  不要用 `std::vector` 或其他容器代替 `Pack` kind。

---

#### 4. 其他缺失的声明类型

- ❌ `IndirectFieldDecl` - 间接字段 (用于匿名联合体)
  - Phase 4 Sema 在处理匿名联合体成员访问时需要。应在 `AST/Decl.h` 中添加，
    继承 `ValueDecl`，包含字段链。不要用多个 `FieldDecl` 指针的 workaround。
- ❌ `UnresolvedUsingTypenameDecl` - 未解析的 using typename
  - Phase 4 Sema 模板实例化时需要。应在 `AST/Decl.h` 中添加，继承 `NamedDecl`。
- ❌ `UnresolvedUsingValueDecl` - 未解析的 using value
  - Phase 4 Sema 模板实例化时需要。应在 `AST/Decl.h` 中添加，继承 `NamedDecl`。
- ❌ `UsingPackDecl` - using 包展开 (C++17)
  - Phase 4 Sema 处理 `using typename... Ts` 时需要。应在 `AST/Decl.h` 中添加。

---

### Stage 3.2 - 基础声明解析 (80%)

#### ✅ 已实现的解析功能

**文件:** `src/Parse/ParseDecl.cpp` (94.3KB)

**核心解析器:**
- ✅ `parseDeclaration()` - 声明解析入口
- ✅ `parseFunctionDeclaration()` - 函数声明解析
- ✅ `parseEnumDeclaration()` - 枚举声明解析
- ✅ `parseNamespaceDeclaration()` - 命名空间解析
- ✅ `parseLinkageSpecDeclaration()` - 链接说明解析
- ✅ `parseDeductionGuide()` - 推导指引解析
- ✅ `parseAttributeSpecifier()` - 属性解析

**类型解析:**
- ✅ `parseDeclarator()` - 声明符解析 (在 ParseType.cpp)
- ✅ 类型说明符解析
- ✅ 模板特化类型解析

**初始化支持:**
- ✅ 变量初始化 (`= expr`, `{ list }`, `( exprs )`)
- ✅ 参数默认值

---

### ❌ Stage 3.2 缺失功能 (20%)

#### 1. 缺乏结构化的 DeclSpec/Declarator 架构 ⚠️

**规划要求:**
```cpp
struct DeclSpec {
  TypeSpecifier TypeSpec;
  StorageClass SC;
  FunctionSpecifiers FS;
  bool IsFriend, IsConstexpr, IsInline, IsVirtual, IsExplicit;
};

struct Declarator {
  DeclSpec DS;
  std::vector<DeclaratorChunk> Chunks;
  DeclarationName Name;
};

struct DeclaratorChunk {
  enum Kind { Pointer, Reference, Array, Function, MemberPointer };
};
```

**现状:**
- ❌ 没有明确的 `DeclSpec` 结构体
- ❌ 没有 `DeclaratorChunk` 机制
- ❌ 采用即席解析,而非分层架构

**影响:**
- 代码可维护性较差
- 复杂声明符 (如函数指针、成员指针) 可能处理不完整

**设计决策 (开发者必读):**
- **何时引入**: Phase3必须完成，因Phase4的 Sema 需要结构化的声明信息。
  不要在 Sema 中用 `std::tuple<QualType, llvm::StringRef, ...>` 等临时结构替代 `Declarator`。
- **如何引入**: 参考 Clang 的 `DeclSpec.h` / `Declarator.h`。最小化实现：
  `DeclSpec` 存储类型+说明符，`Declarator` 管理声明符链，`DeclaratorChunk` 表示
  指针/引用/数组/函数/成员指针。先定义数据结构，再逐步迁移 `parseDeclaration()`
  内的即席逻辑。
- **不要做**: 不要一次性重写所有解析函数。先让新架构和旧代码共存。

---

#### 2. 复杂声明符支持不完整 ⚠️

**可能缺失的场景:**
- ⚠️ 成员指针: `int Class::*ptr`
- ⚠️ 复杂函数指针: `int (*pf)(int)`
- ⚠️ 数组指针: `int (*arr)[10]`
- ⚠️ 指针数组: `int *ap[10]`

需要实际测试验证这些场景是否正常工作。

**设计决策 (Phase 4 开发者必读):**
- 如果复杂声明符解析失败，不要在 Sema 中用字符串匹配或手动解析来 workaround。
  应回到 `ParseType.cpp` 的 `parseDeclarator()` 中修复根本问题。
- 引入 `DeclaratorChunk::MemberPointer` 来处理 `int Class::*ptr`，不要用特殊的
  FieldDecl 标记来表示成员指针类型。

---

### Stage 3.3 - 类与模板解析 (70%)

#### ✅ 已实现的解析功能

**类解析 (集成在 ParseDecl.cpp):**
- ✅ `parseClassDeclaration()` - class/struct/union 解析
- ✅ `parseClassBody()` - 类体解析
- ✅ `parseClassMember()` - 成员解析
- ✅ 基类解析 (含 virtual 继承)
- ✅ 访问控制 (public/protected/private)
- ✅ 成员函数、构造函数、析构函数
- ✅ 嵌套类
- ✅ 友元声明

**模板解析 (集成在 ParseDecl.cpp):**
- ✅ `parseTemplateDeclaration()` - 模板声明
- ✅ `parseTemplateParameters()` - 模板参数列表
- ✅ `parseTemplateTypeParameter()` - 类型参数
- ✅ `parseTemplateTemplateParameter()` - 模板模板参数
- ✅ 参数包支持 (`...`)
- ✅ 默认模板参数
- ✅ 模板特化表达式解析 (在 ParseExpr.cpp)

**Concept 解析:**
- ✅ `parseConceptDefinition()` - Concept 定义
- ✅ 约束表达式解析

---

### ❌ Stage 3.3 缺失功能 (30%)

#### 1. 缺少独立的解析文件组织 ⚠️

**规划要求:**
- `src/Parse/ParseClass.cpp` - 类解析
- `src/Parse/ParseTemplate.cpp` - 模板解析

**现状:**
- ❌ 所有解析都在 `ParseDecl.cpp` 中
- ⚠️ 功能完整但组织不符合规划

**影响:**
- 代码可读性和可维护性降低
- 不影响功能,但违反架构设计

---

#### 2. ~~模板特化/偏特化解析缺失~~ ✅ 已改进

**已实现 (commit: 874ecf3):**
- ✅ 显式特化检测: `template<>` 后创建正确的特化声明节点
- ✅ 偏特化检测: 通过作用域查找同名主模板自动创建偏特化
- ✅ 类型约束解析: `parseTypeConstraint()` 支持 C++20 约束模板参数
- ✅ `parseTemplateParameter()` 支持约束参数 (`ConceptName T`)

**已知局限:**
- ⚠️ `class Vector<int>` 中的模板参数解析需要解析器更大规模改进
  - 当 `class` 关键字后跟 `Name<Args>` 时，`<` 被歧义为比较运算符

---

#### 3. ~~Requires 子句和类型约束解析不完整~~ ✅ 已改进

**已实现:**
- ✅ `parseRequiresClause()` - requires 子句解析
- ✅ `parseTypeConstraint()` - 类型约束解析 (如 `template<Sortable T>`)
- ✅ 约束模板参数支持 (`ConceptName ParamName`)

**已知局限:**
- ⚠️ 约束表达式的高级语法（合取 `&&`、析取 `||`）尚未测试

---

### Stage 3.4 - C++26 特性 + 测试 (50%)

#### ✅ 已实现的功能

**C++20 特性:**
- ✅ Concept 声明
- ✅ 模块系统 (`ModuleDecl`, `ImportDecl`, `ExportDecl`)
- ✅ Using enum
- ✅ 属性支持

**C++17 特性:**
- ✅ 推导指引 (`CXXDeductionGuideDecl`)
- ✅ 嵌套命名空间 (隐含支持)

**C++11 特性:**
- ✅ 类型别名 (`using = `)
- ✅ Auto 类型
- ✅ Decltype

**单元测试:**
- ✅ `DeclarationTest.cpp` (534 行)
- ✅ `ParserTest.cpp` (784 行)
- ✅ `StatementTest.cpp` (345 行)
- ✅ `ErrorRecoveryTest.cpp` (214 行)
- ✅ `AccessControlTest.cpp` (134 行)
- 总计: ~2011 行解析测试

---

### ❌ Stage 3.4 缺失功能 (50%)

#### 1. C++23/C++26 新特性缺失 🔴

**完全缺失:**
- ❌ **Deducing this / 显式对象参数** (C++23)
  ```cpp
  struct S {
    void f(this S& self);  // ❌ 不支持
    void g(this auto& self);  // ❌ 不支持
  };
  ```

- ❌ **占位符变量** (C++26)
  ```cpp
  auto _ = getValue();  // ❌ _ 作为特殊占位符
  ```

- ❌ **Contracts 属性** (P2900)
  ```cpp
  void f(int x)
    [[pre: x > 0]]      // ❌ 前置条件
    [[post: result > 0]]; // ❌ 后置条件
  ```

- ❌ **静态反射类型** (C++26)
  ```cpp
  reflexpr(T)  // ❌ 不支持
  ```

---

#### 2. ~~LLVM Lit 回归测试完全缺失~~ ✅ 已补充

**现有 Lit 测试 (21 个测试文件):**

| 目录 | 文件 | 覆盖内容 |
|------|------|---------|
| `basic/` | `help.test`, `version.test` | 基础命令行 |
| `lex/` | `keywords.test`, `literals.test`, `operators.test`, `chinese-keywords.test` | 词法分析 |
| `preprocess/` | `macros.test`, `predefined-macros.test` | 预处理 |
| `parser/` | `basic.test`, `class.test`, `template.test`, `errors.test` | 基础解析 |
| `parser/` | `namespace.test` ✨ | 命名空间 |
| `parser/` | `using.test` ✨ | using/typedef/static_assert |
| `parser/` | `enum.test` ✨ | 枚举/friend |
| `parser/` | `concept.test` ✨ | Concept/requires |
| `parser/` | `declarations.test` ✨ | 基本声明 |
| `ast-dump/` | `expressions.test` ✨ | 表达式 AST |
| `ast-dump/` | `class-declarations.test` ✨ | 类声明 AST |
| `ast-dump/` | `template-declarations.test` ✨ | 模板声明 AST |
| `ast-dump/` | `statements.test` ✨ | 语句 AST |

✨ = 本次新增 (commit: 874ecf3)

**影响:**
- 无法进行回归测试
- 难以保证解析器的稳定性
- 违反验收标准

---

#### 3. 高级错误恢复机制不完整 ⚠️

**规划要求:**
- `skipUntilNextDeclaration()` - 跳到下一个声明
- `skipUntilBalanced()` - 括号匹配恢复
- `tryRecoverMissingSemicolon()` - 缺失分号恢复

**现状:**
- ⚠️ 有基本的错误恢复 (`ErrorRecoveryTest.cpp`)
- ❌ 缺少声明级别的专门恢复策略
- ❌ 缺少智能错误提示和建议

**设计决策 (Phase 4 开发者必读):**
- 声明级恢复策略应在 `Parser` 中实现（添加 `skipUntilNextDeclaration()` 方法），
  不要在 Sema 中用 try-catch 模拟。
- 智能错误提示应通过 `DiagnosticsEngine` 的扩展实现，不要在各处散落
  `OS << "did you mean..."` 式的临时诊断。

---

#### 4. 测试覆盖率缺口

**缺失的测试场景:**
- ❌ 模板特化声明测试
- ❌ 偏特化测试
- ❌ 推导指引测试
- ❌ 复杂声明符测试 (成员指针、函数指针)
- ❌ Contracts 测试 (功能未实现)
- ❌ Deducing this 测试 (功能未实现)

---

## 🎯 关键问题优先级

### 🔴 高优先级 (阻塞后续开发)

1. **~~模板特化声明类缺失~~** ✅ 已实现
   - 已完成: 8 个特化声明类 + TemplateParameterList 重构

2. **~~DeclContext 基础设施缺失~~** ✅ 已实现
   - 已完成: DeclContext 类 + 5 个容器类集成

3. **~~LLVM Lit 回归测试缺失~~** ✅ 已补充
   - 已完成: 21 个测试文件覆盖解析器和 AST dump

---

### ⚠️ 中优先级 (影响功能完整性)

4. **~~模板特化/偏特化解析缺失~~** ✅ 已改进
   - 已完成: 显式特化和偏特化检测 + parseTypeConstraint

5. **结构化 DeclSpec/Declarator 架构**
   - 影响: 代码可维护性
   - 工作量: 较大 (需重构)
   - 建议: 视情况决定是否重构

6. **C++23/26 新特性**
   - 影响: 语言标准支持不完整
   - 工作量: 小到中等
   - 建议: 可以延后到 Phase 7

---

### 💡 低优先级 (可选优化)

7. **~~解析文件组织优化~~** 可选 (不影响功能)
   - 影响: 代码组织结构
   - 工作量: 小 (主要是移动代码)
   - 建议: 有时间再做

8. **~~TemplateArgument 类型补全~~** ✅ 已完成
   - 已完成: 9 种 TemplateArgumentKind + TemplateArgumentLoc
   - 含决策说明: Expression→Integral 转换规则、Pack 用法

9. **高级错误恢复**
   - 影响: 用户体验
   - 工作量: 中等
   - 建议: 后期优化

---

## 📋 建议的补救计划

### 阶段 1: 紧急修复 (1-2 周)

**目标:** 解决阻塞性问题

1. 实现模板特化声明类
   - `ClassTemplateSpecializationDecl`
   - `ClassTemplatePartialSpecializationDecl`
   - `FunctionTemplateDecl`
   - `ClassTemplateDecl`
   - `VarTemplateDecl` 等

2. 实现模板特化/偏特化解析
   - `parseExplicitSpecialization()`
   - `parsePartialSpecialization()`

3. 补充关键的 Lit 回归测试
   - 至少覆盖基本声明、类、模板

---

### 阶段 2: 基础设施完善 (2-3 周)

**目标:** 为 Phase 4 语义分析打好基础

1. 实现 DeclContext 基础设施
   - 创建独立的 `DeclContext` 类
   - 重构现有容器类使用 DeclContext
   - 实现统一的 `lookup()` 方法

2. ~~补全 TemplateArgument 类型~~ ✅ 已完成
   - 已添加: 9 种类型枚举 + TemplateArgumentLoc + 完整特殊成员函数

3. 完善 Requires 子句和类型约束解析

---

### 阶段 3: 功能增强 (1-2 周)

**目标:** 提升功能完整性

1. 实现 C++23 deducing this
2. 实现占位符变量 `_`
3. 补充测试覆盖率
4. 优化错误恢复机制

---

### 阶段 4: 代码优化 (可选)

**目标:** 提升代码质量

1. 考虑是否重构为结构化 DeclSpec/Declarator
2. 分离 ParseClass.cpp 和 ParseTemplate.cpp
3. 完善文档和注释

---

## 📊 结论

**Phase 3 当前完成度: ~84%** (从 69% 提升)

**核心功能状态:**
- ✅ 基础声明 AST 节点: 完整
- ✅ 基础声明解析: 完整
- ✅ 类定义解析: 完整
- ✅ 模板参数解析: 完整
- ✅ Concept 支持: 基本完整
- ✅ 模板特化声明类: **已实现**
- ✅ DeclContext 基础设施: **已实现**
- ✅ Lit 回归测试: **21 个测试文件**

**关键缺失:**
- ⚠️ 模板特化表达式在类声明中的歧义解析
  - 决策: 需要引入 DeclSpec/Declarator 架构来根本解决。不要在 `parseClassDeclaration`
    中添加更多特殊情况的 `<` 消歧逻辑。Phase 4 引入 Declarator 时一并解决。
- ⚠️ C++23/26 新特性 (Deducing this, Contracts)
  - 决策: 延后到 Phase 7 统一实现。需要时添加新的 AST 节点类型，不要用
    `FunctionDecl` 的 extra 字段来模拟 Deducing this。
- ⚠️ 结构化 DeclSpec/Declarator 架构
  - 决策: Phase 4 启动时同步引入。先定义 `DeclSpec`/`Declarator`/`DeclaratorChunk`
  数据结构，然后逐步迁移 `parseDeclaration()` 中的即席逻辑。

**对后续阶段的影响:**
- Phase 4 (语义分析): ✅ 可以开始, DeclContext 已就绪
- Phase 5 (模板实例化): ⚠️ 基本就绪, 类声明中的特化解析需改进
- Phase 6 (IR 生成): 暂不受影响
- Phase 7 (C++26 特性): 需要补充新特性实现

**建议:**
1. Phase 4 可以启动, DeclContext 已提供名称查找基础设施
2. 模板特化解析歧义需要 DeclSpec/Declarator 架构改进来解决
3. C++23/26 特性可以延后到 Phase 7 统一实现

---

*报告生成时间: 2026-04-17*
*基于 commit: 1ca9ee2*
