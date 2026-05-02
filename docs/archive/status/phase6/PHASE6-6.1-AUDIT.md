Audit Findings
## A. 对比开发文档 (06-PHASE6-irgen.md) — Stage 6.1 部分

## -----------------------------------------------------------

## Task 6.1.1 CodeGenModule 类
文档要求 vs 实际实现：

1.✅ EmitTranslationUnit(TranslationUnitDecl *TU) — 已实现
--✅ [已修复] 两遍发射逻辑已修正：
----第一遍：声明（函数声明 + 全局变量延迟队列 + Record 前向声明 + 类布局计算）
----EmitDeferred：发射全局变量定义
----第二遍（新增）：遍历 TU->decls() 调用 EmitFunction 生成有函数体的函数
----最后：发射全局构造/析构

2.✅ EmitDeferred() — 已实现，逻辑已简化
--✅ [已修复] 移除了无效的 Module 函数遍历循环，只负责发射延迟全局变量

3.✅ EmitGlobalVar(VarDecl *VD) — 已实现
--✅ [已修复] isConstant 判断现在使用 isConstexpr() || isConstQualified()
--✅ [已修复] static 变量使用 InternalLinkage，其他使用 ExternalLinkage
--✅ 注册到 GlobalValues 表正确

4.✅ GetGlobalVar(VarDecl *VD) — 已实现

5.✅ EmitFunction(FunctionDecl *FD) — 已实现

6.✅ GetFunction(FunctionDecl *FD) — 已实现

7.✅ GetOrCreateFunctionDecl(FunctionDecl *FD) — 已实现
--✅ [已修复] inline 函数使用 LinkOnceODRLinkage，并添加 AlwaysInline 属性
--✅ [已修复] noexcept(true) 函数设置 DoesNotThrow 属性

8.✅ EmitVTable(CXXRecordDecl *RD) — 委托到 CGCXX

9.✅ EmitClassLayout(CXXRecordDecl *RD) — 委托到 CGCXX

10.✅ AddGlobalCtor/AddGlobalDtor — 已实现

11.✅ EmitGlobalCtorDtors() — 已实现

## -----------------------------------------------------------
## Task 6.1.2 CodeGenTypes 类型映射
文档要求 vs 实际实现：

1.✅ ConvertType(QualType) — 已实现，分派逻辑完整
--所有 TypeClass 都覆盖了

2.✅ ConvertTypeForMem(QualType) — 已实现（简单委托）

3.✅ [已修复] ConvertTypeForValue(QualType) — 已新增实现
--数组类型的值类型返回指向首元素的指针（LLVM 中数组不能作为一等值传递）
--其他类型委托给 ConvertType

4.✅ GetFunctionType(const FunctionType *) — 已实现

5.✅ GetFunctionTypeForDecl(FunctionDecl *) — 已实现
--✅ [已修复] 成员函数 this 指针已在函数类型中添加
--✅ [已修复] 新增 FunctionTypeCache 缓存避免重复计算
--✅ [已修复] sret 参数（大结构体返回值通过内存传递）已处理
--✅ [已修复] inreg 属性（this 指针寄存器传递）已处理
--新增 ABIArgInfo / FunctionABITy 结构和 GetFunctionABI() 方法
--非静态成员函数：this 指针作为第一个参数（指向类结构体的指针）

6.✅ GetRecordType(RecordDecl *) — 已实现
--✅ 使用 opaque struct 先占位避免递归
--✅ CXXRecordDecl 的 vptr 处理

7.✅ [已修复] GetCXXRecordType(CXXRecordDecl *RD) — 已新增实现
--委托给 GetRecordType（已处理 vptr）

8.✅ GetFieldIndex(FieldDecl *) — 已实现
--✅ [已修复] 新增 FieldIndexCache（DenseMap<FieldDecl*, unsigned>），O(1) 查找
--GetRecordType/GetCXXRecordType 构建 struct 时同步填充缓存

9.✅ GetTypeSize/GetTypeAlign — 委托到 TargetInfo

10.✅ [已修复] GetSize(uint64_t) / GetAlign(uint64_t) — 已新增实现
--返回 llvm::ConstantInt（i64 类型）

