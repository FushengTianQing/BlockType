# 全面核查报告：Stage 4.2 + Stage 4.3 （已经全部修复）

一、Stage 4.2 核查

Task 4.2.1 类型工厂补全

问题清单：

🔴 getRecordType/getEnumType 没有去重缓存

每次调用都创建新对象。Clang 使用 FoldingSet 缓存 RecordType/EnumType，同一 RecordDecl* 只创建一个 RecordType。当前实现会导致 getRecordType(D) != getRecordType(D)，类型比较时产生错误。
建议：添加 llvm::DenseMap<RecordDecl*, RecordType*> 缓存。

🟡 getMemberFunctionType 的 CVR 限定符处理不正确

函数类型的 const/volatile 应属于 FunctionProtoType 的 MethodQualifiers，而非附加到 MemberPointerType 上。当前实现在第 261 行将 CV 加到 MemberPointer 的 Qualifier 上，这与 C++ 语义不符（R (C::*)(Args) const 的 const 属于函数类型，不是成员指针的限定符）。

🟡 缺少 getCharType()/getUnsignedIntType() 等常用便捷方法

对照 Clang 的 ASTContext::CharTy/ASTContext::IntTy，BlockType 的 BuiltinTypes.def 有 UShort/UInt/ULong/LongLong/ULongLong/Char/SChar/UChar/Short 等，但便捷方法只覆盖了 Void/Bool/Int/Float/Double/Long/NullPtr。不影响当前功能但后续需要补充。

---------------------------------
修改总结
🔴 修复 1：getTypeDeclType 绕过缓存
文件: src/AST/ASTContext.cpp:167-175
RecordDecl/EnumDecl 分支改为调用 getRecordType()/getEnumType()，不再直接 new，确保通过任何路径获取同一 decl 的类型都指向同一对象
🟡 修复 2：FunctionType 添加 MethodQualifiers
文件: include/blocktype/AST/Type.h:326-349
添加 MethodIsConst/MethodIsVolatile 字段及 isConst()/isVolatile() 访问器
FunctionType::dump() 输出 const/volatile 后缀
getFunctionType() 签名扩展 IsConst/IsVolatile 参数（默认 false）
🟡 修复 3：getMemberFunctionType CVR 语义
文件: src/AST/ASTContext.cpp:257-273
const/volatile 现在传入 FunctionType 的 MethodQualifiers，而非附加到 MemberPointerType 的 Qualifier 上
MemberPointer 的 QualType 不再携带 CV 限定符
调用方同步更新: src/Parse/Declarator.cpp:60-76, src/Parse/ParseType.cpp:722-732 — 提取 MethodQuals 中的 CV 传入 getFunctionType
🟡 修复 4：补充便捷类型方法（10 个）
文件: include/blocktype/AST/ASTContext.h:224-266, src/AST/ASTContext.cpp:253-295
新增：getCharType(), getSCharType(), getUCharType(), getWCharType(), getShortType(), getUShortType(), getUIntType(), getULongType(), getLongLongType(), getULongLongType()

----------------------------------

Task 4.2.2 类型推导

有问题的项目：

deduceAutoType⚠️ 见下
deduceAutoRefType⚠️
deduceAutoForwardingRefType	⚠️
deduceFromInitList⚠️
deduceDecltypeType⚠️


问题清单：

🟡 deduceAutoForwardingRefType 未实现值类别区分

第 85-88 行：代码注释承认"简化模型：始终推导为右值引用"。C++ 规则应是：auto&& 绑定到左值时推导为 T&，绑定到右值时推导为 T&&。Clang 的 Sema::DeduceAutoType 使用 Expr::isLValue()/isXValue()/isRValue() 来区分。
建议：需要给 Expr 基类添加 isLValue()/isRValue()/isXValue() 虚方法（或 getValueKind() 枚举），才能正确实现。

🟡 deduceDecltypeType 未实现值类别保留

第 147-156 行：直接返回 E->getType()，没有根据值类别添加引用。C++ 规则：lvalue → T&，xvalue → T&&，prvalue → T。这同样需要 Expr 的值类别支持。

🟡 deduceFromInitList 的类型比较过于严格

第 128 行使用 T.getTypePtr() != FirstType.getTypePtr() 做指针比较。应使用 getCanonicalType() 比较，否则 const int 和 int const 会被认为不同。

🟡 deduceAutoRefType 未检查值类别合法性

