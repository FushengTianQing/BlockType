# P0 审查报告：Sema 层代码 — AST 重构后状态

**审查人**: reviewer  
**审查日期**: 2026-04-23  
**审查范围**: src/Sema/, include/blocktype/Sema/  
**审查标准**: Clang/GCC 编译器代码质量标准

---

## 审查摘要

| 指标 | 数值 |
|------|------|
| 审查文件数 | 21 (.cpp) + 16 (.h) |
| 总代码行数 | ~8,000+ |
| 发现问题数 | 18 |
| P0 问题 | 3 |
| P1 问题 | 6 |
| P2 问题 | 5 |
| P3 问题 | 4 |

---

## P0 问题（阻塞级 — 必须立即修复）

### AUDIT-001: 模板实例化 substituteFunctionSignature 为空壳实现

**文件**: `src/Sema/TemplateInstantiation.cpp:74-103`  
**严重程度**: P0  
**类型**: 遗漏功能实现

**问题描述**:  
`TemplateInstantiation::substituteFunctionSignature` 是模板实例化的核心函数，负责创建替换了模板参数的新 FunctionDecl。当前实现直接返回原始函数指针（第102行 `return Original;`），**完全跳过了类型替换**。这意味着所有函数模板实例化实际上并未发生，模板函数调用将使用未替换的原始签名。

**影响范围**: 
- 所有函数模板实例化结果错误
- 模板函数的参数类型和返回类型未替换
- 下游 CodeGen 生成的 IR 类型错误

**修改建议**:  
实现完整的函数签名替换：
1. 替换返回类型：`QualType NewRetType = substituteType(Original->getReturnType())`
2. 替换参数类型：对每个参数执行 `substituteType`
3. 使用 `ASTContext::create<FunctionDecl>` 创建新函数
4. 复制函数体（使用 StmtCloner）
5. 设置模板实例化标志

**预期完成时间**: 2-3天

---

### AUDIT-002: ConstraintSatisfaction 表达式替换未实现（3处 TODO）

**文件**: `src/Sema/ConstraintSatisfaction.cpp:162, 190, 399`  
**严重程度**: P0  
**类型**: 遗漏功能实现

**问题描述**:  
约束满足检查中有3处标记为 `TODO: Implement expression substitution`，当前代码直接使用原始表达式（`Expr *SubstE = E;`）。这意味着：
- `EvaluateExprRequirement` (第162行): 带模板参数的简单表达式要求未替换
- `EvaluateCompoundRequirement` (第190行): 复合要求的表达式未替换
- `SubstituteAndEvaluate` (第399行): 核心替换+求值函数未实现替换

**影响范围**:  
- C++20 Concepts 约束检查在模板参数存在时完全失效
- `requires` 表达式中的模板参数不会被替换
- 约束部分序判断可能产生错误结果

**修改建议**:  
利用已有的 `TemplateInstantiator` 实现表达式替换：
```cpp
Expr *SubstE = Instantiator.substituteExpr(E, Args);
if (!SubstE) return std::nullopt; // SFINAE
```

**预期完成时间**: 2天

---

### AUDIT-003: TypeDeduction 模板参数推导完全未实现

**文件**: `src/Sema/TypeDeduction.cpp:186-192`  
**严重程度**: P0  
**类型**: 遗漏功能实现

**问题描述**:  
`TypeDeduction::deduceTemplateArguments` 是模板参数推导的入口，当前实现直接 `return false;`。虽然 `TemplateDeduction.cpp` 有更完整的推导实现，但 `TypeDeduction` 中的此函数被 Sema 主流程调用时将永远失败。

**影响范围**:  
- 通过 TypeDeduction 路径发起的模板参数推导永远失败
- 可能导致某些模板推导路径走不通

**修改建议**:  
将此函数委托给 `TemplateDeduction::DeduceFunctionTemplateArguments`，或删除此冗余入口统一使用 TemplateDeduction。

**预期完成时间**: 0.5天

---

## P1 问题（严重 — 本周修复）

### AUDIT-004: Sema.cpp 文件过大（3273行），职责过重

**文件**: `src/Sema/Sema.cpp`  
**严重程度**: P1  
**类型**: 代码架构不合理

