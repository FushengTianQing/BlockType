# Stage 4.4 + 4.5 全面核查报告

> 核查时间: 2026-04-18
> 核查范围: Stage 4.4 (重载决议) + Stage 4.5 (语义检查)
> 对照文档: `docs/plan/04-PHASE4-sema-basics/04-PHASE4-sema-basics.md`
> 参照基准: Clang 同模块功能特性

---

## 一、Stage 4.4 重载决议

### Task 4.4.1 重载候选集

| 检查项 | 规划文档要求 | 实现 | 状态 |
|--------|------------|------|------|
| `OverloadCandidate` 类 | 头文件与规划完全一致 | 无修改 | ✅ OK |
| `OverloadCandidateSet` 类 | 头文件与规划完全一致 | 无修改 | ✅ OK |
| `addCandidate` | 创建并返回候选 | 已实现 | ✅ OK |
| `addCandidates` | 从 LookupResult 提取 FunctionDecl | 已实现，含 FunctionTemplate TODO | ✅ OK |
| `getViableCandidates` | 返回可行候选 | 已实现 | ✅ OK |

### Task 4.4.2 隐式转换与排序

| 检查项 | 规划文档要求 | 实现 | 状态 |
|--------|------------|------|------|
| `ConversionRank` 枚举 | 6 个值 ExactMatch→BadConversion | 完全一致 | ✅ OK |
| `StandardConversionKind` 枚举 | 5 个值 | 完全一致 | ✅ OK |
| `SCS::compare()` | 按 Rank 比较，同 Rank 比子步骤 | 已实现 | ✅ OK |
| `ICS::compare()` | 按 Kind 优先级比较 | 已实现 | ✅ OK |
| `isIntegralPromotion` | bool→int, char→int, short→int | 已实现 | ✅ OK |
| `isFloatingPointPromotion` | float→double→long double | 已实现 | ✅ OK |
| `isStandardConversion` | 整数/浮点/指针/布尔/nullptr/数组→指针/函数→指针 | 已实现 | ✅ OK |
| `isQualificationConversion` | 指针 CVR 增加 | 已实现 | ✅ OK |
| `isDerivedToBaseConversion` | 继承链指针转换 | 已实现 | ✅ OK |
| `GetStandardConversion` | 三步标准转换序列构建 | 已实现 | ✅ OK |
| `GetConversion` | 标准转换入口 | 已实现 | ✅ OK |
| **枚举类型提升** | C++ [conv.prom] 枚举→int 提升 | ⚠️ **缺失** — 仅处理 BuiltinType，枚举类型未提升 | ❌ 问题1 |
| **nullptr→bool 转换** | nullptr_t 可隐式转为 false | ⚠️ 部分缺失 — nullptr→指针已处理，nullptr→bool 未处理 | ❌ 问题2 |

### Task 4.4.3 最佳函数选择

| 检查项 | 规划文档要求 | 实现 | 状态 |
|--------|------------|------|------|
| `resolve()` 核心算法 | 全部候选可行性→收集可行→逐对比较→检测歧义 | 已实现 | ✅ OK |
| `Sema::ResolveOverload` | 候选集+诊断(无匹配/歧义) | 已实现 | ✅ OK |
| `Sema::AddOverloadCandidate` | 包装候选添加 | 已实现 | ✅ OK |
| **`err_ovl_deleted_function`** | 规划文档定义了此诊断 | ⚠️ 未使用 — resolve() 中未检查 deleted 函数 | ❌ 问题3 |

---

## 二、Stage 4.5 语义检查

### Task 4.5.1 类型检查