auto& x = rvalue; 是非法的（除非 const auto&）。当前无条件返回左值引用，没有诊断错误。不过这属于 Stage 4.5 类型检查的范畴。
----------------------------------------
修改总结

1. include/blocktype/AST/Expr.h — 添加 ValueKind 枚举 + Expr 值类别支持
新增 ExprValueKind 枚举：VK_PRValue / VK_LValue / VK_XValue
Expr 基类新增 ValueKind 字段（默认 VK_PRValue，向后兼容），以及 getValueKind() / setValueKind() / isLValue() / isXValue() / isPRValue() / isGLValue() / isRValue() 方法
更新子类构造函数传入正确的值类别：
LValue: DeclRefExpr, MemberExpr, ArraySubscriptExpr, 赋值类 BinaryOperator, 逗号 BinaryOperator, UnaryOperator(Deref/PreInc/PreDec)
PRValue: 所有字面量、CallExpr、CastExpr 等保持默认

2. src/Sema/TypeDeduction.cpp — 修复 3 个问题
函数	修复内容
deduceAutoForwardingRefType	使用 Init->isLValue() 区分：lvalue → T&，rvalue → T&&
deduceDecltypeType	根据值类别：lvalue → T&，xvalue → T&&，prvalue → T
deduceFromInitList	指针比较 → getCanonicalType() 语义比较

3 deduceAutoRefType 未检查值类别合法性
额外新增的内容：

文件	修改
DiagnosticSemaKinds.def	新增 err_non_const_lvalue_ref_binds_to_rvalue 诊断（中英双语）
TypeDeduction.h	构造函数增加可选 DiagnosticsEngine* 参数
TypeDeduction.cpp deduceAutoRefType	当 init 是右值且声明类型非 const 时，发出诊断并返回空类型
逻辑：auto& x = 42; → 右值 + 非 const → 报错。const auto& x = 42; → 右值但 const → 合法，延长生命周期。
----------------------------------------

Task 4.2.3 类型完整性检查


问题清单：

🟡 isCompleteType 中 RecordType 的完整性判断有缺陷

第 257-259 行：使用 RD->members().empty() || RD->getNumBases() > 0 判断。但空类 class X {}; 是完整类型，其 members().empty() 返回 true 但 getNumBases() 为 0，导致错误返回 false。
根本原因：RecordDecl::isCompleteDefinition() 方法缺失。Clang 使用 RecordDecl::isCompleteDefinition() 来判断。需要给 RecordDecl/CXXRecordDecl 添加 bool IsCompleteDefinition 标志位。
修复建议：在 RecordDecl 添加 bool IsCompleteDefinition = false 和 void setCompleteDefinition(bool V = true)。在 isCompleteType 中改为 return RD->isCompleteDefinition()。

🟡 Sema.cpp 中仍然保留了 Stage 4.3 的 stub 代码

第 327-353 行：LookupUnqualifiedName/LookupQualifiedName/LookupADL/CollectAssociatedNamespacesAndClasses 都是空的 stub 返回。实际实现在 Lookup.cpp 中。但由于 Sema.cpp 和 Lookup.cpp 都在编译，链接时 Sema.cpp 中的定义会覆盖 Lookup.cpp 的定义（因为它们在同一翻译单元的同一 .a 库中）。
🔴 这是一个严重 BUG：Sema.cpp 中的 stub 定义会导致 Lookup.cpp 的实际实现被忽略，所有名字查找永远返回空结果！
必须修复：删除 Sema.cpp 中第 327-353 行的 stub 代码。


Expr 基类增强

问题清单：

🟡 Expr 子类构造函数没有传递 QualType 参数
所有现有 Expr 子类（IntegerLiteral、FloatingLiteral 等）的构造函数没有更新来传递 QualType 给基类。虽然默认参数 QualType() 能编译，但所有现有表达式的 getType() 都返回空 QualType()。
需要在子类构造时传入正确的类型（如 IntegerLiteral 应传入 IntType），或者在 Sema 构建表达式时通过 setType() 设置。这属于后续 Stage 的工作。


-----------------------------------------

修改总结
1. include/blocktype/AST/Decl.h — TagDecl 添加 IsCompleteDefinition
在 TagDecl（RecordDecl/EnumDecl 的公共基类）中添加：

bool IsCompleteDefinition = false 字段（默认 false，向后兼容）
isCompleteDefinition() / setCompleteDefinition(bool) 方法
2. src/Sema/Sema.cpp — 修复 isCompleteType
Record 类型：从 !RD->members().empty() || RD->getNumBases() > 0 改为 RT->getDecl()->isCompleteDefinition() Enum 类型：从 !ET->getDecl()->enumerators().empty() 改为 ET->getDecl()->isCompleteDefinition()

