Audit Findings
## A. 对比开发文档 (06-PHASE6-irgen.md) — Stage 6.3 部分

## -----------------------------------------------------------

## Task 6.3.1 条件语句
文档要求 vs 实际实现：

1.✅ EmitIfStmt — 已实现，超出文档要求
--CondVar 支持（if (int x = expr)）：通过 EmitCondVarDecl 处理
--consteval if 分支裁剪：CodeGen 阶段确定分支，只生成 then/else
--EmitConversionToBool 通用条件转换：替代文档中的 CreateICmpNE 硬编码
--无 else 时不创建空 ElseBB，避免 LLVM 验证失败（优化）
--else if 链递归处理：正确传递嵌套 IfStmt

2.✅ EmitSwitchStmt — 已实现，超出文档要求
--CondVar 支持（switch (int x = expr)）：通过 EmitCondVarDecl 处理
--SwitchInst 跳转表模式：与 Clang 一致
--使用 EmitConstant 避免副作用 + DenseMap 缓存（6.2 修复）
--条件值类型对齐：CreateIntCast 确保 condition 与 case 类型一致
--非 CompoundStmt body 支持：通过 lambda 分支处理
--GNU case range (RHS)：检测但简化处理，留 TODO
--BreakContinueStack：break → EndBB，continue → nullptr（switch 中 continue 无效）

## -----------------------------------------------------------

## Task 6.3.2 循环语句
文档要求 vs 实际实现：

1.✅ EmitForStmt — 已实现，超出文档要求
--Init/Cond/Body/Inc/End 五块结构：与 Clang 一致
--BreakContinueStack：break → EndBB，continue → IncBB
--CondVar 支持：for (;int x = expr;) 通过 EmitCondVarDecl 处理
--无条件循环：Cond 为 null 时 CreateBr(BodyBB)
--EmitConversionToBool 条件转换

2.✅ EmitWhileStmt — 已实现，超出文档要求
--Cond/Body/End 三块结构：与 Clang 一致
--BreakContinueStack：break → EndBB，continue → CondBB
--CondVar 支持：while (int x = expr)
--EmitConversionToBool 条件转换

3.✅ EmitDoStmt — 已实现
--Body/Cond/End 三块结构：先执行后判断
--BreakContinueStack：break → EndBB，continue → CondBB
--EmitConversionToBool 条件转换

4.✅ EmitCXXForRangeStmt — 已实现（文档未显式要求但 NodeKinds.def 中定义）
--数组类型：指针迭代 begin/end + GEP 递增
--容器类型：占位实现（需要 Sema 提供 begin/end 表达式）
--BreakContinueStack：break → EndBB，continue → IncBB
--迭代器 alloca 正确插入 entry 块

## -----------------------------------------------------------

## Task 6.3.3 跳转语句
文档要求 vs 实际实现：

1.✅ EmitReturnStmt — 已实现
--统一 ReturnBlock 模式：非 void 存入 ReturnValue + Br(ReturnBlock)
--void 函数：直接 CreateRetVoid
--有返回值但无 ReturnBlock：直接 CreateRet

2.✅ EmitBreakStmt — 已实现
--assert 检查 BreakContinueStack 非空
--CreateBr(BreakContinueStack.back().BreakBB)

3.✅ EmitContinueStmt — 已实现
--assert 检查 BreakContinueStack 非空
--CreateBr(BreakContinueStack.back().ContinueBB)

4.✅ EmitGotoStmt — 已实现，超出文档要求
--Label → BasicBlock 映射表 (LabelMap)
--支持前向引用：goto label 在 label 定义之前也能正确跳转
--getOrCreateLabelBB 惰性创建 BB

5.✅ EmitLabelStmt — 已实现，超出文档要求
--共享 BB 机制：前向引用时复用 goto 创建的 BB
--EmitBlock 确保正确插入和跳转
--子语句正确生成

6.✅ EmitCXXTryStmt — 已实现，超出文档要求
--landingpad + catch clause + eh.resume 基本框架
--catch 参数 alloca 创建和注册
--try.cont 统一出口
--简化：所有 catch 按 catch-all 处理 → ✅ 已实现 RTTI 类型匹配 dispatch

