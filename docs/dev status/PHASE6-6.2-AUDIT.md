Audit Findings
## A. 对比开发文档 (06-PHASE6-irgen.md) — Stage 6.2 部分

## -----------------------------------------------------------

## Task 6.2.1 CodeGenFunction 类框架
文档要求 vs 实际实现：

1.✅ CodeGenFunction(CodeGenModule &M) — 已实现
--构造函数初始化 CGM 引用和 IRBuilder

2.✅ EmitFunctionBody(FunctionDecl *, llvm::Function *) — 已实现
--创建 entry 块
--为返回值创建 alloca + ReturnBlock（非 void 类型）
--为函数参数创建 alloca 并存储
--生成函数体语句
--处理 ReturnBlock 统一返回逻辑

3.✅ EmitExpr(Expr *) — 已实现
--完整的 switch 分派覆盖：字面量(6种)、引用(3种)、运算符(3种)、调用(2种)、C++(6种)、转换(5种)、初始化列表(1种)

4.✅ EmitStmt(Stmt *) — 已实现
--覆盖所有 Stmt 类型：Compound/Return/Expr/Decl/If/Switch/For/While/Do/Break/Continue/Goto/Label/Null/CXXTry/Case/Default/CXXCatch

5.✅ createBasicBlock(StringRef) — 已实现

6.✅ EmitBlock(BasicBlock *) — 已实现
--正确处理已有终止指令的情况

7.✅ getCurrentBlock() / haveInsertPoint() — 已实现

8.✅ CreateAlloca(QualType, StringRef) — 已实现
--使用 saveIP/restoreIP 在入口块创建 alloca（参照 Clang 模式）

9.✅ setLocalDecl / getLocalDecl / LoadLocalVar / StoreLocalVar — 已实现

10.✅ BreakContinueStack — 已实现
--struct BreakContinue { BreakBB, ContinueBB }

11.✅ ReturnBlock + ReturnValue — 已实现
--统一的 ReturnBlock 模式（参照 Clang）

12.⚠️ EmitExprForStore(Expr *) — 文档中定义了但未实现
--当前 EmitAssignment 直接处理 store 逻辑
--不影响功能正确性，但缺少文档定义的接口（P2）
开发文档方案 vs 实际实现

设计思路	
文档方案 EmitExprForStore：独立接口，EmitExpr 求值 → EmitExprForStore 包装 store	
实际实现 EmitAssignment：一体化，EmitAssignment 内部直接 EmitExpr(RHS) + CreateStore

职责边界	
文档方案 EmitExprForStore：EmitExpr 只求值，store 逻辑由专用接口负责	
实际实现 EmitAssignment：EmitExpr 只求值，store 由赋值/复合赋值函数各自处理

差距
接口层次：文档设想 EmitExprForStore 作为 EmitExpr 的 store 变体，用于所有需要"求值+存储"的场景。但实际上只有 EmitAssignment 和 EmitCompoundAssignment 需要 store，两个函数各自处理，逻辑清晰。

重复模式：store-back 逻辑在 EmitAssignment（第 199-229 行）和 EmitCompoundAssignment（第 302-316 行）中有相似的三级分派（局部变量 → 全局变量 → EmitLValue）。这是唯一可提取的共性代码。

风险评估
功能正确性：无风险。store 逻辑完整覆盖局部/全局/成员/数组下标
可维护性：低风险。两个函数的 store 逻辑如果未来需要扩展（如 volatile、原子操作），需要同步修改两处。可提取为一个 StoreToLValue(Expr *LHS, Value *RHS) 内部辅助方法
API 一致性：文档接口未被声明。这是 P2 级别的 API 规范问题，不影响功能
建议：保持现状即可。如果未来 store 逻辑变复杂（volatile/atomic），再提取公共辅助方法。现在不需要为此投入时间。
## -----------------------------------------------------------

## Task 6.2.2 算术与逻辑表达式
文档要求 vs 实际实现：

1.✅ EmitBinaryOperator(BinaryOperator *) — 已实现
--完整分派：短路逻辑 → 赋值 → 复合赋值 → 逗号 → 指针运算 → 整数 → 浮点