11✅ 所有 22 种 BuiltinType 映射 — 已实现
--✅ WChar 映射到 i32（AArch64/x86_64 macOS 上 wchar_t 为 4 字节，正确）
--✅ [已修复] LongDouble 平台适配：Darwin → double（8 字节），Linux → FP128（16 字节）

12✅ ConvertBuiltinType — 所有 22 种覆盖

13✅ ConvertPointerType — 已实现

14✅ ConvertReferenceType — 已实现

15✅ ConvertArrayType — 已实现

16✅ ConvertFunctionType — 已实现

17✅ ConvertRecordType — 已实现

18✅ ConvertEnumType — 已实现
--✅ [已修复] 不再硬编码 i32，现在查询 EnumDecl::getUnderlyingType()

19✅ ConvertTypedefType — 已实现

20✅ ConvertTemplateSpecializationType — 已实现

21✅ ConvertMemberPointerType — 已实现（简化为 i8*）

22✅ ConvertAutoType — 已实现

23✅ ConvertDecltypeType — 已实现

24✅ ElaboratedType 处理 — 已实现（递归到 getNamedType()）
## -----------------------------------------------------------

## Task 6.1.3 CodeGenConstant 常量生成
文档要求 vs 实际实现：

1.✅ EmitConstant(Expr *) — 已实现

2.✅ EmitConstantForType(Expr *, QualType) — 已实现

3.✅ EmitIntLiteral(IntegerLiteral *) — 已实现

4.✅ EmitFloatLiteral(FloatingLiteral *) — 已实现

5.✅ EmitStringLiteral(StringLiteral *) — 已实现

6.✅ EmitBoolLiteral(bool) — 已实现

7.✅ EmitCharLiteral(CharacterLiteral *) — 已实现

8.✅ EmitNullPointer(QualType) — 已实现

9.✅ EmitInitListExpr(InitListExpr *) — 已实现

10.✅ EmitZeroValue(QualType) — 已实现

11.✅ EmitUndefValue(QualType) — 已实现

12.✅ [已修复] EmitIntCast(llvm::Constant *, QualType, QualType) — 已新增实现
--整数截断/符号扩展

13.✅ [已修复] EmitFloatToIntCast(llvm::Constant *, QualType, QualType) — 已新增实现
--APFloat → APSInt 转换

14.✅ [已修复] EmitIntToFloatCast(llvm::Constant *, QualType, QualType) — 已新增实现
--APInt → APFloat 转换

15.✅ [已修复] GetNullPointer(QualType) — 已新增实现
--返回 ConstantPointerNull

16.✅ GetIntZero/GetIntOne — 已实现

17.✅ getLLVMContext() — 已实现

## -----------------------------------------------------------

## Task 6.1.4 TargetInfo 目标平台信息
文档要求 vs 实际实现：

✅ TargetInfo(llvm::StringRef) — 已实现
✅ getTypeSize(QualType) — 已实现
✅ getTypeAlign(QualType) — 已实现
✅ getBuiltinSize(BuiltinKind) — 已实现
✅ getBuiltinAlign(BuiltinKind) — 已实现
✅ getPointerSize() — 已实现
✅ getPointerAlign() — 已实现
✅ getDataLayout() — 已实现
✅ getTriple() — 已实现
✅ isStructReturnInRegister(QualType) — 已实现
✅ isThisPassedInRegister() — 已实现
✅ getEnumSize() — 已实现
TargetInfo 对比文档非常完整，没有遗漏。
## -----------------------------------------------------------
## B. 对比 Clang 同模块特性
## -----------------------------------------------------------

## CodeGenModule 对比 Clang CodeGenModule

1. Mangle 系统 — Clang 使用 NameMangler 生成唯一符号名（如 _Z3fooi）。BlockType 直接使用 Decl 名称，会导致：
--✅ 简化处理可接受（Stage 6.1 基础设施）
--✅ 支持 C++ name mangling（重载函数区分）

2. 属性处理 — Clang 处理 visibility, dllimport/dllexport, weak 等属性。BlockType 未处理任何函数/变量属性。
--P2 问题：缺少属性处理框架

3. Linkage/Visibility — Clang 正确处理 InternalLinkage/ExternalLinkage/WeakAnyLinkage 等。
--✅ [已修复] static 全局变量使用 InternalLinkage
--✅ [已修复] inline 函数使用 LinkOnceODRLinkage
--✅ 完整的 Visibility 属性未处理（P2）