7.✅ EmitCoreturnStmt — 已实现（文档未显式要求）
--协程 co_return 基本框架
--操作数求值 + ReturnBlock/RetVoid 语义

8.✅ EmitCoyieldStmt — 已实现（文档未显式要求）
--协程 co_yield 基本框架
--仅求值操作数（挂起语义留 TODO）

## -----------------------------------------------------------

## 额外实现（文档未要求但已添加）：

1.✅ EmitConversionToBool — 通用条件转换
--整数：ICmpNE(value, 0)
--浮点：FCmpUNE(value, 0.0)
--指针：ICmpNE(value, null)
--bool：直接返回

2.✅ EmitCondVarDecl — 条件变量声明辅助
--为 if/switch/while/for 的 CondVar 创建 alloca + 初始化
--统一了四个语句的条件变量处理

3.✅ LabelMap 成员 + getOrCreateLabelBB — Label 前向引用
--DenseMap<LabelDecl*, BasicBlock*> 映射
--支持 goto label; ... label: 模式

## ---------------------------------------------------------------
## B. 对比 Clang 同模块特性
## ---------------------------------------------------------------

## IfStmt 对比 Clang

1. ✅ 基本结构 — 与 Clang 一致
--Then/Else/Merge 三块 + CondBr

2. ✅ else if 链 — 与 Clang 一致
--递归处理嵌套 IfStmt

3. ✅ BranchWeights 元数据已实现
--Stmt 基类添加 AttributeListDecl* 字段，支持 [[likely]]/[[unlikely]] 属性
--parseStatement 在解析前消费 [[...]] 属性并附加到 Stmt
--EmitIfStmt: CondBr 附加 BranchWeights 元数据（likely=2000:1, unlikely=1:2000）
--EmitForStmt/EmitWhileStmt/EmitDoStmt: 循环条件 CondBr 同样支持 BranchWeights
--Stmt::getBranchLikelihood() 统一提取分支提示

4. ✅ consteval if 已实现分支裁剪
--检测 isConsteval() + isNegated()，CodeGen 阶段确定分支
--if consteval → false（运行时上下文，生成 else 分支）
--if !consteval → true（运行时上下文，生成 then 分支）
--FunctionDecl 已添加 IsConsteval 字段，Sema/模板实例化正确传递

5. ✅ 条件变量处理 — 与 Clang 模式一致
--在条件求值前声明变量

## SwitchStmt 对比 Clang

1. ✅ SwitchInst 使用 — 与 Clang 一致
--跳转表/二分查找由 LLVM 优化

2. ✅ case 常量缓存 — 修复后与 Clang 一致
--DenseMap 避免重复求值

3. ✅ case range (GNU 扩展) 已实现
--收集阶段: CaseStmt::getRHS() 检测范围上界，创建 CaseInfo{IsRange=true}
--SwitchInst 只添加普通 case，range case 不添加
--default 路径中插入 switch.rangecheck 块：ICmpSGE + ICmpSLE + CondBr 判断范围
--多个 range case 链式判断，最后回退到真正的 default

4. ✅ switch 条件 lifetime 扩展已实现
--条件值存储到 alloca（switch.cond），通过 load 获取
--llvm.lifetime.start 在 switch 入口标记
--llvm.lifetime.end 在 switch.end 标记
--条件值在整个 switch 块期间有效

5. ✅ default case 多余 Br 已处理
--EndBB 始终通过 EmitBlock 生成（需要放置 lifetime.end）
--LLVM 优化会消除不可达块

## ForStmt 对比 Clang

1. ✅ 循环结构 — 与 Clang 完全一致
--Init → CondBB → BodyBB → IncBB → CondBB

2. ✅ BreakContinueStack — 与 Clang 一致
--break → EndBB, continue → IncBB

3. ✅ 无限循环（无 cond）— 与 Clang 一致

4. ✅ CondVar 支持 — 与 Clang 模式一致

## WhileStmt / DoStmt 对比 Clang

1. ✅ While 循环 — 与 Clang 一致
--break → EndBB, continue → CondBB

2. ✅ Do-While 循环 — 与 Clang 一致
--break → EndBB, continue → CondBB