2.✅ 整数算术 — 已实现
--Add/Sub/Mul/SDiv|UDiv/SRem|URem/Shl/AShr|LShr/And/Or/Xor
--有符号/无符号区分正确（通过 isSignedType 判断 BuiltinType::isSignedInteger()）

3.✅ 浮点算术 — 已实现
--FAdd/FSub/FMul/FDiv/FRem

4.✅ 比较运算 — 已实现
--整数：ICmp with SLT/ULT/SGT/UGT/SLE/ULE/SGE/UGE/EQ/NE
--浮点：FCmp with OLT/OGT/OLE/OGE/OEQ/ONE

5.✅ 短路逻辑运算 — 已实现
--EmitLogicalAnd: CondBr + PHI 节点，保存 LHBB 避免 getParent() 问题
--EmitLogicalOr: CondBr + PHI 节点，同上

6.✅ 赋值运算 — 已实现（P1-1 ✅ 已修复）
--EmitAssignment: DeclRefExpr(局部/全局) + ArraySubscriptExpr + MemberExpr(EmitLValue)
--✅ 成员赋值(MemberExpr) 的 EmitLValue 路径已完成

7.✅ 复合赋值运算 — 已实现（P1-2 ✅ 已修复）
--EmitCompoundAssignment: 10种复合赋值 → 基础运算 + store back
--✅ 局部变量/全局变量/成员变量(EmitLValue) store back 均已处理

8.✅ 指针运算 — 已实现（含 pointer - pointer → ptrdiff_t）
--pointer ± integer → GEP，正确使用 PointerType::getPointeeType()
--pointer - pointer → PtrToInt + Sub + SDiv(sizeof(pointee))，返回 ptrdiff_t (i64)

9.✅ 一元运算 — 已实现
--Minus(整数/浮点) / Plus / Not(位反) / LNot(逻辑非) / Deref(解引用) / AddrOf(取地址) / PreInc|PreDec|PostInc|PostDec

10.✅ ++/-- 运算 — 已实现（P1-3 ✅ 已修复）
--EmitIncDec: 正确区分 prefix/postfix 返回值
--✅ 局部变量/全局变量/成员变量均已处理

11.✅ 逗号运算符 — 已实现

12.✅ Spaceship (<=> , C++20) — 已实现
--Sema: getBinaryOperatorResultType 返回 int（非 bool）
--CodeGen: 整数和浮点均支持，生成 (LHS > RHS) - (LHS < RHS) → -1/0/1

## -----------------------------------------------------------

## Task 6.2.3 函数调用
文档要求 vs 实际实现：

1.✅ EmitCallExpr(CallExpr *) — 已实现
--从 DeclRefExpr 和 MemberExpr 获取 CalleeDecl
--调用 CGM.GetOrCreateFunctionDecl 获取 llvm::Function

2.✅ 成员函数 this 指针 — 已实现
--检测 CXXMethodDecl && !isStatic() → 添加 base 作为 this 参数

3.✅ 参数生成 — 已实现
--遍历 CallExpr::getArgs() 逐个 EmitExpr

4.✅ this 指针对 obj.method() 的处理不完整（P1-10 ✅ 已修复）
--根因：EmitCallExpr 第 635 行使用 EmitExpr(base) 获取 this，对 obj.method() 返回 load 后的结构体值（如 {i32,i32}）
--但 this 指针应为 %struct*（结构体指针），需要改用 EmitLValue(base) 获取地址
--修复：按 isArrow() 分支，ptr->method() 使用 EmitExpr(base)，obj.method() 使用 EmitLValue(base)
--新增 lit 测试覆盖 obj.method() 调用路径

5.✅ 未设置调用属性 — 已修复
--函数级属性（AlwaysInline/DoesNotThrow）已在 GetOrCreateFunctionDecl 中设置
--新增：Parser 层 parseDeclSpecifierSeq 解析 [[noreturn]] → FunctionDecl 存储 AttributeListDecl
--新增：GetOrCreateFunctionDecl 检查 hasAttr("noreturn") 调用 Fn->setDoesNotReturn()
--新增：EmitCallExpr 在 CreateCall/CreateInvoke 后设置 CallInst->setDoesNotReturn() 调用点属性