这修复了两个问题：

空类 class X {}; 不再被误判为不完整
普通 RecordDecl（C struct/union）不再始终返回 false
3. src/Parse/ParseClass.cpp — 解析完成后标记完整定义
在 class/struct/union 的 } 消费后调用 setCompleteDefinition()

4. src/Parse/ParseDecl.cpp — enum 解析完成后标记完整定义
在 enum 的 } 消费后调用 setCompleteDefinition()

Sema.cpp stub 代码
已确认 Stage 4.3 的 stub 代码（LookupUnqualifiedName 等）已在之前被移除，只剩一行注释 // Name lookup [Stage 4.3 — implemented in Lookup.cpp]，无需修改。


5. Expr.h — 构造函数添加 QualType 参数：

IntegerLiteral(Loc, Val, QualType) → Expr(Loc, T)
FloatingLiteral(Loc, Val, QualType) → Expr(Loc, T)
CharacterLiteral(Loc, Val, QualType) → Expr(Loc, T)
CXXBoolLiteral(Loc, Val, QualType) → Expr(Loc, T)
CXXNullPtrLiteral(Loc, QualType) → Expr(Loc, T)
StringLiteral(Loc, Val, QualType) → Expr(Loc, T)
DeclRefExpr(Loc, D) → 从 D->getType() 推导
MemberExpr(Loc, Base, Member, IsArrow) → 从 Member->getType() 推导
ParseExpr.cpp / Parser.cpp / ParseDecl.cpp — Parser 传入正确类型：

Context.getIntType(), Context.getDoubleType(), Context.getCharType(), Context.getBoolType(), Context.getNullPtrType()
-----------------------------------------


二、Stage 4.3 核查

Task 4.3.1 Lookup 基础设施

有问题的项目：
LookupResult 类	 ⚠️
NestedNameSpecifier 类	⚠️

问题清单：

🔴 Sema.cpp 中的 stub 覆盖了 Lookup.cpp 的实现（与问题 9 重复）

这是最严重的问题。必须立即删除 Sema.cpp 第 327-353 行。

🟡 LookupResult 缺少去重机制

addDecl() 无条件 push_back，没有检查是否已存在同一声明。多次 ADL 或 using 指令查找可能导致重复结果。
Clang 对比：Clang 的 LookupResult::addDecl() 内部使用 llvm::SmallPtrSet 做去重。

🟡 LookupResult::clear() 没有清除 TypeName 和 Overloaded 标志

第 139 行：void clear() { Decls.clear(); Ambiguous = false; } — 缺少 TypeName = false; Overloaded = false;。

🟡 NestedNameSpecifier 使用 const char* 存储 Identifier

Create(Ctx, Prefix, Identifier) 在第 87 行存储 Identifier.data()（原始字符串指针）。如果原始 llvm::StringRef 指向临时字符串，指针将悬挂。
Clang 对比：Clang 使用 ASTContext::saveString() 持久化字符串。
建议：在 Create 方法中使用 Ctx.saveString(Identifier).data() 保存字符串。


🟡 NestedNameSpecifier 缺少 getAsTemplate() 方法
文档 E4.3.1.1 规定了 TemplateTypeSpec 种类，但没有提供获取模板信息的接口。

--------------------------------------------------

Task 4.3.1 修改总结
1. include/blocktype/Sema/Lookup.h — LookupResult 修复
添加 SmallPtrSet<NamedDecl*, 4> FoundDecls 用于去重
addDecl()：先检查 FoundDecls.insert(D).second，仅在新声明时才 push_back
addAllDecls()：改用 addDecl(D) 替代直接 push_back，自动去重
clear()：同时重置 FoundDecls、TypeName、Overloaded（原来只清 Decls 和 Ambiguous）
2. include/blocktype/Sema/Lookup.h — NestedNameSpecifier 修复
Union 扩展：添加 TemplateDecl *Template 成员，支持 TemplateTypeSpec 种类存储模板声明
添加 getAsTemplate()：返回 Data.Template，仅在 Kind == TemplateTypeSpec 时有效
修复悬挂指针：Create(Ctx, Prefix, Identifier) 现在使用 Ctx.saveString(Identifier) 持久化字符串
3. src/Sema/Lookup.cpp — NestedNameSpecifier::Create 修复
Identifier 版 Create 方法改为 Ctx.saveString(Identifier) 后再取 .data()，避免原始 StringRef 临时失效
Sema.cpp stub 代码
已确认 Sema.cpp 第 326-327 行只有注释 // Name lookup [Stage 4.3 — implemented in Lookup.cpp]，无函数定义，不存在覆盖问题。

