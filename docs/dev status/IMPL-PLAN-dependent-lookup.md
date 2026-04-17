# 依赖类型查找完整实现方案

最后更新：2026-04-18

## 问题概述

Phase 5 审计报告中有两个标记为"已修复"但实际未完全解决的问题：

1. **LookupUnqualifiedName 依赖检测** — `ScopeFlags::TemplateScope` 已定义但 Parser 从未使用，模板参数未通过 Scope 引入
2. **依赖类型查找** — `LookupQualifiedName` 有 `isDependentType()` 检测，但 `LookupUnqualifiedName` 在模板上下文中无感知；`SubstituteDependentType` 替换后不真正解析 `T::name` 成员

## 当前状态

| 组件 | 状态 | 问题 |
|------|------|------|
| `ScopeFlags::TemplateScope` | 已定义(0x10) | Parser 从未 `pushScope(TemplateScope)` |
| `LookupUnqualifiedName` | 无模板感知 | 不检测 scope 链中的 TemplateScope |
| `LookupQualifiedName` | ✅ 有 `isDependentType()` | 已处理 |
| `SubstituteDependentType` | 替换后返回新 DependentType | 不真正解析 `T::name` 中的 name |
| `LookupResult` | 无依赖标记 | 无法表示"查找在依赖上下文中失败" |
| `Expr` 节点 | 无依赖查找表达式 | 需要 `CXXDependentScopeMemberExpr` |
| `Sema::isDependentContext()` | 不存在 | 无法判断当前是否在模板定义内 |

## 分层方案

### Layer 1: Parser 模板作用域 (Layer1-Parser-TemplateScope)

**目标**：Parser 在解析 `template<typename T>` 后创建 TemplateScope，将模板参数加入 Scope。

**修改文件**：
- `src/Parse/ParseTemplate.cpp`

**具体步骤**：

1. 在 `parseTemplateDeclaration()` 中，解析完模板参数列表 `>` 后、解析声明体之前：
   - `pushScope(ScopeFlags::TemplateScope)`
   - 将 `Params` 中的每个 `NamedDecl` 通过 `CurrentScope->addDeclAllowRedeclaration(P)` 加入 Scope
2. 在 `parseDeclaration()` 返回后：
   - `popScope()`
3. 处理显式特化 `template<>` 和显式实例化的特殊情况（这两种不创建 TemplateScope）

**注意**：
- 概念定义 `template<typename T> concept C = ...` 也需要 TemplateScope
- 模板模板参数的嵌套 `template<typename> template<typename T>` 不额外创建 Scope（内层参数属于 TemplateTemplateParmDecl）

---

### Layer 2: LookupUnqualifiedName 依赖检测 (Layer2-Lookup-Dependent)

**目标**：`LookupUnqualifiedName` 检测 scope 链中的 TemplateScope，标记查找结果为依赖。

**修改文件**：
- `include/blocktype/Sema/Lookup.h` — `LookupResult` 新增 `isDependent` 标志
- `src/Sema/Lookup.cpp` — `LookupUnqualifiedName` 中遍历 scope 链检测 TemplateScope

**具体步骤**：

1. **`LookupResult` 新增字段**：
   ```cpp
   bool Dependent = false;  // 查找发生在依赖上下文中
   void setDependent(bool D = true) { Dependent = D; }
   bool isDependent() const { return Dependent; }
   ```

2. **`LookupUnqualifiedName` 逻辑**：
   - 在 scope 链遍历前初始化 `bool InTemplateScope = false`
   - 遍历 scope 链时，如果 `Cur->hasFlags(ScopeFlags::TemplateScope)`，设置 `InTemplateScope = true`
   - 在函数返回前：如果 `InTemplateScope && Result.empty()`，设置 `Result.setDependent(true)`
   - 含义：名称在模板作用域内未找到，可能是依赖名称，将在实例化时解析

3. **不改变现有行为**：非模板作用域内的查找完全不受影响

---

### Layer 3: SubstituteDependentType 真正解析 (Layer3-ResolveDependentMember)

**目标**：`SubstituteDependentType` 替换 BaseType 后，如果结果是非依赖 RecordType，则通过 `LookupQualifiedName` 真正解析 `T::name`。

**修改文件**：
- `src/Sema/TemplateInstantiation.cpp` — `SubstituteDependentType`
- `include/blocktype/Sema/Sema.h` — 新增 `LookupMemberType` 辅助方法

**具体步骤**：

1. **修改 `SubstituteDependentType`**：
   ```
   当前流程：
     SubstituteType(BaseType) → SubBase
     return DependentType(SubBase, Name)  // 仍然依赖！

   新流程：
     SubstituteType(BaseType) → SubBase
     if SubBase 仍然依赖 → return DependentType(SubBase, Name)  // 保持原样
     if SubBase 是 RecordType →
       用 LookupQualifiedName(Name, NNS(SubBase)) 查找成员
       if 找到 TypeDecl → return 实际类型
       else → return DependentType(SubBase, Name)  // 解析失败，保持依赖
     else → return DependentType(SubBase, Name)  // 非记录类型，保持依赖
   ```

2. **新增 `Sema::LookupMemberType`** 辅助方法：
   - 输入：`QualType Base, llvm::StringRef MemberName`
   - 构造 `NestedNameSpecifier` 指向 Base 类型
   - 调用 `LookupQualifiedName(MemberName, NNS)`
   - 如果找到 `TypeDecl`，返回其 QualType；否则返回空

3. **需要 `TemplateInstantiator` 访问 `Sema` 的查找功能**：已有 `SemaRef` 成员