6.✅ 未处理隐式参数转换 — 已修复
--非虚函数参数循环已有 EmitScalarConversion（之前已修复）
--虚函数参数循环已有 EmitScalarConversion（之前已修复）
--变参部分由条目 7 覆盖（emitDefaultArgPromotion）

7.✅ 未处理变参函数 — 已修复
--AST: FunctionDecl 新增 isVariadic() 委托到 FunctionType::isVariadic()
--CodeGen: 新增 emitDefaultArgPromotion 辅助方法实现 C ABI default argument promotion
--float → double（浮点提升）, < 32-bit integer → int（整数提升）
--虚函数和非虚函数两处参数循环均增加 IsVarArg 判断，对 I >= Params.size() 的参数做提升

## -----------------------------------------------------------

## Task 6.2.4 成员访问与类型转换
文档要求 vs 实际实现：

1.✅ EmitMemberExpr(MemberExpr *) — 已实现
--通过 GEP 访问结构体字段
--正确区分指针类型和记录类型获取 StructType

2.✅ EmitArraySubscriptExpr(ArraySubscriptExpr *) — 已实现
--GEP with [0, index] 模式

3.✅ EmitCastExpr(CastExpr *) — 已实现
--完整覆盖：CStyle/CXXStatic/CXXDynamic/CXXConst/CXXReinterpret

4.✅ 类型转换路径 — 已实现
--int↔int (CreateIntCast)
--fp↔fp (CreateFPCast)
--int→fp (SIToFP/UIToFP)
--fp→int (FPToSI/FPToUI)
--ptr↔int (PtrToInt/IntToPtr)
--ptr↔ptr (BitCast)

5.✅ EmitConditionalOperator(ConditionalOperator *) — 已实现
--CondBr + Then/Else/Merge + PHI 节点

6.✅ EmitInitListExpr(InitListExpr *) — 已实现
--结构体初始化：StructGEP + Store
--数组初始化：GEP[0, idx] + Store

7.✅ EmitLValue(Expr *) — 已实现
--支持：DeclRefExpr(局部/全局)、MemberExpr、ArraySubscriptExpr、UnaryOperator(Deref)

8.✅ dynamic_cast 实现运行时类型检查（RTTI 基础设施完整）
--AST: CXXDynamicCastExpr 新增 DestType 字段，Parser 传递 CastType
--RTTI: EmitTypeInfo 生成 _ZTS typeinfo name + _ZTI typeinfo 对象全局变量
  ----无基类: __class_type_info / 单继承: __si_class_type_info / 多重继承: __vmi_class_type_info
--VTable: RTTI 槽位从 null 替换为真实 typeinfo 指针
--CodeGen: EmitDynamicCast 实现 null check → 加载 vtable RTTI → 调用 __dynamic_cast
--引用类型 dynamic_cast<T&> 失败时调用 __cxa_bad_cast()
--非多态类型 fallback 为 static bitcast
--全部 662 测试通过，无回归

9.✅ DerivedToBase / BaseToDerived 转换 — 已实现（P1-4 ✅ 已修复）
--CastKind 已扩展：DerivedToBase 使用偏移量 GEP，BaseToDerived 使用 BitCast

10.✅ LValueToRValue 转换 — 已实现（P1-5 ✅ 已修复）
--CastKind::LValueToRValue 在 EmitCastExpr 中生成 CreateLoad

## -----------------------------------------------------------

## 额外实现（文档未要求但已添加）：

1.✅ C++ 表达式 — 已实现
--EmitCXXThisExpr: 返回第一个参数
--EmitCXXConstructExpr: alloca + 调用构造函数（Stage 6.4 增强）
--EmitCXXNewExpr: malloc + bitcast
--EmitCXXDeleteExpr: free
--EmitCXXThrowExpr: unreachable（简化）

