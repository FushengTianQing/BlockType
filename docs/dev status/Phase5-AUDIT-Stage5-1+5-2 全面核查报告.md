Phase5-AUDIT-Stage5.1+5.2 全面核查报告
对照开发文档 docs/plan/05-PHASE5-template-generics.md 逐项核查，并结合 Clang 同模块特性对比。

一、Stage 5.1 核查
Task 5.1.1 — 模板 Sema ActOn* 方法 ✅ 基本完成，有不足
开发要点	状态	说明
E5.1.1.1 Sema.h 声明 8 个 ActOn* 方法	✅	全部声明：ActOnClassTemplateDecl, ActOnFunctionTemplateDecl, ActOnVarTemplateDecl, ActOnTypeAliasTemplateDecl, ActOnConceptDecl, ActOnTemplateId, ActOnExplicitSpecialization, ActOnExplicitInstantiation
E5.1.1.2 SemaTemplate.cpp 实现	⚠️	实现存在但验证逻辑偏薄
问题：

ActOnExplicitSpecialization / ActOnExplicitInstantiation 为纯占位（返回 getInvalid()）— 开发文档标注"Stage 5.5 完整实现"，所以这是合理的延迟。但 ActOnExplicitInstantiation 应至少触发实例化（目前直接返回 D），应在 5.5 补充。
参数验证不完整：开发文档要求"验证模板参数列表"，当前实现只检查 Params 是否为空，未检查：
参数是否有重复名称
NonTypeTemplateParmDecl 的类型是否完整
TemplateTemplateParmDecl 的嵌套参数是否有效
缺少 CurContext->addDecl() 调用：ActOnClassTemplateDecl 等方法将声明注册到 Symbols 但没有调用 CurContext->addDecl()，这与其他 ActOn* 方法（如 ActOnVarDecl）不一致。在 Clang 中，模板声明需要注册到当前 DeclContext。
Task 5.1.2 — 模板实例化框架 ✅ 完成较好
开发要点	状态	说明
E5.1.2.1 TemplateInstantiation.h	✅	完整定义 TemplateArgumentList + TemplateInstantiator，含 SFINAE 集成
E5.1.2.2 TemplateInstantiation.cpp	✅	实现了完整的实例化流程
Clang 对比问题：

InstantiateClassTemplate 缺少递归深度溢出时的诊断：当 CurrentDepth >= MaxInstantiationDepth 时仅返回 nullptr，应通过 Sema 的 DiagnosticsEngine 报告 err_template_recursion 错误。
SubstituteExpr 是空实现（直接返回 E）：开发文档说明"依赖表达式的完整递归替换将在后续阶段展开"，这可以接受，但在当前阶段至少应处理 DeclRefExpr（替换引用的模板参数声明）和 BinaryOperator 等简单情况。Stage 5.3 的变参模板需要此功能。
FindExistingSpecialization 使用线性扫描：Clang 使用 llvm::FoldingSet 做特化缓存查找（O(1)），当前实现对每个特化线性遍历。功能正确但在大量特化时性能差。
SubstituteType 未处理 QualifiedType / AttributedType：Clang 的 TreeTransform 处理了这两种类型。当前实现如果遇到这些类型会直接返回原值，可能导致 cv 限定符丢失。
Task 5.1.3 — 模板名称查找与 Sema 集成 ⚠️ 部分缺失
开发要点	状态	说明
E5.1.3.1 Sema.h 添加 Instantiator 成员	✅	已有 std::unique_ptr<TemplateInstantiator> Instantiator
E5.1.3.2 扩展名称查找处理模板名	⚠️	ActOnTemplateId 中做了模板查找，但 LookupUnqualifiedName 中未修改
E5.1.3.3 依赖类型查找	❌	未实现
问题：

LookupUnqualifiedName 未处理模板名：开发文档要求"当 Name 匹配到 ClassTemplateDecl/FunctionTemplateDecl 时，需要正确返回"。当前 Lookup.cpp 中的 LookupUnqualifiedName 没有调用 lookupTemplate。这意味着模板名在非模板上下文中的查找可能失败。
依赖类型查找未实现：开发文档要求检查 isDependentType() 区分依赖/非依赖名称。当前 ActOnTemplateId 对所有情况都立即尝试实例化，无法处理 T::type 这种依赖名称。
Task 5.1.4 — 模板相关诊断 ID ✅ 完成
开发要点	状态
13 个诊断 ID	✅
已定义但部分未使用（如 err_template_recursion 在实例化深度溢出时未发出）。

二、Stage 5.2 核查
Task 5.2.1 — 类型推导引擎 ✅ 完成较好
开发要点	状态	说明
E5.2.1.1 TemplateDeduction.h	✅	完整定义 TemplateDeductionResult, TemplateDeductionInfo, TemplateDeduction
E5.2.1.2 TemplateDeduction.cpp	✅	实现了推导算法
Clang 对比问题：

