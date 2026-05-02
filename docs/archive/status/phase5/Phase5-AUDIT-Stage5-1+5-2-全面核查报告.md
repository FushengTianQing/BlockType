Phase5-AUDIT-Stage5.1+5.2 全面核查报告
对照开发文档 docs/plan/05-PHASE5-template-generics.md 逐项核查，并结合 Clang 同模块特性对比。
最后更新：2026-04-18（提交 b536aaf 后刷新）

---

一、Stage 5.1 核查

Task 5.1.1 — 模板 Sema ActOn* 方法

E5.1.1.2 SemaTemplate.cpp 实现	✅	验证逻辑完整

问题：

1. ActOnExplicitSpecialization / ActOnExplicitInstantiation 为纯占位（返回 getInvalid()）— 开发文档标注"Stage 5.5 完整实现"，所以这是合理的延迟。但 ActOnExplicitInstantiation 应至少触发实例化（目前直接返回 D），应在 5.5 补充。
   → 状态：✅ 已修复（Stage 5.5）— ActOnExplicitSpecialization 返回有效空结果（template<> 解析成功）；ActOnExplicitInstantiation 支持 ClassTemplateSpecializationDecl 触发实际实例化（通过 Instantiator->InstantiateClassTemplate），支持 FunctionDecl 显式实例化注册

2. 参数验证不完整：开发文档要求"验证模板参数列表"，当前实现只检查 Params 是否为空，未检查：
   - 参数是否有重复名称
   - NonTypeTemplateParmDecl 的类型是否完整
   - TemplateTemplateParmDecl 的嵌套参数是否有效
   → 状态：✅ 已修复（Stage 5.5）— 新增 ValidateTemplateParameterList 共享验证函数，在所有 5 个 ActOn*TemplateDecl + 2 个偏特化方法中调用。检查：重复名称（err_template_param_duplicate_name）、非类型参数类型完整性（err_template_param_incomplete_type）、模板模板参数嵌套列表有效性（err_template_template_param_nested_invalid）

3. 缺少 CurContext->addDecl() 调用：ActOnClassTemplateDecl 等方法将声明注册到 Symbols 但没有调用 CurContext->addDecl()，这与其他 ActOn* 方法（如 ActOnVarDecl）不一致。
   → 状态：✅ 已修复（b536aaf）— 所有 5 个 ActOn* 方法已添加 CurContext->addDecl()


Task 5.1.2 — 模板实例化框架 ✅ 完成较好

E5.1.2.1 TemplateInstantiation.h
TemplateArgumentList + TemplateInstantiator，含 SFINAE 集成

E5.1.2.2 TemplateInstantiation.cpp	实现了完整的实例化流程

Clang 对比问题：

1. InstantiateClassTemplate 缺少递归深度溢出时的诊断：当 CurrentDepth >= MaxInstantiationDepth 时仅返回 nullptr，应通过 Sema 的 DiagnosticsEngine 报告 err_template_recursion 错误。
   → 状态：✅ 已修复（b536aaf）— InstantiateClassTemplate 和 InstantiateFunctionTemplate 均在非 SFINAE 上下文中报告 err_template_recursion

2. SubstituteExpr 是空实现（直接返回 E）：开发文档说明"依赖表达式的完整递归替换将在后续阶段展开"，这可以接受，但在当前阶段至少应处理 DeclRefExpr（替换引用的模板参数声明）和 BinaryOperator 等简单情况。Stage 5.3 的变参模板需要此功能。
   → 状态：✅ 已修复（Stage 5.3）— SubstituteExpr 已实现 DeclRefExpr、BinaryOperator、UnaryOperator、CallExpr 的递归替换

3. FindExistingSpecialization 使用线性扫描：Clang 使用 llvm::FoldingSet 做特化缓存查找（O(1)），当前实现对每个特化线性遍历。功能正确但在大量特化时性能差。
   → 状态：⏳ 性能优化项，可在后续阶段改为 FoldingSet

4. SubstituteType 未处理 QualifiedType / AttributedType：Clang 的 TreeTransform 处理了这两种类型。当前实现如果遇到这些类型会直接返回原值，可能导致 cv 限定符丢失。
   → 状态：✅ 已修复 — SubstituteType 的 TemplateTypeParmType 和 DependentType 分支现在合并原始 CVR 限定符到替换结果（如 `const T` 替换为 `int` → `const int`）



Task 5.1.3 — 模板名称查找与 Sema 集成 ⚠️ 部分缺失

E5.1.3.2 扩展名称查找处理模板名	✅ 已修复

E5.1.3.3 依赖类型查找	✅	完整实现（Layer 1-4）