2.✅ 控制流语句 — 已实现（属于 Stage 6.3 范围但已提前完成）
--EmitIfStmt / EmitSwitchStmt(使用 SwitchInst) / EmitForStmt / EmitWhileStmt / EmitDoStmt
--EmitReturnStmt / EmitDeclStmt / EmitBreakStmt / EmitContinueStmt
--EmitGotoStmt / EmitLabelStmt / EmitCXXTryStmt

## -----------------------------------------------------------
## B. 对比 Clang 同模块特性
## -----------------------------------------------------------

## CodeGenFunction 对比 Clang CodeGenFunction

1. ✅ IRBuilder 使用模式 — 与 Clang 一致
--Clang 使用 IRBuilder::CreateAdd/Sub/Mul 等
--BlockType 同样使用，正确

2. ✅ ReturnBlock 模式 — 与 Clang 一致
--Clang 使用统一的 ReturnBlock + ReturnValue alloca
--BlockType 实现了相同模式

3. ✅ BreakContinueStack — 与 Clang 一致
--Clang 使用类似的 BreakContinue 栈结构

4. ⚠️ Alloca 插入位置 — Clang 更精细
--Clang 在所有 alloca 之前保存插入点，使用 AllocaInsertPt 成员变量
--BlockType 每次临时 save/restore，功能正确但效率略低（P2）

5. ⚠️ 条件表达式 bool 转换 — Clang 更精确
--Clang 对条件表达式有专门的 EmitScalarConversion 和 EmitConversionToBool
--BlockType 统一使用 CreateICmpNE + null，功能正确但不处理非零整数/浮点情况（P2）

6. ⚠️ 缺少 Cleanups 栈
--Clang 有完整的 EH 和 cleanup 栈（RunCleanupsScope）
--BlockType 未实现（P2，异常处理相关）

## -----------------------------------------------------------

## 表达式生成对比 Clang

1. ✅ 整数提升 (integral promotion) — 已修复
--EmitBinaryOperator: 删除硬编码 promoteToInt32 lambda，改用 EmitScalarConversion 将操作数转换到 BinaryOp->getType() 推导的公共类型
--EmitCompoundAssignment: 运算前用 EmitScalarConversion 将 RHS 转换到 LHS 类型
--EmitUnaryOperator: Minus/Not 运算前用 EmitScalarConversion 将操作数提升到 UnaryOp->getType()（Sema 已正确做了 integral promotion）
--覆盖：short+long→i64, short+int→i32, -short→int, ~short→int, short+=int 等场景

2. ✅ 指针差值 (pointer - pointer → ptrdiff_t) — 已修复
--Sema 层 getBinaryOperatorResultType: 新增 pointer+integer→ptr, integer+pointer→ptr, pointer-integer→ptr, pointer-pointer→long(ptrdiff_t) 特殊分发
--CodeGen 层 EmitBinaryOperator: 新增 both isPointerTy() && Sub 分支，用 PtrToInt+Sub+SDiv 生成指针差值
--pointer - pointer 正确除以 sizeof(pointee) 得到元素个数差

3. ✅ EmitAssignment 成员赋值 — 已修复（P1-1 ✅）
--EmitAssignment 对 MemberExpr 使用 EmitLValue() 获取地址，正确 store
--EmitLValue(MemberExpr) 返回 GEP 地址（不做 CreateLoad）

4. ✅ EmitCompoundAssignment 全局变量回写 — 已修复（P1-2 ✅）
--三级 store-back：局部变量(StoreLocalVar) → 全局变量(GetGlobalVar) → EmitLValue
--成员变量和数组下标的复合赋值均已处理

5. ✅ EmitIncDec 全局变量和成员 — 已修复（P1-3 ✅）
--与 EmitCompoundAssignment 相同的三级策略
--全局变量和成员的 ++/-- 已处理

6. ⚠️ new[] / delete[] 未处理（P2）
--AST 层已有 ArraySize 和 IsArray 字段，Parse 层已解析
--EmitCXXNewExpr 完全忽略 getArraySize()，始终按单元素大小分配
--EmitCXXDeleteExpr 不检查 isArrayForm()，缺少数组析构循环