---

### Layer 4: 完整两阶段查找 (Layer4-TwoPhaseLookup)

**目标**：支持模板定义时识别依赖名称，实例化时重新查找。

**修改文件**：
- `include/blocktype/AST/NodeKinds.def` — 新增 2 个 Expr Kind
- `include/blocktype/AST/Expr.h` — 新增 2 个 Expr 类
- `src/AST/Expr.cpp` — 新增 dump 实现
- `include/blocktype/Sema/Sema.h` — 新增 `isDependentContext()` 方法
- `src/Sema/Sema.cpp` — 跟踪模板上下文
- `src/Sema/Lookup.cpp` — 依赖上下文查找返回特殊结果
- `src/Parse/ParseExpr.cpp` — 解析 `x.member` 时如果 base 是依赖类型则创建 `CXXDependentScopeMemberExpr`
- `src/Sema/TemplateInstantiation.cpp` — `SubstituteExpr` 处理新 Expr 类型

**新增 AST 节点**：

#### 4.1 `CXXDependentScopeMemberExpr`

表示在依赖类型上的成员访问，如 `T::member` 或 `x.member`（x 的类型是 T）。

```cpp
class CXXDependentScopeMemberExpr : public Expr {
  Expr *Base;                    // 基础表达式（可能为 null，如 T::foo）
  QualType BaseType;             // 基础类型（当 Base 为 null 时使用）
  bool IsArrow;                  // . 还是 ->
  llvm::StringRef MemberName;    // 成员名

public:
  // 构造、getter、isTypeDependent()=true、dump、classof
};
```

#### 4.2 `DependentScopeDeclRefExpr`

表示依赖类型的限定名称引用，如 `T::type` 或 `typename T::value_type`。

```cpp
class DependentScopeDeclRefExpr : public Expr {
  NestedNameSpecifier *Qualifier;  // 限定符（如 T::）
  llvm::StringRef DeclName;        // 名称（如 type）

public:
  // 构造、getter、isTypeDependent()=true、dump、classof
};
```

#### 4.3 NodeKinds.def 新增

```
EXPR(CXXDependentScopeMemberExpr, Expr)
EXPR(DependentScopeDeclRefExpr, Expr)
```

#### 4.4 Sema 模板上下文追踪

```cpp
// Sema.h 新增
bool isDependentContext() const;
unsigned TemplateDefinitionDepth = 0;

// 进入模板定义时 ++TemplateDefinitionDepth
// 退出模板定义时 --TemplateDefinitionDepth
// isDependentContext() = TemplateDefinitionDepth > 0
```

#### 4.5 Lookup 依赖上下文行为

当 `isDependentContext()` 为 true 时：
- `LookupUnqualifiedName` 对未找到的名称不报错，返回带 `Dependent=true` 的空结果
- 调用方（Parser/Sema）检测到此结果时创建 `DependentScopeDeclRefExpr`

#### 4.6 SubstituteExpr 处理新节点

- `CXXDependentScopeMemberExpr`：
  1. 替换 BaseType
  2. 如果 BaseType 变为非依赖 RecordType，用 `LookupQualifiedName` 查找成员
  3. 成功则创建 `MemberExpr`；失败则报告错误

- `DependentScopeDeclRefExpr`：
  1. 替换 Qualifier 中的类型
  2. 如果变为非依赖，用 `LookupQualifiedName` 查找
  3. 成功则创建 `DeclRefExpr`；失败则报告错误

---

## 实施顺序

| 步骤 | Layer | 内容 | 预计改动量 |
|------|-------|------|-----------|
| 1 | L1 | Parser pushScope(TemplateScope) | ~15 行 |
| 2 | L2 | LookupResult + LookupUnqualifiedName 依赖检测 | ~20 行 |
| 3 | L3 | SubstituteDependentType 真正解析 | ~40 行 |
| 4 | L4.1-4.3 | 新增 2 个 Expr AST 节点 + NodeKinds.def | ~120 行 |
| 5 | L4.4 | Sema isDependentContext() 追踪 | ~20 行 |
| 6 | L4.5 | Lookup 依赖上下文行为 | ~15 行 |
| 7 | L4.6 | SubstituteExpr 处理新节点 | ~50 行 |
| 8 | 测试 | 编译验证 + 新增测试用例 | ~100 行 |
| 9 | 文档 | 更新核查报告 | ~20 行 |

每步完成后编译验证，确保 568 个现有测试全部通过。

## 文件修改清单

| 文件 | Layer | 操作 |
|------|-------|------|
| `src/Parse/ParseTemplate.cpp` | L1 | 修改 |
| `include/blocktype/Sema/Lookup.h` | L2 | 修改 |
| `src/Sema/Lookup.cpp` | L2, L4.5 | 修改 |
| `include/blocktype/Sema/Sema.h` | L3, L4.4 | 修改 |
| `src/Sema/TemplateInstantiation.cpp` | L3, L4.6 | 修改 |
| `include/blocktype/AST/NodeKinds.def` | L4.1 | 修改 |
| `include/blocktype/AST/Expr.h` | L4.2 | 修改 |
| `src/AST/Expr.cpp` | L4.2 | 修改 |
| `src/Sema/Sema.cpp` | L4.4 | 修改 |
| `src/Parse/ParseExpr.cpp` | L4.5 | 修改 |
| `tests/unit/Sema/DependentLookupTest.cpp` | 测试 | 新建 |
| `docs/dev status/Phase5-AUDIT-Stage5-1+5-2 全面核查报告.md` | 文档 | 修改 |