DeduceFunctionTemplateArguments 缺少对显式指定模板实参的处理：Clang 的 Sema::DeduceTemplateArguments 支持部分实参由调用者显式指定（如 f<int>(1.0)），剩余实参通过推导。当前实现假设所有实参都通过推导获取。
DeduceTemplateArguments 未处理 const T / volatile T 的 cv 限定符剥离：C++ [temp.deduct.call] 规定推导时需要剥离实参类型的顶层 cv 限定符。例如，template<typename T> void f(T) 调用 f(const int&) 应推导 T = int（不是 const int）。当前实现直接用 ArgType，未剥离 cv。
collapseReferences 实现不完整：T&& & → T& 的情况直接返回 Inner（即 T&&），实际应创建 LValueReferenceType。注释中已承认"actual implementation needs Context"。这需要 ASTContext 来创建正确的引用类型。
DeduceFromReferenceType 缺少非引用实参到 T& 参数的处理：C++ [temp.deduct.call] 规定 T& 参数可以从非引用左值实参推导（去掉引用后匹配）。当前实现要求双方都是 ReferenceType 才匹配。
缺少对 TemplateTemplateParmDecl 的推导：开发文档中 TemplateDeduction 的 private 方法列表没有 DeduceFromTemplateTemplateParm，但 Clang 支持模板模板参数的推导。
Task 5.2.2 — SFINAE 实现 ✅ 完成
开发要点	状态	说明
E5.2.2.1 SFINAE.h	✅	SFINAEContext + SFINAEGuard（RAII）
E5.2.2.2 集成到 TemplateInstantiator	⚠️	成员已添加，但未在替换失败时实际使用
关键问题：

SFINAE 集成只是框架，未实际生效：TemplateInstantiator 添加了 SFINAEContext 成员和 isSFINAEContext() 方法，但 SubstituteType 等替换方法中没有任何地方检查 isSFINAEContext()。在 Clang 中，当处于 SFINAE 上下文时：

替换失败返回 QualType() 而非报错
DiagnosticsEngine 的错误被抑制（DiagnosticsEngine::Suppress 或 SFINAETrap）
当前实现：替换失败时总是返回空 QualType，无论是否处于 SFINAE 上下文。在非 SFINAE 上下文中应该报告诊断错误。

Task 5.2.3 — 部分排序 ⚠️ 实现偏差
开发要点	状态	说明
isMoreSpecialized 实现	⚠️	使用评分法，非标准算法
问题：

部分排序使用评分法而非标准双向推导法：C++ [temp.deduct.partial] 的算法是"为 P1 生成虚拟类型 → 尝试推导 P2"双向验证。当前使用 computeSpecificity 评分法，在某些边界情况下会给出错误结果。例如：
template<typename T> void f(T, T) vs template<typename T> void f(T*, T*) — 评分法可能正确
template<typename T> void f(T, int) vs template<typename T> void f(T, T) — 评分法可能误判（前者第二个参数是具体类型 int，得分 3；后者是 T，得分 0。但按 C++ 规则，这两个无法比较谁更特化）
generateDeducedType 是空实现：如果将来要实现标准算法，此方法是核心——它需要通过 ASTContext 创建唯一的虚拟类型。
三、跨模块关联性问题
ResolveOverload 未集成模板推导：Sema::ResolveOverload（Sema.cpp:624）和 ActOnCallExpr（Sema.cpp:215）中没有调用 TemplateDeduction。当调用 f(42) 且 f 是函数模板时，应该触发推导。当前 ActOnCallExpr 只处理非模板函数。
isCompleteType 未考虑已实例化的模板特化：Sema::isCompleteType 对 TemplateSpecialization 类型返回 false（Sema.cpp:596），但如果特化已被实例化（有完整的成员列表），应该返回 true。需要检查 ClassTemplateSpecializationDecl::isCompleteDefinition()。
SubstituteCXXMethodDecl 的 Parent 指针：方法实例化时 Parent 仍指向原始 CXXRecordDecl（模板类的模式），而非实例化后的特化声明。在 Clang 中，方法的 Parent 被更新为实例化后的 ClassTemplateSpecializationDecl。
四、核查结果汇总
类别	严重	中等	轻微
功能缺失	#15(SFINAE未生效), #18(推导未集成ResolveOverload)	#8(模板名查找), #9(依赖类型), #19(isCompleteType)	#10(显式模板实参), #14(模板模板参数推导)
逻辑缺陷	#11(cv剥离), #20(Parent指针)	#12(collapseReferences不完整), #13(非引用到T&)	#5(SubstituteExpr空实现)
规范偏差	#16(部分排序算法)	#4(深度溢出无诊断)	#7(QualifiedType)
一致性	#3(CurContext缺失)	#6(线性扫描性能)	#17(generateDeducedType空)
建议优先修复（阻塞后续开发）：

#15 SFINAE 未实际生效 — Stage 5.4 Concepts 强依赖此功能
#18 推导未集成 ResolveOverload — Stage 5.3 变参模板和整体功能都需要
#20 实例化方法的 Parent 指针 — 导致成员函数无法正确访问实例化后的类成员
#4 深度溢出应报告诊断 — 影响用户体验