7. ✅ 构造函数调用 — 已修复（Stage 6.4 ✅）
--EmitCXXConstructExpr 真正调用 CreateCall(CtorFn, {this, args...})
--基类/成员初始化器也已实现（CGCXX.cpp EmitBaseInitializer/EmitMemberInitializer）
--遗留：缺 in-place 构造（new T(args) 应在 malloc 地址上构造）、copy elision（P2）

8. ⚠️ 异常处理 — 框架已搭建，throw 严重简化（P2）
--✅ try/catch 有完整 invoke + landingpad + resume 框架（CodeGenStmt.cpp）
--✅ EmitCallExpr 在 try 块内生成 invoke 而非 call
--⚠️ throw 仍为 unreachable，不调用 __cxa_allocate_exception / __cxa_throw
--⚠️ catch 无类型匹配（所有 catch(T e) 当 catch-all 处理），无 RTTI dispatch
--⚠️ 构造函数调用（EmitCXXConstructExpr）不走 invoke

## -----------------------------------------------------------

## SwitchStmt 对比 Clang

1. ✅ 使用 llvm::SwitchInst — 与 Clang 类似
--Clang 也使用 SwitchInst 生成跳转表

2. ✅ case 表达式重复求值 — 已修复（P1-8 ✅）
--第一遍使用 CGM.getConstants().EmitConstant() 获取常量值（无副作用）
--使用 DenseMap 缓存 CaseValue → BasicBlock 映射，第二遍直接查找

3. ⚠️ 未处理非 CompoundStmt 的 switch body
--如果 switch body 不是 CompoundStmt（如直接跟 case），当前不处理（P2）

4. ⚠️ 未处理 GNU case range (CaseStmt::getRHS)
--CaseStmt 有 RHS 字段支持 case 1...5 语法，当前只使用 LHS（P2）

## -----------------------------------------------------------

## 控制流对比 Clang

1. ✅ for 循环结构 — 与 Clang 一致
--Init → CondBB → BodyBB → IncBB → CondBB 循环

2. ✅ while 循环结构 — 与 Clang 一致

3. ✅ do-while 循环结构 — 与 Clang 一致

4. ⚠️ GotoStmt 未真正实现
--缺少 Label → BasicBlock 映射表
--当前 goto 只是创建一个未连接的 target 块（P1）

5. ⚠️ EmitIfStmt 缺少条件变量支持
--Clang 支持 if (int x = expr) 语法
--BlockType 的 IfStmt 有 CondVar 字段但未处理（P2）

6. ⚠️ EmitCXXTryStmt 完全简化
--不生成 invoke 指令，catch 块直接顺序执行
--无异常处理能力（P2）

## -----------------------------------------------------------

## 通用问题

1. ⚠️ 缺少 ExprValueKind 感知
--AST 定义了 VK_PRValue/VK_LValue/VK_XValue
--CodeGen 未根据 value kind 区分 lvalue/rvalue 语义
--所有表达式统一返回 llvm::Value*，lvalue/rvalue 判断靠上下文（P1）

2. ⚠️ 枚举常量类型硬编码为 i32
--EmitDeclRefExpr 中 EnumConstantDecl → ConstantInt::get(Int32Ty, ...)
--应使用枚举的底层类型（P1）

3. ⚠️ 结构体返回值未处理
--大型结构体按值返回应通过 sret 参数
--当前直接 load/store 整个结构体（P2）

## ---------------------------------------------------------------
## C. 关联关系错误
## ---------------------------------------------------------------

1. ✅ EmitLogicalAnd/EmitLogicalOr 的 getParent() 问题已修复
--使用 getCurrentBlock() 在 CreateICmpNE 之前保存基本块

2. ✅ getPointeeType() 问题已修复
--使用 llvm::dyn_cast<PointerType> 获取被指类型

3. ✅ CreateAlloca 的 IRBuilder 构造函数问题已修复
--使用 saveIP/restoreIP 模式

4. ✅ TargetInfo 不完整类型已修复
--CodeGenExpr.cpp 已添加 #include "TargetInfo.h"