实施方案见 `docs/dev status/IMPL-PLAN-dependent-lookup.md`，分 4 层实现：

问题：

1. LookupUnqualifiedName 未处理模板名：开发文档要求"当 Name 匹配到 ClassTemplateDecl/FunctionTemplateDecl 时，需要正确返回"。
   → 状态：✅ 已修复（b536aaf）— LookupUnqualifiedName 全局回退中添加了 lookupTemplate/lookupConcept 查找；LookupTypeName 中 ClassTemplateDecl 被识别为有效类型名

2. 依赖类型查找未实现：开发文档要求检查 isDependentType() 区分依赖/非依赖名称。
   → 状态：✅ 已修复 — 完整 4 层实现：
   - **Layer 1** (fdba823): Parser 在解析 `template<typename T>` 后 `pushScope(TemplateScope)`，将模板参数加入 Scope，使 `lookupInScope("T")` 可见
   - **Layer 2** (53d7b6f): `LookupResult` 新增 `isDependent` 标志；`LookupUnqualifiedName` 检测 scope 链中的 `TemplateScope`，空查找时标记为依赖
   - **Layer 3** (6904d76): `SubstituteDependentType` 替换 BaseType 后，如果 SubBase 为非依赖 RecordType，通过 `LookupQualifiedName` 真正解析 `T::name` 成员
   - **Layer 4**: 新增 `CXXDependentScopeMemberExpr` + `DependentScopeDeclRefExpr` AST 节点；`Sema::isDependentContext()` 检测模板上下文；`SubstituteExpr` 处理新节点并在实例化时解析为具体 `MemberExpr`/`DeclRefExpr`

结论
层面	当前能力	状态
LookupQualifiedName	✅ isDependentType() 检测依赖 NNS	已完成
LookupUnqualifiedName 依赖检测	✅ TemplateScope 检测 + Dependent 标记	已完成
SubstituteDependentType	✅ 真正解析 T::name 成员	已完成
完整两阶段查找	✅ 新 AST 节点 + 实例化时解析	已完成


Task 5.1.4 — 模板相关诊断 ID

13 个诊断 ID	✅
已定义但部分未使用（如 err_template_recursion 在实例化深度溢出时未发出）。
→ 状态：✅ 已修复（b536aaf）— err_template_recursion 已在两处深度溢出检查点发出诊断



---

二、Stage 5.2 核查

Task 5.2.1 — 类型推导引擎 ✅ 完成较好

E5.2.1.1 TemplateDeduction.h	✅	完整定义 TemplateDeductionResult, TemplateDeductionInfo, TemplateDeduction
E5.2.1.2 TemplateDeduction.cpp	✅	实现了推导算法

Clang 对比问题：

1. DeduceFunctionTemplateArguments 缺少对显式指定模板实参的处理：Clang 的 Sema::DeduceTemplateArguments 支持部分实参由调用者显式指定（如 f<int>(1.0)），剩余实参通过推导。当前实现假设所有实参都通过推导获取。
   → 状态：✅ 已修复 — DeduceFunctionTemplateArguments 新增 ExplicitArgs 参数，在推导前将显式实参种子到 TemplateDeductionInfo

2. DeduceTemplateArguments 未处理 const T / volatile T 的 cv 限定符剥离：C++ [temp.deduct.call] 规定推导时需要剥离实参类型的顶层 cv 限定符。
   → 状态：✅ 已修复（b536aaf）— DeduceFunctionTemplateArguments 中对非引用参数剥离顶层 cv 限定符和引用

3. collapseReferences 实现不完整：T&& & → T& 的情况直接返回 Inner（即 T&&），实际应创建 LValueReferenceType。
   → 状态：✅ 已修复（b536aaf）— collapseReferences 现在通过 SemaRef.getASTContext() 获取 ASTContext 创建正确的 LValueReferenceType

4. DeduceFromReferenceType 缺少非引用实参到 T& 参数的处理：C++ [temp.deduct.call] 规定 T& 参数可以从非引用左值实参推导。
   → 状态：✅ 已修复（b536aaf）— 主推导函数中 ReferenceType 分支新增了 lvalue reference 参数对非引用实参的推导处理

5. 缺少对 TemplateTemplateParmDecl 的推导：Clang 支持模板模板参数的推导。
   → 状态：✅ 已修复 — DeduceFromTemplateSpecializationType 中检测 TemplateTemplateParmDecl，将参数模板的 TemplateDecl 作为推导结果，并递归推导内层模板实参


Task 5.2.2 — SFINAE 实现

E5.2.2.1 SFINAE.h	✅	SFINAEContext + SFINAEGuard（RAII）
E5.2.2.2 集成到 TemplateInstantiator	✅ 已修复