4. 延迟函数发射 — Clang 有完整的延迟发射机制（deferred functions）。
--✅ [已修复] 两遍发射逻辑已修正，函数体正确发射

5. 全局变量初始化优先级 — Clang 区分常量初始化（可编译期完成）和动态初始化。BlockType 未区分。
--P2 问题

## -----------------------------------------------------------


## CodeGenTypes 对比 Clang CodeGenTypes

1. 函数类型生成 — Clang 的 GetFunctionType 处理了：
--✅ [已修复] this 指针（成员函数首个参数）已处理
--✅ [已修复] sret 参数（结构体返回值通过内存传递）已处理
----新增 ABIArgInfo / FunctionABITy 结构描述参数传递方式
----GetFunctionABI() 计算完整 ABI 信息并缓存
----GetOrCreateFunctionDecl 中设置 sret 属性 + NoAlias
----TargetInfo::isStructReturnInRegister() 控制阈值（当前 > 16 字节触发 sret）
--✅ [已修复] inreg 属性已处理
----新增 shouldUseInReg() 方法（当前返回 false，为后续 ABI 扩展预留）
----this 指针根据 TargetInfo::isThisPassedInRegister() 标记 inreg
----GetOrCreateFunctionDecl 中根据 ABI 信息设置 Attribute::InReg
--✅ 变参处理已实现

2. 结构体布局 — Clang 使用 ASTContext::getASTRecordLayout 获取精确布局（含基类子对象、虚基类、填充）。BlockType 简化为按声明顺序排列。
--✅ [已修复] 基类子对象已通过 collectBaseClassFields() 展平到派生类
--⚠️ 缺少精确填充计算（padding）和对齐处理 — P2

3. 枚举类型 — Clang 正确获取枚举的底层类型（通过 EnumDecl::getIntegerType()）。
--✅ [已修复] 现在查询 EnumDecl::getUnderlyingType()

4. Record 类型基类子对象
--✅ [已修复] GetRecordType 通过 collectBaseClassFields() 递归展平基类字段
--⚠️ 虚继承布局未处理 — P2

## -----------------------------------------------------------

## CodeGenConstant 对比 Clang ConstantEmitter

1. ✅ [已修复] 常量折叠 — sizeof/alignof 已完整支持
--新增 UnaryExprOrTypeTraitExpr AST 节点（NodeKinds.def + Expr.h）
--新增 UnaryExprOrTypeTrait 枚举（SizeOf / AlignOf）
--支持两种形式：sizeof(type) 和 sizeof expr
--Parser: parseUnaryExprOrTypeTraitExpr() 解析 sizeof/alignof（含类型/表达式歧义消解）
--CodeGenConstant: EmitConstant() 中新增 case，通过 TargetInfo::getTypeSize/getTypeAlign 求值
--CodeGenFunction: EmitExpr() + EmitUnaryExprOrTypeTraitExpr() 返回 ConstantInt(i64)
--AST dump 实现完整
--⚠️ 地址常量（&x, &arr[i]）未处理 — P2

2. ✅ [已修复] 字符串字面量合并 — 已实现池化去重
--新增 StringLiteralPool（DenseMap<StringRef, GlobalVariable*>）在 CodeGenModule
--EmitStringLiteral 先查池，命中则复用已有全局变量，未命中则创建并注册
--减少重复字符串的全局变量数量，与 Clang 行为一致

3. ✅ [已修复] 静态初始化 — 已区分三种初始化类型
--ClassifyGlobalInit() 区分：零初始化、常量初始化、动态初始化
--constexpr 变量 → ConstantInitialization
--能被 EmitConstantForType 求值的 → ConstantInitialization
--其他 → DynamicInitialization（延迟到 EmitDynamicGlobalInit 处理）
--DynamicInitVars 队列正确管理延迟初始化

## -----------------------------------------------------------

## CGCXX 对比 Clang CGCXX + CGClass + CGVTables
1. ✅ [已修复] VTable 布局 — 已包含 offset-to-top + RTTI + 虚函数指针
--EmitVTable() 生成布局：[offset-to-top(i64→ptr)] [RTTI ptr] [base vfuncs...] [own new vfuncs...]
--GetVTableIndex() 正确跳过头部 2 条目（Idx = 2）
--覆盖检测：遍历基类虚函数，查找派生类同名同参方法
--⚠️ 多重继承下非主 vtable 的 offset-to-top 应为非零值 — P2