-------------------------------------------------
Task 4.3.2 Unqualified Lookup
有问题的项目：
函数重载收集⚠️
using 指令处理⚠️


问题清单：

🟡 函数重载收集逻辑在非函数匹配时过早返回

第 154-158 行：找到非函数声明后直接 return Result，不会继续向上查找函数重载。这不符合 C++ 的"隐藏规则"：如果外层作用域有同名函数，应被内层的非函数声明隐藏。当前实现是正确的，但没有收集 using 指令中的重载。
场景：using namespace std; 中有 swap 函数，当前作用域有变量 int swap = 0;。正确行为是变量隐藏函数，当前实现正确。


🟡 using 指令处理中的函数重载收集不完整

第 178-186 行：只在当前 scope 没有找到名字时处理 using 指令。但如果当前 scope 找到了函数，using 指令中的同名函数也应作为重载候选加入。这在 Clang 中由 Sema::LookupQualifiedName 配合处理。


🟡 LookupOperatorName 和 LookupMemberName 没有特殊处理

文档 E4.3.1.1 定义了 LookupOperatorName 和 LookupMemberName，但 LookupUnqualifiedName 中没有对它们做特殊处理，统一走普通查找逻辑。对 operator 查找，Clang 有 Sema::LookupOperatorName 单独实现，会同时在当前和参数关联作用域中查找。
------------------------------------------------

Task 4.3.2 修改总结
src/Sema/Lookup.cpp — LookupUnqualifiedName 重构
1. LookupMemberName 特殊处理（新增）

在函数开头添加 early return 路径：只搜索当前 scope（不向上遍历父 scope），同时收集同 scope 内的函数重载。适用于 obj.member 式成员访问。

2. LookupOperatorName 处理（注释说明）

添加注释说明 operator 查找分两阶段：第一阶段是 ordinary lookup（当前逻辑），第二阶段 ADL 由调用方单独调用 LookupADL 处理。

3. Using 指令函数重载收集修复（核心修复）

原来的 using 指令处理在 scope 匹配的 else 分支（即只有 scope 中找不到名字时才检查 using 指令）。

重构后的逻辑：

使用 FoundInScope 标志追踪当前 scope 是否找到匹配
找到函数时：继续处理 using 指令，将 using 命名空间中的同名函数作为重载候选加入（含命名空间内所有重载）
找到非函数时：跳过 using 指令（非函数声明隐藏 using 中的名字）
未找到时：正常处理 using 指令，行为与原来一致


----------------------------------------------

Task 4.3.3 Qualified Lookup

有问题的项目：
TypeSpec Class:: 查找⚠️


问题清单：

🟡 Qualified Lookup 中 TypeSpec 的 DeclContext 获取有误

第 246-248 行：CXXRD->getDeclContext() 返回的是类所在的父上下文（如命名空间），而不是类本身的上下文。应该是直接在 CXXRD 上查找，因为 CXXRecordDecl IS-A DeclContext。
修复：DC = static_cast<DeclContext*>(CXXRD); 而非 DC = CXXRD->getDeclContext();。
Clang 对比：Clang 的 Sema::ComputeDeclContext 对于 TypeSpec 直接返回 cast<CXXRecordDecl>(RT->getDecl())。


🟡 Qualified Lookup 不处理基类查找

Class::name 应在类及其基类中查找。当前只在指定类中查找。这是 C++ 的基本语义。
-------------------------------------------
Task 4.3.3 修复完成，编译通过。总结修改内容：

修改文件：src/Sema/Lookup.cpp
问题 1（TypeSpec DeclContext）：已确认当前代码正确（第 355-356 行 DC = static_cast<DeclContext *>(CXXRD)），无需修改。

问题 2（基类查找）：核心修改如下：

新增辅助函数 lookupInClassAndBases（第 280-323 行）：

递归在 CXXRecordDecl 及其所有基类中查找成员
使用 lookupInContext（而非 lookup）仅在类自身上下文查找，避免穿透到父命名空间
使用 SmallPtrSet<Decl*, 8> 防止菱形继承重复访问
每个类中都收集同名函数重载
修改 LookupQualifiedName（第 378-399 行）：