5. ✅ EmitCXXNewExpr 类型大小问题已修复
--从 T* 类型中提取 T 的大小，而非指针大小

6. ✅ EmitMemberExpr 返回值语义 — 已修复（P0 ✅）
--EmitMemberExpr 已重写：-> 使用 EmitExpr(base) 获取指针，. 使用 EmitLValue(base) 获取地址
--GEP 计算后对 rvalue 做 CreateLoad，EmitLValue(MemberExpr) 返回 GEP 地址

7. ✅ EmitLValue(MemberExpr) GEP source type — 已修复（P1-7 ✅）
--已使用正确的结构体类型（从 base 类型推导）作为 GEP source type
--不再使用 Field->getType()（字段类型）

8. ✅ EmitSwitchStmt case 表达式重复 EmitExpr — 已修复（P1-8 ✅）
--第一遍使用 CGM.getConstants().EmitConstant() 获取常量值（无副作用）
--使用 DenseMap 缓存，第二遍直接查找 BasicBlock

9. ✅ EmitDeclRefExpr 枚举底层类型 — 已修复（P1-9 ✅）
--使用 ConvertType(EnumConstant->getType()) 替代硬编码 Int32Ty
--通过 ConvertEnumType 正确解析枚举底层类型

## ---------------------------------------------------------------
## D. 汇总

## P0 问题（必须立即修复）— 1 个 ✅ 全部已修复
1. ✅ EmitMemberExpr 返回值语义混乱（已修复）
--EmitMemberExpr 已重写：-> 使用 EmitExpr(base) 获取指针，. 使用 EmitLValue(base) 获取地址
--GEP 计算后对 rvalue 做 CreateLoad，EmitLValue(MemberExpr) 返回 GEP 地址

## P1 问题（应尽快修复）— 10 个 ✅ 全部已修复
1. ✅ EmitAssignment 中 MemberExpr 赋值路径（已修复）
2. ✅ EmitCompoundAssignment 不处理全局变量回写（已修复）
3. ✅ EmitIncDec 不处理全局变量和成员变量（已修复）
4. ✅ 缺少 DerivedToBase / BaseToDerived 类型转换（已修复 — CastKind 扩展）
5. ✅ 缺少 LValueToRValue 隐式转换处理（已修复 — CastKind 扩展）
6. ✅ 缺少整数提升（integral promotion）（已修复 — EmitScalarConversion 替代 promoteToInt32，覆盖 Binary/CompoundAssign/Unary）
7. ✅ EmitLValue(MemberExpr) GEP source type 错误（已修复）
8. ✅ EmitSwitchStmt case 表达式重复 EmitExpr（已修复 — DenseMap 缓存）
9. ✅ EmitDeclRefExpr 枚举常量类型硬编码为 i32（已修复 — ConvertType 解析底层类型）
10. ✅ this 指针对 obj.method() 处理不完整（已修复 — EmitLValue 分支）

## P2 问题（后续改进）— 16 个（5 个已修复）
1. 缺少 EmitExprForStore() 接口（文档定义但未实现）
2. ✅ Spaceship (<=>) 运算符已实现（整数/浮点三路比较，返回 -1/0/1）
3. ✅ 函数调用属性已设置（[[noreturn]] 解析 + 函数级/调用点属性）
4. ✅ 参数隐式类型转换已处理（EmitScalarConversion + 变参 default argument promotion）
5. ✅ 变参函数已处理（isVariadic + emitDefaultArgPromotion: float→double, <i32→int）
6. dynamic_cast 运行时检查未实现
7. new[] / delete[] 未处理
8. 构造函数调用未真正生成
9. 异常处理严重简化
10. Alloca 插入点效率优化（使用 AllocaInsertPt 成员）
11. 条件表达式 bool 转换精度
12. 缺少 Cleanups 栈
13. ✅ 指针差值已实现（pointer - pointer → ptrdiff_t，Sema+CodeGen 两层修复）
14. GotoStmt 未真正实现（缺少 Label 映射表）
15. EmitCXXTryStmt 完全简化
16. 整数提升 EmitScalarConversion 全面覆盖（BinaryOp/CompoundAssign/Unary）