| 检查项 | 规划文档要求 | 实现 | 状态 |
|--------|------------|------|------|
| `CheckAssignment` | const 保护 + 类型兼容 | 已实现 | ✅ OK |
| `CheckInitialization` | 拷贝初始化，引用分发 | 已实现 | ✅ OK |
| `CheckDirectInitialization` | 直接初始化 | 已实现 | ✅ OK |
| `CheckListInitialization` | 列表初始化 | 已实现，数组/聚合部分留 TODO | ✅ OK |
| `CheckReferenceBinding` | 非const左值引用不能绑定右值 | 已实现 | ✅ OK |
| `CheckCall` | 参数类型检查 | 已实现 | ✅ OK |
| `CheckReturn` | 返回值类型匹配 | 已实现 | ✅ OK |
| `CheckCondition` | 可转为 bool | 已实现 | ✅ OK |
| `CheckCaseExpression` | 整数常量 | 已实现 | ✅ OK |
| `isTypeCompatible` | 基于 ConversionChecker | 已实现 | ✅ OK |
| `isSameType` | 忽略 CVR | 已实现 | ✅ OK |
| `getCommonType` | 常规算术转换 | 已实现 | ✅ OK |
| `getBinaryOperatorResultType` | 委托 getCommonType | ⚠️ **简化** — 不区分运算符类型 | ❌ 问题4 |
| `getUnaryOperatorResultType` | 整数提升后返回 | ⚠️ **不完整** — 提升逻辑有 TODO 未实现 | ❌ 问题5 |
| `isComparable` | 算术/指针/枚举/bool | 已实现 | ✅ OK |
| `isCallable` | 函数/函数指针 | 已实现 | ✅ OK |

### Task 4.5.2 访问控制检查

| 检查项 | 规划文档要求 | 实现 | 状态 |
|--------|------------|------|------|
| `isAccessible` | public/protected/private 规则 | 已实现 | ✅ OK |
| `CheckMemberAccess` | 含友元检查 | ⚠️ **诊断代码被注释掉** | ❌ 问题6 |
| `CheckBaseClassAccess` | 基类访问 | ⚠️ **简化** — 始终返回 false | ❌ 问题7 |
| `CheckFriendAccess` | 友元声明遍历 | ⚠️ **不完整** — 函数名匹配逻辑为 TODO | ❌ 问题8 |
| `getEffectiveAccess` | 返回有效访问级别 | 已实现 | ✅ OK |
| `isDerivedFrom` | 继承关系 | 已实现（委托 CXXRecordDecl） | ✅ OK |

### Task 4.5.3 语义诊断系统

| 检查项 | 规划文档要求 | 实现 | 状态 |
|--------|------------|------|------|
| Name lookup errors (7个) | 全部定义 | 完全一致 | ✅ OK |
| Type checking errors (5个) | 全部定义 | ✅ + 额外 `err_non_const_lvalue_ref_binds_to_rvalue` | ✅ 超规格 |
| Overload resolution errors (5个) | 全部定义 | 完全一致 | ✅ OK |
| Access control errors (3个) | 全部定义 | 完全一致 | ✅ OK |
| Statement checking errors (4个) | 全部定义 | 完全一致 | ✅ OK |
| Warnings (4个) | 全部定义 | 完全一致 | ✅ OK |
| `DiagnosticIDs.h` include | 包含 DiagnosticSemaKinds.def | 已包含 | ✅ OK |

### Task 4.5.4 常量表达式求值

| 检查项 | 规划文档要求 | 实现 | 状态 |
|--------|------------|------|------|
| `EvalResult` | 5种结果类型 + APInt/APFloat | 已实现 | ✅ OK |
| `Evaluate` | 入口分派 | 已实现 | ✅ OK |
| `EvaluateAsBooleanCondition` | optional\<bool\> | 已实现 | ✅ OK |
| `EvaluateAsInt` | optional\<APSInt\> | 已实现 | ✅ OK |
| `EvaluateAsFloat` | optional\<APFloat\> | 已实现 | ✅ OK |
| `isConstantExpr` | 递归检查 | 已实现 | ✅ OK |
| `EvaluateCall` | constexpr 函数调用 | ⚠️ **stub** — 返回 NotConstantExpression | ❌ 问题9 |
| `EvaluateIntegerLiteral` | APInt→APSInt | 已实现 | ✅ OK |
| `EvaluateFloatingLiteral` | 直接返回 | 已实现 | ✅ OK |
| `EvaluateBooleanLiteral` | 使用 CXXBoolLiteral | 已实现 | ✅ OK |
| `EvaluateBinaryOperator` | 算术/比较/逻辑/位运算 | 已实现（含短路求值） | ✅ OK |
| `EvaluateUnaryOperator` | +/-/!/按位取反 | 已实现 | ✅ OK |
| `EvaluateConditionalOperator` | 三元运算符 | 已实现 | ✅ OK |
| `EvaluateDeclRefExpr` | 枚举常量 + const 变量 | 已实现 | ✅ OK |
| `EvaluateCastExpr` | int↔float 转换 | 已实现 | ✅ OK |
| **CharacterLiteral 分派** | `isConstantExpr` 包含但 `EvaluateExpr` 缺失 | ⚠️ **缺失** | ❌ 问题10 |

