# Phase C-C3 优化版：IRToLLVMConverter 值/指令转换

> 基于 PhaseC-Planner-Report 修复后生成
> 修复内容：补充缺失 Opcode 映射

---

## 依赖关系

- C.2（类型转换）

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/IRToLLVMConverter.cpp` |

---

## convertInstruction 完整映射表

### 终结指令

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::Ret | `Builder.CreateRet(mapValue(Val))` | RetVoid: `CreateRetVoid()` |
| Opcode::Br | `Builder.CreateBr(mapBasicBlock(Dest))` | — |
| Opcode::CondBr | `Builder.CreateCondBr(mapValue(Cond), TrueBB, FalseBB)` | — |
| Opcode::Switch | `Builder.CreateSwitch(mapValue(Val), Default, NumCases)` | — |
| Opcode::Invoke | `Builder.CreateInvoke(Callee, NormalBB, UnwindBB, Args)` | — |
| Opcode::Unreachable | `Builder.CreateUnreachable()` | — |
| Opcode::Resume | `Builder.CreateResume(mapValue(Val))` | 异常恢复 |

### 整数二元

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::Add | `Builder.CreateAdd(LHS, RHS)` | 需检查 nuw/nsw 标志 |
| Opcode::Sub | `Builder.CreateSub(LHS, RHS)` | 同上 |
| Opcode::Mul | `Builder.CreateMul(LHS, RHS)` | 同上 |
| Opcode::UDiv | `Builder.CreateUDiv(LHS, RHS)` | 需检查 exact 标志 |
| Opcode::SDiv | `Builder.CreateSDiv(LHS, RHS)` | 同上 |
| Opcode::URem | `Builder.CreateURem(LHS, RHS)` | — |
| Opcode::SRem | `Builder.CreateSRem(LHS, RHS)` | — |

### 浮点二元

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::FAdd | `Builder.CreateFAdd(LHS, RHS)` | 需检查 fast-math 标志 |
| Opcode::FSub | `Builder.CreateFSub(LHS, RHS)` | 同上 |
| Opcode::FMul | `Builder.CreateFMul(LHS, RHS)` | 同上 |
| Opcode::FDiv | `Builder.CreateFDiv(LHS, RHS)` | 同上 |
| Opcode::FRem | `Builder.CreateFRem(LHS, RHS)` | 同上 |

### 位运算

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::Shl | `Builder.CreateShl(LHS, RHS)` | — |
| Opcode::LShr | `Builder.CreateLShr(LHS, RHS)` | — |
| Opcode::AShr | `Builder.CreateAShr(LHS, RHS)` | — |
| Opcode::And | `Builder.CreateAnd(LHS, RHS)` | — |
| Opcode::Or | `Builder.CreateOr(LHS, RHS)` | — |
| Opcode::Xor | `Builder.CreateXor(LHS, RHS)` | — |

### 内存

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::Alloca | `Builder.CreateAlloca(mapType(ElemTy), Align)` | 需获取对齐 |
| Opcode::Load | `Builder.CreateLoad(mapType(ElemTy), mapValue(Ptr), Align)` | volatile 标志 |
| Opcode::Store | `Builder.CreateStore(mapValue(Val), mapValue(Ptr), Align)` | volatile 标志 |
| Opcode::GEP | `Builder.CreateGEP(mapType(SourceTy), mapValue(Ptr), Indices)` | 需获取 source type |
| Opcode::Memcpy | `Builder.CreateMemCpy(Dst, DstAlign, Src, SrcAlign, Size)` | — |
| Opcode::Memset | `Builder.CreateMemSet(Ptr, Val, Size, Align)` | — |

### 转换

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::Trunc | `Builder.CreateTrunc(mapValue(V), DstTy)` | — |
| Opcode::ZExt | `Builder.CreateZExt(mapValue(V), DstTy)` | — |
| Opcode::SExt | `Builder.CreateSExt(mapValue(V), DstTy)` | — |
| Opcode::FPTrunc | `Builder.CreateFPTrunc(mapValue(V), DstTy)` | — |
| Opcode::FPExt | `Builder.CreateFPExt(mapValue(V), DstTy)` | — |
| Opcode::FPToSI | `Builder.CreateFPToSI(mapValue(V), DstTy)` | — |
| Opcode::FPToUI | `Builder.CreateFPToUI(mapValue(V), DstTy)` | — |
| Opcode::SIToFP | `Builder.CreateSIToFP(mapValue(V), DstTy)` | — |
| Opcode::UIToFP | `Builder.CreateUIToFP(mapValue(V), DstTy)` | — |
| Opcode::PtrToInt | `Builder.CreatePtrToInt(mapValue(V), DstTy)` | — |
| Opcode::IntToPtr | `Builder.CreateIntToPtr(mapValue(V), DstTy)` | — |
| Opcode::BitCast | `Builder.CreateBitCast(mapValue(V), DstTy)` | — |

### 比较

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::ICmp | `Builder.CreateICmp(Predicate, LHS, RHS)` | 需从 getICmpPredicate() 获取谓词 |
| Opcode::FCmp | `Builder.CreateFCmp(Predicate, LHS, RHS)` | 需从 getFCmpPredicate() 获取谓词 |

### 调用

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::Call | `Builder.CreateCall(mapFunction(Callee), Args)` | 需获取 calling convention |

### 聚合操作

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::Phi | `Builder.CreatePHI(mapType(Ty), NumIncoming)` | 需后续 addIncoming() |
| Opcode::Select | `Builder.CreateSelect(Cond, TrueVal, FalseVal)` | — |
| Opcode::ExtractValue | `Builder.CreateExtractValue(Agg, Indices)` | — |
| Opcode::InsertValue | `Builder.CreateInsertValue(Agg, Val, Indices)` | — |
| Opcode::ExtractElement | `Builder.CreateExtractElement(Vec, Idx)` | — |
| Opcode::InsertElement | `Builder.CreateInsertElement(Vec, Val, Idx)` | — |
| Opcode::ShuffleVector | `Builder.CreateShuffleVector(V1, V2, Mask)` | — |

### 调试信息

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::DbgDeclare | `Builder.CreateDbgDeclare(Variable, Expr)` | 调试信息映射 |
| Opcode::DbgValue | `Builder.CreateDbgValue(Variable, Expr)` | 同上 |
| Opcode::DbgLabel | `Builder.CreateDbgLabel(Label)` | 同上 |

### 原子操作

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::AtomicLoad | `Builder.CreateLoad(Ty, Ptr, Align, Ordering)` | 需获取内存序 |
| Opcode::AtomicStore | `Builder.CreateStore(Val, Ptr, Align, Ordering)` | 同上 |
| Opcode::AtomicRMW | `Builder.CreateAtomicRMW(Op, Ptr, Val, Ordering)` | — |
| Opcode::AtomicCmpXchg | `Builder.CreateAtomicCmpXchg(Ptr, Cmp, New, Ordering)` | — |
| Opcode::Fence | `Builder.CreateFence(Ordering)` | — |

### FFI 指令（C-F3 增强任务引入）

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::FFICall | 映射为 `Builder.CreateCall()`（C ABI 调用） | 需获取 FFI 签名 |
| Opcode::FFICheck | 发出运行时检查调用 | — |
| Opcode::FFICoerce | 类型转换（coerce） | E-F7 实现 |
| Opcode::FFIUnwind | FFI 异常展开 | E-F7 实现 |

### Cpp Dialect 指令（C.8 实现）

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::DynamicCast | 映射为 `__dynamic_cast` 运行时调用 | C.8 实现 |
| Opcode::VtableDispatch | 映射为虚表查找 + 间接调用 | C.8 实现 |
| Opcode::RTTITypeid | 映射为 `__typeid` 运行时调用 | C.8 实现 |

### 未映射 Opcode（远期实现）

以下 Opcode 在 Phase C 中**未实现**，`convertInstruction` 遇到时发出 `diag::warn_ir_opcode_not_supported` 并跳过：
- `TargetIntrinsic` — 目标特定内联函数
- `MetaInlineAlways/MetaInlineNever/MetaHot/MetaCold` — 元数据指令

---

## 实现约束

1. 所有 mapValue 必须缓存
2. Phi 节点需两遍处理：第一遍创建占位，第二遍 addIncoming()
3. 未实现的 Opcode 发出 `diag::warn_ir_opcode_not_supported` 并跳过
4. Cpp Dialect 指令在 C.8 中实现，Phase C 初始版本可发出警告

---

## 验收标准

```cpp
// V1: add 指令转换
// IR: %sum = add i32 %a, %b
// LLVM: %sum = add i32 %a, %b
// mapValue 返回正确的 llvm::Value*

// V2: 函数调用转换
// IR: call @foo(i32 1, i32 2)
// LLVM: call void @foo(i32 1, i32 2)

// V3: 条件分支转换
// IR: condBr %cond, %then, %else
// LLVM: br i1 %cond, label %then, label %else

// V4: 端到端：convert 完整函数体
auto LLVMMod = Converter.convert(IRMod);
assert(LLVMMod != nullptr);
auto* F = LLVMMod->getFunction("add_func");
assert(F != nullptr);
assert(F->size() > 0);  // 有基本块
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| C.2: IRToLLVMConverter 类型框架 | 在此基础上补充指令转换 |
| C.2: mapType() | 指令中类型映射 |
| C.2: TypeMap 缓存 | 值映射依赖类型映射 |

---

> Git 提交：`feat(C): 完成 C.3 — IRToLLVMConverter 值/指令转换`