2. ✅ [已修复] VTable 继承 — 单一继承已正确处理
--EmitVTable() 先收集基类虚函数（检查覆盖），再添加自身新增虚函数
--ComputeClassLayout() 处理基类子对象偏移 + vptr 位置
--构造函数中 InitializeVTablePtr() 在每个基类初始化后立即更新 vptr
--⚠️ 多重继承下多个子 vtable 未处理 — P2

3. ⚠️ 虚析构函数 — 未生成 deleting/complete 两个版本
--当前 EmitDestructor() 只生成单一析构函数
--Clang ABI 要求：complete destructor（析构对象+基类+成员）+ deleting destructor（调用 complete 后 operator delete）
--⚠️ 虚析构函数 vtable 条目应指向 deleting destructor — P2

## -----------------------------------------------------------

C. 关联关系错误
## -----------------------------------------------------------

1. ✅ [已修复] EmitTranslationUnit 的两遍发射逻辑已修正：
--第一遍：创建声明 + 延迟全局变量 + 类布局
--EmitDeferred：发射全局变量定义
--第二遍（新增）：遍历 TU->decls() 调用 EmitFunction 生成有函数体的函数

2. ✅ [已修复] CodeGenTypes.h 已添加 FunctionTypeCache
--在 GetFunctionTypeForDecl 中使用缓存

3. ✅ PointerType::getPointeeType() 返回 const Type* — 代码中使用 QualType(PT->getPointeeType(), Qualifier::None) 正确

4. ⚠️ [遗留] TargetInfo 构造函数使用 DataLayout(StringRef) — 如果传入无效三元组，DataLayout 字符串为空，`llvm::DataLayout("")` 会生成空布局（所有大小为 0）
--风险：当前 `getDataLayoutForTriple()` 有 64-bit fallback 兜底，但若绕过该函数直接构造 DataLayout 则无保护
--建议：在 TargetInfo 构造函数中增加 triple 有效性校验，或在 DataLayout 为空时 assert（P2）


## ---------------------------------------------------------------
## D. 汇总

## P0 问题（必须立即修复） — 全部已修复 ✅
1. ✅ 函数体从未被发射 — 已修正两遍发射逻辑，第二遍遍历 AST 调用 EmitFunction

## P1 问题（应尽快修复） — 全部已修复 ✅
1. ✅ 缺少 ConvertTypeForValue() — 已新增
2. ✅ 缺少 GetCXXRecordType() — 已新增
3. ✅ 缺少 GetSize() / GetAlign() — 已新增
4. ✅ 缺少 EmitIntCast / EmitFloatToIntCast / EmitIntToFloatCast — 已新增
5. ✅ 缺少 GetNullPointer() — 已新增
6. ✅ 全局变量/函数 linkage 类型判断 — static→InternalLinkage, inline→LinkOnceODR
7. ✅ 成员函数 this 指针处理 — GetFunctionTypeForDecl 中为非静态成员函数添加 this
8. ✅ 枚举类型不再硬编码 — 查询 EnumDecl::getUnderlyingType()

## P2 问题（后续改进）
1. ✅ [已修复] 函数属性设置（inline→AlwaysInline, noexcept→DoesNotThrow）
2. ✅ [已修复] 全局变量 isConstant 判断（isConstexpr || isConstQualified）
3. ✅ [已修复] 字符串字面量合并（StringLiteralPool 池化去重）
4. ✅ [已修复] VTable 布局已包含 offset-to-top + RTTI ptr
5. ⚠️ Record 类型布局缺少精确填充计算（padding/alignment）
6. ✅ [已修复] LongDouble 在 macOS 上已适配为 double（8 字节），Linux 为 FP128（16 字节）
7. GetFieldIndex 效率低（遍历所有 RecordTypeCache）
8. ✅ [已修复] FunctionTypeCache 已添加
9. ✅ [已修复] sret 参数（大结构体返回值通过内存传递）已处理
10. ✅ [已修复] inreg 属性已处理（框架就绪，当前 shouldUseInReg 返回 false）
11. 虚继承布局未处理
12. ✅ [已修复] sizeof/alignof 常量表达式 — 完整 AST + Parser + CodeGen 支持