---

## 三、Sema.cpp 集成完整性

| 检查项 | 状态 | 说明 |
|--------|------|------|
| `Sema.h` include Overload.h | ✅ | 已包含 |
| `Sema.h` include Lookup.h | ✅ | 已包含 |
| `Sema.h` include Conversion.h | ✅ | 通过 Overload.h 间接包含 |
| `ResolveOverload` 实现 | ✅ | 完整含诊断 |
| `AddOverloadCandidate` 实现 | ✅ | 简洁 |
| TypeCheck 在 Sema 中集成 | ❌ | Sema 不持有 TypeCheck 实例 |
| AccessControl 在 Sema 中集成 | ❌ | Sema 不持有/调用 AccessControl |
| ConstantExprEvaluator 在 Sema 中集成 | ❌ | Sema 不持有/调用 ConstantExprEvaluator |

### ❌ 问题11: Sema 未集成 TypeCheck/AccessControl/ConstantExpr (严重度: 高)

**Clang 参考:** Clang 的 `Sema` 类是语义分析的统一调度中心，持有所有检查器实例，所有 `ActOn*` 方法内部调用相应检查器。

**现状:** `TypeCheck`、`AccessControl`、`ConstantExprEvaluator` 是独立类。`Sema` 的 stub 方法（`ActOnCallExpr`、`ActOnVarDecl`、`ActOnReturnStmt` 等）未调用它们。这些检查器是"悬空"的——可以创建实例但不会被 Sema 自动使用。

---

## 四、问题详情与 Clang 对比

### 问题1: 枚举类型整型提升缺失 (严重度: 中)
- **位置:** `Conversion.cpp` `isIntegralPromotion()`
- **Clang 参考:** `clang::Sema::IsIntegralPromotion()` — 枚举类型（scoped/unscoped）会提升到 int/unsigned int。
- **现状:** 只处理 `BuiltinType`，未处理 `EnumType`。当枚举值参与重载决议时，不会正确识别为提升（识别为转换），导致 `void f(int)` vs `void f(long)` 可能选错。

### 问题2: nullptr→bool 转换缺失 (严重度: 低)
- **位置:** `Conversion.cpp` `GetStandardConversion()`
- **现状:** `NullPtr→指针` 已处理（第8步），但 `NullPtr→bool` 未处理。Clang 中 `nullptr` 可隐式转为 `false`。

### 问题3: deleted 函数未检查 (严重度: 中)
- **位置:** `Overload.cpp` `resolve()` + `Sema.cpp` `ResolveOverload()`
- **Clang 参考:** Clang 在可行性检查阶段标记 `isDeleted()`，若最佳候选是 deleted 则报告 `err_ovl_deleted_function`。
- **现状:** `CXXMethodDecl` 有 `isDeleted()` 方法但 `resolve()` 未检查。`err_ovl_deleted_function` 诊断 ID 已定义但从未使用。

### 问题4: getBinaryOperatorResultType 过于简化 (严重度: 中)
- **位置:** `TypeCheck.cpp` 第371-379行
- **Clang 参考:** Clang 对不同运算符返回不同类型：
  - 算术/位运算 → `getCommonType()`
  - 比较运算符 → `bool`
  - 逻辑运算符 → `bool`
  - 赋值运算符 → LHS 类型（LValue）
  - 逗号 → RHS 类型
- **现状:** 所有运算符统一委托 `getCommonType()`，不区分运算符类型。需接受 `BinaryOpKind` 参数。

### 问题5: getUnaryOperatorResultType 整数提升未完成 (严重度: 低)
- **位置:** `TypeCheck.cpp` 第381-402行
- **现状:** 检测到需要提升（rank < 4）但没有 `ASTContext` 来获取 int 类型，直接返回原类型。需要传入或持有 `ASTContext` 引用。

### 问题6: AccessControl 诊断被注释 (严重度: 中)
- **位置:** `AccessControl.cpp` `CheckMemberAccess()` 第93-102行
- **现状:** `err_access_private` 和 `err_access_protected` 诊断代码被注释掉。AccessControl 全是 static 方法，需要改为接受 `DiagnosticsEngine&` 参数或在 Sema 层调用诊断。

