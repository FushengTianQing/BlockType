## 用户需求

用户在上一次 new/delete 类型系统重构中发现 AST 相关功能被分散在 Parse/Sema/CodeGen 多个模块中，要求调研分析并给出重构方案，使各模块职责回归编译器标准分层架构。

## 调研发现

### 当前 AST 职责分布（不合理）

| 模块 | 当前职责 | 问题 |
| --- | --- | --- |
| AST/ | 纯数据结构定义、内存管理、类型工厂 | 正确 |
| Parse/ | 解析 + **直接创建所有 AST 节点**（155+ 处 Context.create），**不设置 ExprTy** | 设计偏差 |
| Sema/ | ActOn 方法（创建+类型化）、ProcessAST | 存在但未被调用（死代码） |
| **CodeGen/** | IR 生成 + **修改 AST 节点（setType）** + **完整 AST 遍历** | **严重越权** |


### 识别的 5 个问题

1. **P0-严重**：`CodeGenModule::SemaPostProcessAST()` 调用 `setType()` 修改 AST 节点（编译器分层中 CodeGen 应是 AST 的纯消费者）
2. **P1-中等**：Sema 和 CodeGen 各有一套几乎相同的 AST 遍历逻辑（SemaVisitExpr vs ASTVisitor），需同步维护
3. **P2-设计偏差**：Parser 155+ 处 `Context.create<T>()` 直接创建节点，不经过 Sema（本次不处理，留作后续阶段）
4. **P3-功能缺失**：driver.cpp 未创建 Sema 实例，`Sema::ProcessAST()` 永远不执行
5. **P4-分散**：CXXNewExpr 的类型设置分散在 Parser（AllocatedType）、Sema（ExprTy=空）、CodeGen（ExprTy=T*）

### 当前数据流（有问题）

```
Parser → Context.create<CXXNewExpr>(...) → ExprTy=null
CodeGen::SemaPostProcessAST → setType(T*)  ← 越权！
CodeGen::EmitCXXNewExpr → 读取 getType()
```

### 目标数据流

```
Parser → Context.create<CXXNewExpr>(..., AllocatedType)
Sema::ProcessAST → setType(T*)             ← 正确的位置
CodeGen::EmitCXXNewExpr → 只读 getType()    ← 纯消费者
```

## 重构范围

- 本次聚焦：消除 CodeGen 对 AST 的修改（问题 1），用 Sema::ProcessAST 替代 CodeGen::SemaPostProcessAST（问题 2/3/4）
- 不涉及：Parser 委托 Sema 创建节点（问题 2 的根因，155+ 处修改，留作后续阶段）

## 技术栈

C++ 编译器项目，使用 LLVM 基础设施（LLVM IR、BumpPtrAllocator、raw_ostream 等）。

## 实现方案

### 核心策略：将 Sema 层从"死代码"激活为编译流程的必要阶段

**原理**：`Sema::ProcessAST(TU)` 已经存在且功能完整（ASTVisitor 遍历 + ActOnCXXNewExpr/DeleteExpr），只是 driver 未创建 Sema 实例导致从未执行。`CodeGenModule::SemaPostProcessAST` 是为了弥补这一空缺而在错误位置添加的 hack。

**方案**：

1. 从 `CodeGenModule` 中删除 `SemaPostProcessAST`/`SemaVisitStmt`/`SemaVisitExpr` 三个方法
2. 在 `EmitTranslationUnit` 中移除 `SemaPostProcessAST(TU)` 调用
3. 在 driver.cpp 编译流程中插入 Sema 阶段：创建 `Sema(Context, Diags)` 并调用 `ProcessAST(TU)`
4. `EmitTranslationUnit` 增加防御性断言：检测到未设置 ExprTy 的 CXXNewExpr 时输出警告（不阻断，保持兼容）

### 性能影响

无性能退化：Sema::ProcessAST 和原来的 CodeGen::SemaPostProcessAST 做的遍历工作量完全相同，只是从 CodeGen 移到了 Sema。

### 架构改进

```
修改前：Parser → CodeGen（内含 AST 修改 hack）
修改后：Parser → Sema::ProcessAST → CodeGen（纯消费者）
```

## 修改文件清单

```
include/blocktype/CodeGen/CodeGenModule.h  # [MODIFY] 删除 SemaPostProcessAST/SemaVisitStmt/SemaVisitExpr 声明
src/CodeGen/CodeGenModule.cpp               # [MODIFY] 删除三个方法实现 + 移除 EmitTranslationUnit 中的调用
tools/driver.cpp                            # [MODIFY] 添加 Sema 创建和 ProcessAST 调用
```