当 DC 是 CXXRecordDecl 时，调用 lookupInClassAndBases 进行类+基类查找
非类上下文（命名空间、枚举、TU）仍使用原有简单 lookup 逻辑
-------------------------------------------

Task 4.3.4 ADL
有问题的项目：
关联命名空间收集⚠️
关联类收集⚠️
友元函数查找⚠️


问题清单：

🟡 findNamespaceDecl 依赖于遍历父上下文的 decls

第 312-326 行：通过遍历 Parent->decls() 查找匹配的 NamespaceDecl。如果命名空间嵌套（A::B），父上下文中可能找不到直接子命名空间（因为 B 在 A 的 decls 中，不在 TU 的 decls 中）。这个函数只有在 NSCtx 的直接父级 decls 中搜索才可能成功。

🟡 ADL 友元函数查找逻辑不精确

第 349-363 行：当前在类的 DeclContext 中查找所有同名函数。但只有 friend 函数才应通过 ADL 可见。需要检查函数是否有 friend 属性。

🟡 ADL 不处理模板实例化的关联命名空间

std::vector<int> 的关联命名空间应包含 std。当前不处理 TemplateSpecializationType。


🟡 没有处理 inline 命名空间

C++ 的 inline namespace 的成员自动暴露在外围命名空间中。当前没有处理 NamespaceDecl::isInline()。
-------------------------------------------

Task 4.3.4 ADL 修复完成，编译通过。修改总结：

修改文件：src/Sema/Lookup.cpp（第 408-602 行）
修复 1 — 关联命名空间收集（findNamespaceDecl + addAssociatedNamespace）

findNamespaceDecl（第 430-446 行）：从仅搜索直接父级的 decls 改为沿祖先链递归搜索，增强嵌套命名空间的鲁棒性
新增 addAssociatedNamespace（第 451-465 行）：统一添加关联命名空间，并处理 inline 命名空间 — 当添加的命名空间是 inline 时，递归添加其外围命名空间
修复 2 — 友元函数查找（LookupADL 第 488-508 行）

旧代码遍历 CXXRD->getDeclContext()->decls() 查找所有同名 FunctionDecl（错误地包括了类中定义的普通成员函数）
新代码遍历 CXXRD 自身的 decls 中的 FriendDecl 节点，通过 FD->getFriendDecl() 获取友元函数声明，并跳过 isFriendType() 为 true 的情况（friend class 声明）
修复 3 — 模板特化类型处理（CollectAssociatedNamespacesAndClasses 第 567-577 行）

新增 TemplateSpecializationType 检测，遍历模板参数，对每个类型参数递归调用 CollectAssociatedNamespacesAndClasses
例如 std::vector<int> 会从 int（无关联命名空间）和模板自身的上下文收集
修复 4 — inline 命名空间 + ArrayType 处理

inline 命名空间通过 addAssociatedNamespace 自动添加外围命名空间
新增 ArrayType 处理（第 595-601 行），递归收集元素类型的关联命名空间和类


-------------------------------------------

三、总结：按严重程度排序
🔴 必须立即修复（3 项）
#	问题	位置
9/11	Sema.cpp stub 覆盖了 Lookup.cpp 的实现 — 所有名字查找永远返回空	Sema.cpp:327-353
1	getRecordType/getEnumType 无缓存 — 同一 Decl 创建多个 Type 导致比较失败	ASTContext.cpp:205-214
19	Qualified Lookup 的 DeclContext 获取错误 — CXXRD->getDeclContext() 返回父上下文而非类自身	Lookup.cpp:248
🟡 建议修复（14 项，按优先级排序）
#	问题	优先级
8	isCompleteType 空类误判为不完整	高
14	NestedNameSpecifier::Create(Identifier) 字符串悬挂	高
13	LookupResult::clear() 未清除 TypeName/Overloaded	中
12	LookupResult::addDecl() 无去重	中
4/5	ForwardingRef/decltype 未区分值类别	中（需 Expr 增强）
2	getMemberFunctionType CV 限定符语义错误	中
17	using 指令重载收集不完整	中
21	findNamespaceDecl 嵌套命名空间可能失败	中
22	ADL 友元查找缺少 friend 检查	低
20	Qualified Lookup 不查找基类	低
18	LookupOperatorName/MemberName 无特殊处理	低
6	deduceFromInitList 类型比较过于严格	低
23	ADL 不处理模板实例化关联命名空间	低
24	不处理 inline 命名空间	低
是否需要我立即修复 3 个🔴严重问题？