**问题描述**:  
Sema.cpp 包含了声明处理、表达式处理、语句处理、模板处理等所有语义分析逻辑。作为对比，Clang 将 Sema 拆分为 SemaDecl.cpp、SemaExpr.cpp、SemaStmt.cpp、SemaTemplate.cpp 等数十个文件。

**修改建议**:  
按功能域拆分：
- `SemaDecl.cpp` — 声明相关 ActOn 方法
- `SemaExpr.cpp` — 表达式相关 ActOn 方法  
- `SemaStmt.cpp` — 语句相关 ActOn 方法
- `SemaTemplate.cpp` — 已存在，确认所有模板逻辑已迁移

**预期完成时间**: 3-5天（可渐进式）

---

### AUDIT-005: ActOnCallExpr 中重复的模板查找逻辑

**文件**: `src/Sema/Sema.cpp:2131-2170`  
**严重程度**: P1  
**类型**: 不合理的简化方案

**问题描述**:  
在 `ActOnCallExpr` 中，当 `D=nullptr` 时（第2138-2152行）和当 D 非 null 但 FD 仍为 null 时（第2157-2169行），执行了几乎相同的模板查找逻辑（`Symbols.lookupTemplate(Name)` → `DeduceAndInstantiateFunctionTemplate`）。这是代码重复，且两次查找可能产生不一致的结果。

**修改建议**:  
统一模板查找逻辑为单一路径：
```cpp
if (!FD && !Name.empty()) {
  if (auto *FTD = Symbols.lookupTemplate(Name)) {
    if (auto *FuncFTD = dyn_cast<FunctionTemplateDecl>(FTD))
      FD = DeduceAndInstantiateFunctionTemplate(FuncFTD, Args, LParenLoc);
  }
}
```

**预期完成时间**: 0.5天

---

### AUDIT-006: deduceReturnTypeFromBody 不递归处理嵌套语句

**文件**: `src/Sema/Sema.cpp:35-63`  
**严重程度**: P1  
**类型**: 不合理的简化方案

**问题描述**:  
`deduceReturnTypeFromBody` 仅遍历 `CompoundStmt` 的直接子语句。对于嵌套在 if/for/while 中的 return 语句，无法找到返回类型。例如：
```cpp
auto f(bool b) { if (b) return 1; return 0; } // 无法推导
```

**修改建议**:  
实现递归的语句遍历，使用 StmtVisitor 模式遍历所有子语句寻找 ReturnStmt。

**预期完成时间**: 1天

---

### AUDIT-007: TypeCheck::CheckDirectInitialization 跳过多参数构造函数检查

**文件**: `src/Sema/TypeCheck.cpp:100-103`  
**严重程度**: P1  
**类型**: 遗漏功能实现（TODO 标记）

**问题描述**:  
多参数直接初始化时（`ClassT obj(a, b, c);`），函数直接 `return true;` 而不检查构造函数重载决议。注释标记为 `TODO: constructor overload resolution`。

**影响范围**:  
- 多参数构造函数调用不做类型检查
- 错误的构造函数参数不会报错

**修改建议**:  
实现构造函数重载决议：查找类中所有构造函数，使用 OverloadCandidateSet 选择最佳匹配。

**预期完成时间**: 2天

---

### AUDIT-008: TypeCheck::getIntegerCommonType 简化实现忽略符号性规则

**文件**: `src/Sema/TypeCheck.cpp:657-677`  
**严重程度**: P1  
**类型**: 不合理的简化方案

**问题描述**:  
C++ [expr.arith.conv] 对整数公共类型的规则涉及符号性比较：当无符号类型的秩大于等于有符号类型时，应使用无符号类型；否则需要检查有符号类型是否能表示无符号类型的所有值。当前实现仅比较秩（`if (Rank1 >= Rank2) return T1;`），忽略了符号性。

**示例**: `int + unsigned int` 应结果为 `unsigned int`，但当前可能返回 `int`。

**修改建议**:  
实现完整的 C++ 整数转换规则，参考 Clang 的 `ASTContext::getIntegerTypeOrder` 和符号性处理。

**预期完成时间**: 1天