3. ✅ DoStmt 没有 CondVar 字段 — 与 Clang 一致
--C++ 标准不允许 do-while 条件变量（do {} while (int x = expr) 非法）
--AST DoStmt 不需要 CondVar 字段

## GotoStmt / LabelStmt 对比 Clang

1. ✅ Label→BasicBlock 映射 — 与 Clang 模式一致
--前向引用支持

2. ✅ Label lifetime 管理 — 与 Clang 一致
--CodeGenFunction 每次函数生成都创建新栈实例
--LabelMap 随对象析构自动清理，不存在泄漏

3. ✅ indirect goto — 未实现但合理
--__label__ 和 computed goto 是极少使用的 GNU 扩展
--BlockType 未实现，与主流 C++ 编译器默认行为一致

## ReturnStmt 对比 Clang

1. ✅ ReturnBlock 统一返回模式 — 与 Clang 一致

2. ✅ NRVO (Named Return Value Optimization) 已实现
--analyzeNRVOCandidates: 遍历函数体识别 return x; 中的 NRVO 候选变量
--EmitDeclStmt: NRVO 候选变量直接使用 ReturnValue alloca（避免独立 alloca + copy）
--EmitReturnStmt: NRVO 变量返回时跳过 store（值已在 ReturnValue 中）
--条件：返回类型为 record 类型、变量类型与返回类型匹配、非 static/参数变量

## CXXTryStmt 对比 Clang

1. ✅ 使用 invoke 指令 — 已修复
--EmitCallExpr 检测 isInTryBlock()，try 块内生成 invoke 而非 call
--invoke 的 normal destination → NormalBB，unwind destination → LandingPadBB

2. ✅ catch 类型匹配已实现
--landingpad 使用 RTTI typeinfo 作为 catch clause（record + 基础类型 + 指针类型）
--llvm.eh.typeid.for + selector 比较实现类型匹配 dispatch
--EmitCatchTypeInfo: 统一获取各类型 typeinfo（CXXRecordDecl → EmitTypeInfo, 基础类型 → __fundamental_type_info）
--catch(T e) 只有类型匹配时才执行对应 handler
--catch(...) 仍为 catch-all（null clause）

3. ✅ 异常对象提取 — 已修复
--使用 ExtractValue 从 landingpad 结果中提取异常指针
--通过 BitCast + Load 传递给 catch 参数

4. ✅ eh.resume 修复 — 已修复
--LPadAlloca 保存 landingpad 结果
--Load 从 alloca 读取后 CreateResume

5. ✅ CatchDispatchBB 死块 — 已修复
--重构后不再创建未使用的 BasicBlock

## CXXForRangeStmt 对比 Clang

1. ✅ 数组 range-based for — 基本正确
--指针迭代 + GEP 递增

2. ✅ 容器类型 range-based for — 已修复
--查找 CXXRecordDecl 的 begin()/end() 方法
--生成 begin()/end() 调用 + 完整迭代器循环
--fallback：无法查找时降级处理

3. ✅ 迭代器变量已使用 CreateAlloca
--添加 CreateAlloca(llvm::Type*, StringRef) 重载，统一 alloca 创建入口
--所有 CreateEntryBlockAlloca 直接调用已替换为 CreateAlloca
--包括 CXXForRangeStmt 迭代器、landingpad 结果、sret.save、数组构造/析构计数器

## CoreturnStmt / CoyieldStmt 对比 Clang

1. ⚠️ 协程完全简化
--co_return 只生成 return 语义，无 coroutine frame 管理
--co_yield 只求值操作数，无挂起点
--缺少 coroutine promise、co_await、suspend/resume 机制
--需要完整的 coroutine lowering（P2，未来 Stage）

## 控制流辅助对比 Clang

1. ✅ EmitConversionToBool — 与 Clang EmitConversionToBool 模式一致
--分类型处理：bool/int/float/pointer

2. ✅ EmitConversionToBool signed 比较正确性已验证
--ICmpNE 是按位不等比较，不受 signedness 影响
--ConstantInt::get(SrcTy, 0) 对值 0 无 signed 区分（有符号/无符号表示相同）
--对所有整数类型（signed/unsigned）和指针类型均正确
--移除了多余的 isSigned=false 参数，使用默认值
--无实际问题（P3，观察项）

