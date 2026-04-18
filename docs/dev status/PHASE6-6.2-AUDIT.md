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

8.✅ 指针运算 — 已实现
--pointer ± integer → GEP，正确使用 PointerType::getPointeeType()

9.✅ 一元运算 — 已实现
--Minus(整数/浮点) / Plus / Not(位反) / LNot(逻辑非) / Deref(解引用) / AddrOf(取地址) / PreInc|PreDec|PostInc|PostDec

10.✅ ++/-- 运算 — 已实现（P1-3 ✅ 已修复）
--EmitIncDec: 正确区分 prefix/postfix 返回值
--✅ 局部变量/全局变量/成员变量均已处理

11.✅ 逗号运算符 — 已实现

12.⚠️ Spaceship (<=> , C++20) — 未实现
--BinaryOpKind::Spaceship 存在但 EmitBinaryOperator 未处理（P2）
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

5.⚠️ 未设置调用属性（P2）
--函数级属性（AlwaysInline/DoesNotThrow）已在 GetOrCreateFunctionDecl 中设置
--调用点属性（NoReturn 等）在 CreateCall 时未设置 — Clang 通过 CallBase::addFnAttr 附加
--当前不影响正确性，但缺少 [[noreturn]] 优化和调用约定约束

6.⚠️ 未处理隐式参数转换（P2）
--第 669-674 行直接 EmitExpr(arg) 加入参数列表，无类型检查
--如 int→long、float→double 等提升、派生类指针→基类指针转换均未处理
--风险：当实参类型与形参不匹配时，LLVM 会报类型错误或生成错误的隐式 bitcast

7.⚠️ 未处理变参函数（P2）
--未处理 va_list / ... 参数 — 没有识别 FunctionDecl::isVariadic()
--变参调用需要在 CreateCall 时正确处理可变参数部分的类型
--当前所有测试用例均为固定参数函数

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

8.⚠️ dynamic_cast 未实现运行时检查
--当前只是 bitcast，缺少 null check（文档提到需要）（P2）

9.✅ DerivedToBase / BaseToDerived 转换 — 已实现（P1-4 ✅ 已修复）
--CastKind 已扩展：DerivedToBase 使用偏移量 GEP，BaseToDerived 使用 BitCast

10.✅ LValueToRValue 转换 — 已实现（P1-5 ✅ 已修复）
--CastKind::LValueToRValue 在 EmitCastExpr 中生成 CreateLoad

## -----------------------------------------------------------

## 额外实现（文档未要求但已添加）：

1.✅ C++ 表达式 — 已实现
--EmitCXXThisExpr: 返回第一个参数
--EmitCXXConstructExpr: alloca + 零初始化
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

1. ⚠️ 缺少整数提升 (integral promotion)
--Clang 在 EmitBinaryOperator 中先做 usual arithmetic conversions
--BlockType 假设操作数类型已匹配（P1）
--例如：short + int 应将 short 提升到 int，当前未处理

2. ⚠️ 指针差值 (pointer - pointer → ptrdiff_t) 未实现
--只有 pointer ± integer，缺少 pointer - pointer（P2）

3. ⚠️ EmitAssignment 成员赋值不完整
--MemberExpr 的 lvalue 路径需要返回地址而非值
--当前 EmitMemberExpr 返回的是加载后的值，不是地址（P1）

4. ⚠️ EmitCompoundAssignment 全局变量回写未处理
--只处理了 DeclRefExpr→VarDecl→StoreLocalVar 路径
--全局变量和成员变量的复合赋值未处理（P1）

5. ✅ EmitIncDec 全局变量和成员 — 已修复（P1-3 ✅）
--与 EmitCompoundAssignment 相同的三级策略
--全局变量和成员的 ++/-- 已处理

6. ⚠️ new[] / delete[] 未处理
--只有 new T 和 delete p，缺少 new T[n] 和 delete[] p（P2）

7. ⚠️ 构造函数调用未真正生成
--EmitCXXConstructExpr 只是零初始化，不调用构造函数体（P2，Stage 6.4）

8. ⚠️ 异常处理严重简化
--throw 只生成 unreachable，catch 完全无效
--缺少 invoke/landingpad/resume 指令（P2，Stage 6.5）

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
6. ✅ 缺少整数提升（integral promotion）（已修复 — promoteToInt32 lambda）
7. ✅ EmitLValue(MemberExpr) GEP source type 错误（已修复）
8. ✅ EmitSwitchStmt case 表达式重复 EmitExpr（已修复 — DenseMap 缓存）
9. ✅ EmitDeclRefExpr 枚举常量类型硬编码为 i32（已修复 — ConvertType 解析底层类型）
10. ✅ this 指针对 obj.method() 处理不完整（已修复 — EmitLValue 分支）

## P2 问题（后续改进）— 15 个（1 个已修复）
1. 缺少 EmitExprForStore() 接口（文档定义但未实现）
2. Spaceship (<=>) 运算符未处理
3. 函数调用属性（NoReturn/NoInline）未设置
4. ✅ 参数隐式类型转换已处理（EmitScalarConversion）
5. 变参函数未处理
6. dynamic_cast 运行时检查未实现
7. new[] / delete[] 未处理
8. 构造函数调用未真正生成
9. 异常处理严重简化
10. Alloca 插入点效率优化（使用 AllocaInsertPt 成员）
11. 条件表达式 bool 转换精度
12. 缺少 Cleanups 栈
13. 指针差值未实现
14. GotoStmt 未真正实现（缺少 Label 映射表）
15. EmitCXXTryStmt 完全简化