---

### AUDIT-009: Overload::addCandidates 不处理 FunctionTemplateDecl

**文件**: `src/Sema/Overload.cpp:220-227`  
**严重程度**: P1  
**类型**: 遗漏功能实现（TODO 标记）

**问题描述**:  
`addCandidates` 遍历 LookupResult 时，只处理 `FunctionDecl`，跳过 `FunctionTemplateDecl`。标记为 `TODO: handle FunctionTemplateDecl by attempting instantiation`。

**影响范围**:  
- 重载决议无法考虑函数模板候选
- 模板函数与非模板函数重载时总是选择非模板函数

**修改建议**:  
对 FunctionTemplateDecl 执行模板参数推导，将实例化结果加入候选集。

**预期完成时间**: 1-2天

---

## P2 问题（中等 — 本月修复）

### AUDIT-010: ActOnGotoStmt 未解析标签到实际 LabelDecl

**文件**: `src/Sema/Sema.cpp:2634-2638`  
**严重程度**: P2  
**类型**: 遗漏功能实现（TODO 标记）

**问题描述**:  
`ActOnGotoStmt` 为每个 goto 创建新的 LabelDecl 而不是查找已有的标签声明。这导致 goto 跳转目标无法与实际标签关联。

**修改建议**:  
在当前作用域链中查找已有 LabelDecl，若找到则使用它；若未找到则创建前向声明并注册。

**预期完成时间**: 0.5天

---

### AUDIT-011: ActOnVarDecl 双重注册（Scope + SymbolTable）

**文件**: `src/Sema/Sema.cpp:329-351`  
**严重程度**: P2  
**类型**: 代码架构不合理

**问题描述**:  
`ActOnVarDecl` 同时调用 `Symbols.addDecl(VD)` 和 `CurContext->addDecl(VD)`，但没有使用统一的 `registerDecl` 方法。而 `registerDecl` 的逻辑是仅在 TU 作用域时才添加到 SymbolTable。这导致局部变量也被添加到全局 SymbolTable，可能造成名称污染。

**修改建议**:  
使用 `registerDecl(VD)` 替代直接的 `Symbols.addDecl(VD)` 调用，确保局部变量仅注册到 Scope。

**预期完成时间**: 0.5天

---

### AUDIT-012: TypeCheck::CheckListInitialization 跳过数组和聚合类型检查

**文件**: `src/Sema/TypeCheck.cpp:124-131`  
**严重程度**: P2  
**类型**: 遗漏功能实现（2处 TODO 标记）

**问题描述**:  
列表初始化中，数组元素的类型兼容性检查和聚合初始化检查均标记为 TODO，直接 `return true;`。

**修改建议**:  
对数组类型逐个检查元素类型；对聚合类型按 C++ [dcl.init.aggr] 检查。

**预期完成时间**: 1-2天

---

### AUDIT-013: Sema::InstantiateClassTemplate 中模板参数类型假设错误

**文件**: `src/Sema/Sema.cpp:162-165`  
**严重程度**: P2  
**类型**: 潜在错误

**问题描述**:  
在构建替换映射时，代码假设所有模板参数都是 `TypedefNameDecl`（`dyn_cast_or_null<TypedefNameDecl>(Params[i])`），但模板类型参数实际上是 `TemplateTypeParmDecl`，它继承自 `TypeDecl` 而非 `TypedefNameDecl`。这导致替换映射永远为空，类模板实例化时所有字段类型不会被替换。

**修改建议**:  
改为 `dyn_cast_or_null<TemplateTypeParmDecl>(Params[i])`，并相应调整 `addSubstitution` 的键类型。

**预期完成时间**: 0.5天

---

### AUDIT-014: TemplateInstantiation::hasUnsubstitutedParams 永远返回 false

**文件**: `src/Sema/TemplateInstantiation.cpp:105-109`  
**严重程度**: P2  
**类型**: 遗漏功能实现（TODO 标记）

**问题描述**:  
此函数应检查类型中是否仍包含未替换的模板参数，但当前直接返回 false。这可能导致过早认为类型已完全替换。

**修改建议**:  
递归遍历类型，检查是否包含 TemplateTypeParmType。