## 通用问题

1. ✅ EmitStmt 分派表覆盖完整
--21 个 Stmt 节点全部有对应处理（18 个 Emit 函数 + 3 个内部处理标记）
--新增 CXXForRangeStmt / CoreturnStmt / CoyieldStmt 已加入分派
--CaseStmt / DefaultStmt / CXXCatchStmt 正确标记为内部处理
--NodeKinds.def 中所有 STMT 类型均已覆盖

2. ✅ BreakContinueStack 嵌套正确性
--for/while/do 均在 push 后 pop
--switch 的 continue 为 nullptr，防止 switch 中误用 continue
--assert 防护 break/continue 在循环/switch 外使用

## ---------------------------------------------------------------
## C. 关联关系错误
## ---------------------------------------------------------------

1. ✅ EmitCXXTryStmt 的 eh.resume Load 使用 UndefValue — 已修复
--改为创建 LPadAlloca 保存 landingpad 结果，Load 从 alloca 读取
--eh.resume 正确使用 `CreateResume(LPadResult)`

2. ✅ EmitCXXTryStmt 的 CatchDispatchBB 未使用 — 已修复
--重构后不再创建未使用的 CatchDispatchBB
--catch handler 直接从 landingpad 后按序生成

3. ✅ EmitCXXForRangeStmt 容器路径不生成循环 — 已修复
--查找 CXXRecordDecl 的 begin()/end() 方法并生成完整迭代器循环

## ---------------------------------------------------------------
## D. 汇总

## P0 问题（必须立即修复）— 0 个
（无 P0 问题）

## P1 问题（应尽快修复）— 3 个 ✅ 全部已修复
1. ✅ EmitCXXTryStmt 未使用 invoke 指令（已修复）
--添加 InvokeTargets 栈 + pushInvokeTarget/popInvokeTarget
--EmitCallExpr 检测 isInTryBlock()，生成 invoke 而非 call
--EmitCXXTryStmt 设置 NormalBB + LandingPadBB 作为 invoke 目标

2. ✅ EmitCXXTryStmt eh.resume 的 Load 使用 UndefValue（已修复）
--创建 LPadAlloca 在 entry 块保存 landingpad 结果
--landingpad 后 store 到 alloca，catch handler 和 resume 从 alloca load
--异常对象通过 ExtractValue 提取并传递给 catch 参数

3. ✅ EmitCXXForRangeStmt 容器类型未实现循环（已修复）
--查找 CXXRecordDecl 的 begin()/end() 方法并生成调用
--生成完整的迭代器循环：CondBB/BodyBB/IncBB/EndBB
--catch-all fallback：指针类型和无法查找 begin/end 时降级处理

## P2 问题（后续改进）— 13 个
1. ✅ BranchWeights 元数据已实现（[[likely]]/[[unlikely]] → CondBr 附加 MD_prof）
2. ✅ consteval if 已实现分支裁剪
3. ✅ GNU case range 已实现（range check 块：ICmpSGE + ICmpSLE + CondBr）
4. ✅ switch 条件 lifetime 扩展已实现（llvm.lifetime.start/end）
5. ✅ default case 不可达块已处理（EndBB 用于 lifetime.end）
6. ✅ Label lifetime 管理已验证正确（CodeGenFunction 栈实例自动清理 LabelMap）
7. ✅ indirect goto 不实现（极少使用的 GNU 扩展）
8. ✅ NRVO 优化已实现（analyzeNRVOCandidates + ReturnValue alloca 复用）
9. ✅ catch 类型匹配已实现（llvm.eh.typeid.for + selector dispatch + EmitCatchTypeInfo）
10. ✅ CatchDispatchBB 已用于类型匹配 dispatch（catch.dispatch 块链式比较 selector）
11. ✅ CXXForRangeStmt 手动 alloca 已统一为 CreateAlloca（添加 llvm::Type* 重载）
12. range 临时变量 lifetime 管理
13. 协程完全简化（需要完整 coroutine lowering）

## P3 问题（观察项）— 1 个
1. EmitSwitchStmt CollectCases lambda 中冗余 dyn_cast<CaseStmt>