### 问题7: CheckBaseClassAccess 简化 (严重度: 低)
- **位置:** `AccessControl.cpp` `CheckBaseClassAccess()`
- **现状:** protected/private base 始终返回 false，未检查 AccessingContext。需要传入 AccessingContext 参数。

### 问题8: CheckFriendAccess 不完整 (严重度: 低)
- **位置:** `AccessControl.cpp` `CheckFriendAccess()`
- **现状:** 由于 `DeclContext` 和 `Decl` 无继承关系，无法从 `AccessingContext` 获取函数声明进行匹配。需要设计合适的参数接口。

### 问题9: EvaluateCall 是 stub (严重度: 中)
- **位置:** `ConstantExpr.cpp` `EvaluateCall()`
- **现状:** constexpr 函数调用求值返回 NotConstantExpression。`FunctionDecl::isConstexpr()` 检查存在但求值逻辑未实现。Clang 的 `EvaluateCall` 会展开 constexpr 函数体。

### 问题10: CharacterLiteral 未在 EvaluateExpr 分派 (严重度: 低)
- **位置:** `ConstantExpr.cpp` `EvaluateExpr()`
- **现状:** `isConstantExpr()` 包含 `CharacterLiteral`，但 `EvaluateExpr()` 的分派逻辑中缺少对 `CharacterLiteral` 的处理，会导致运行时返回 NotConstantExpression。

### 问题11: Sema 未集成 TypeCheck/AccessControl/ConstantExpr (严重度: 高)
- **位置:** `Sema.h` / `Sema.cpp`
- **Clang 参考:** Clang 的 `Sema` 是统一的语义分析调度中心。
- **现状:** `ActOnCallExpr`、`ActOnVarDecl`、`ActOnReturnStmt` 等全部为 stub（返回 invalid），未调用 TypeCheck/AccessControl/ConstantExprEvaluator。

---

## 五、总结评分

| 模块 | 完成度 | 说明 |
|------|--------|------|
| Task 4.4.1 重载候选集 | **95%** | 完整，仅 FunctionTemplate 实例化留 TODO |
| Task 4.4.2 隐式转换与排序 | **85%** | 核心完整，缺枚举提升、nullptr→bool |
| Task 4.4.3 最佳函数选择 | **90%** | 核心完整，缺 deleted 函数检查 |
| Task 4.5.1 类型检查 | **80%** | 大部分完整，getUnaryOperator/getBinaryOperator 简化 |
| Task 4.5.2 访问控制 | **65%** | 核心逻辑在，但诊断被注释、friend/base 简化 |
| Task 4.5.3 语义诊断 | **100%** | 完整且超规格（多1个诊断 ID） |
| Task 4.5.4 常量表达式 | **75%** | 基础求值完整，缺 constexpr 函数求值、CharacterLiteral 分派 |
| Sema 集成 | **50%** | 重载已集成，TypeCheck/AccessControl/ConstantExpr 未接入 |

---

## 六、优先修复建议（按严重度排序）

| 优先级 | 问题 | 严重度 | 修复建议 |
|--------|------|--------|----------|
| P0 | 问题11: Sema 未集成检查器 | 高 | 在 Sema 中添加 TypeCheck/AccessControl/ConstantExprEvaluator 成员，在 ActOn* 方法中调用 |
| P1 | 问题1: 枚举类型整型提升 | 中 | 在 isIntegralPromotion 中添加 EnumType 处理 |
| P1 | 问题3: deleted 函数未检查 | 中 | 在 checkViability 中检查 isDeleted() |
| P1 | 问题4: 运算符结果类型简化 | 中 | getBinaryOperatorResultType 接受 BinaryOpKind 参数 |
| P1 | 问题6: AccessControl 诊断注释 | 中 | 取消注释或改为在 Sema 层报告 |
| P1 | 问题9: constexpr 函数求值 stub | 中 | 实现 EvaluateCall 的基本逻辑 |
| P2 | 问题2: nullptr→bool | 低 | 在 GetStandardConversion 中添加 |
| P2 | 问题5: unary 结果类型提升 | 低 | 需引入 ASTContext 获取 int 类型 |
| P2 | 问题7: BaseClassAccess 简化 | 低 | 传入 AccessingContext |
| P2 | 问题8: FriendAccess 不完整 | 低 | 设计合适的参数接口 |
| P2 | 问题10: CharacterLiteral 分派 | 低 | 在 EvaluateExpr 添加分派 |