关键问题：

SFINAE 集成只是框架，未实际生效：
→ 状态：✅ 已修复（Stage 5.4/5.5）— 完整集成：1) DiagnosticsEngine 新增 pushSuppression/popSuppression 抑制栈，report() 在 SuppressCount>0 时静默丢弃诊断；2) SFINAEGuard 构造时自动 pushSuppression，析构时自动 popSuppression；3) DeduceAndInstantiateFunctionTemplate 在推导阶段进入 SFINAE 抑制上下文；4) ConstraintSatisfaction::CheckConstraintSatisfaction / CheckConceptSatisfaction 在约束替换评估中进入 SFINAE 抑制上下文。TemplateInstantiator 深度溢出时也区分 SFINAE/非 SFINAE。


Task 5.2.3 — 部分排序 ⚠️ 实现偏差

isMoreSpecialized 实现	⚠️	使用评分法，非标准算法

问题：

1. 部分排序使用评分法而非标准双向推导法：C++ [temp.deduct.partial] 的算法是"为 P1 生成虚拟类型 → 尝试推导 P2"双向验证。
   → 状态：✅ 已修复 — isMoreSpecialized 已重写为标准双向推导法：Direction1 用 P1 的合成类型推导 P2，Direction2 反向验证。generateDeducedType 和 transformForPartialOrdering 已完整实现

2. generateDeducedType 是空实现。
   → 状态：✅ 已修复 — generateDeducedType 为 TemplateTypeParmDecl 创建唯一合成 TemplateTypeParmType；transformForPartialOrdering 递归处理 PointerType/ReferenceType


---

三、跨模块关联性问题

1. ResolveOverload 未集成模板推导：ActOnCallExpr 中没有调用 TemplateDeduction。
   → 状态：✅ 已修复（b536aaf）— ActOnCallExpr 现在检测 FunctionTemplateDecl callee，通过新增的 DeduceAndInstantiateFunctionTemplate 方法自动推导并实例化

2. isCompleteType 未考虑已实例化的模板特化：Sema::isCompleteType 对 TemplateSpecialization 类型返回 false。
   → 状态：✅ 已修复（b536aaf）— isCompleteType 中 TemplateSpecialization 类型通过 Instantiator->FindExistingSpecialization 检查特化是否已实例化且定义完整

3. SubstituteCXXMethodDecl 的 Parent 指针：方法实例化时 Parent 仍指向原始 CXXRecordDecl。
   → 状态：✅ 已修复（b536aaf）— SubstituteCXXMethodDecl 新增 Parent 参数，InstantiateClassTemplate 传入实例化后的 Spec 作为 MethodParent


---

四、修复状态汇总

| # | 问题 | 状态 | 修复提交 |
|---|---|---|---|
| #3 | CurContext->addDecl() 缺失 | ✅ 已修复 | b536aaf |
| #4 | 深度溢出无诊断 | ✅ 已修复 | b536aaf |
| #5 | SubstituteExpr 空实现 | ✅ 已修复 | Stage 5.3 |
| #6 | FindExistingSpecialization 线性扫描 | ⏳ 性能优化 | — |
| #7 | QualifiedType/cv-qualifier 保留 | ✅ 已修复 | Stage 5.5 |
| #8 | LookupUnqualifiedName 模板名查找 | ✅ 已修复 | b536aaf |
| #9 | 依赖类型查找 | ✅ 已修复 | Stage 5.5 Layer 1-4 |
| #10 | 显式指定模板实参 | ✅ 已修复 | Stage 5.5 |
| #11 | CV 限定符剥离 | ✅ 已修复 | b536aaf |
| #12 | collapseReferences 不完整 | ✅ 已修复 | b536aaf |
| #13 | 非引用到 T& 参数处理 | ✅ 已修复 | b536aaf |
| #14 | 模板模板参数推导 | ✅ 已修复 | Stage 5.5 |
| #15 | SFINAE 未实际生效 | ✅ 已修复 | Stage 5.4/5.5 |
| #16 | 部分排序算法偏差 | ✅ 已修复 | Stage 5.5 |
| #17 | generateDeducedType 空 | ✅ 已修复 | Stage 5.5 |
| #18 | 推导未集成 ResolveOverload | ✅ 已修复 | b536aaf |
| #19 | isCompleteType 模板特化 | ✅ 已修复 | b536aaf |
| #20 | Parent 指针错误 | ✅ 已修复 | b536aaf |

统计：17/18 已修复，1/18 延迟（#6 FindExistingSpecialization 线性扫描为性能优化项）