**预期完成时间**: 0.5天

---

## P3 问题（轻微 — 季度内修复）

### AUDIT-015: ActOnFinishOfFunctionDef 中 auto 返回类型推导使用 DummyExpr

**文件**: `src/Sema/Sema.cpp:400-405`  
**严重程度**: P3  
**类型**: 不合理的简化方案

**问题描述**:  
auto 返回类型推导时，创建了一个 `IntegerLiteral` 作为 DummyExpr 传给 `deduceAutoType`。这个 DummyExpr 的类型已经被设为 DeducedType，所以 `deduceAutoType` 实际上没有执行任何推导逻辑（输入类型已经等于输出类型）。这是循环逻辑。

**修改建议**:  
直接使用从 `deduceReturnTypeFromBody` 获取的类型，无需再经过 `deduceAutoType`。

**预期完成时间**: 0.5天

---

### AUDIT-016: Scope 管理使用裸 new/delete 而非 BumpPtrAllocator

**文件**: `src/Sema/Sema.cpp:94-101`  
**严重程度**: P3  
**类型**: 不符合 C++ 编译器最佳实践

**问题描述**:  
`PushScope` 使用 `new Scope(...)`，`PopScope` 使用 `delete CurrentScope`。Clang/LLVM 中 Scope 对象通常由 BumpPtrAllocator 分配以提升性能。

**修改建议**:  
改用 ASTContext 的 BumpPtrAllocator 分配 Scope 对象。

**预期完成时间**: 1天

---

### AUDIT-017: ExprResult/StmtResult/DeclResult 缺少移动语义和隐式转换

**文件**: `include/blocktype/Sema/Sema.h:49-111`  
**严重程度**: P3  
**类型**: 不符合 C++ 最佳实践

**问题描述**:  
Clang 的 ExprResult/StmtResult 支持从指针隐式转换、移动构造等。当前实现只有显式构造和 `get()` 方法，缺少 `operator*`、`operator->` 等便利接口。

**修改建议**:  
参考 Clang 的实现添加便利接口，但注意保持错误安全性。

**预期完成时间**: 0.5天

---

### AUDIT-018: CMakeLists.txt 声称 C++23 但项目描述为 C++26

**文件**: `CMakeLists.txt:9-10`  
**严重程度**: P3  
**类型**: 配置不一致

**问题描述**:  
`set(CMAKE_CXX_STANDARD 23)` 但项目 README 和架构文档声称支持 C++26。如果需要 C++26 特性（如反射 P2996、Contracts P2900），应使用 C++26 标准。

**修改建议**:  
确认项目实际使用的 C++ 标准版本，更新 CMakeLists.txt 或项目描述使其一致。

**预期完成时间**: 0.5天

---

## 审查结论

### 关键风险
Sema 层存在 **3个 P0 级别的空壳实现**，直接影响编译器的核心功能：
1. 模板实例化未真正执行类型替换
2. Concepts 约束替换未实现
3. TypeDeduction 模板推导入口为空

这些问题意味着 **BlockType 当前无法正确编译任何使用函数模板或 C++20 Concepts 的程序**。

### 代码质量评估
| 维度 | 评分 | 说明 |
|------|------|------|
| 架构设计 | 7/10 | 模块划分合理，但 Sema.cpp 过大 |
| 代码正确性 | 4/10 | 多个核心功能为空壳实现 |
| C++ 最佳实践 | 6/10 | 遵循 Clang 风格但有偏差 |
| 测试覆盖 | 6/10 | 有单元测试但未覆盖空壳路径 |
| 文档质量 | 7/10 | 注释和 Doxygen 较完善 |

### 修复优先级建议
1. **立即**: AUDIT-001, AUDIT-002, AUDIT-003（P0 空壳实现）
2. **本周**: AUDIT-005, AUDIT-006, AUDIT-013（P1 可快速修复）
3. **本月**: AUDIT-004, AUDIT-007, AUDIT-008, AUDIT-009（P1 需设计）
4. **季度**: AUDIT-010 至 AUDIT-018（P2/P3）

---

**报告生成时间**: 2026-04-23  
**审查人**: reviewer
