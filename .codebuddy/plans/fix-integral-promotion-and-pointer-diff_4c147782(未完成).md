---
name: fix-integral-promotion-and-pointer-diff
overview: 彻底解决整数提升和指针差值两个 CodeGen/Sema 问题：1) 将硬编码 promoteToInt32 替换为基于 BinaryOp->getType() 的通用提升，并在 EmitCompoundAssignment/EmitUnaryOperator 中复用 EmitScalarConversion；2) Sema 层为 pointer-pointer Sub 返回 ptrdiff_t，CodeGen 层新增 pointer-pointer 分支生成 CreatePtrDiff。
todos:
  - id: sema-ptr-arith
    content: 修复 Sema getBinaryOperatorResultType 指针运算类型分发
    status: pending
  - id: codegen-int-promotion
    content: 修复 EmitBinaryOperator 整数提升（复用 EmitScalarConversion）
    status: pending
  - id: codegen-ptr-diff
    content: 新增 EmitBinaryOperator pointer-pointer 差值分支
    status: pending
  - id: codegen-compound-promotion
    content: 修复 EmitCompoundAssignment RHS 类型提升
    status: pending
  - id: codegen-unary-promotion
    content: 修复 EmitUnaryOperator Minus/Not 类型提升
    status: pending
  - id: add-tests
    content: 新增 lit 测试覆盖所有修复场景
    status: pending
    dependencies:
      - sema-ptr-arith
      - codegen-int-promotion
      - codegen-ptr-diff
      - codegen-compound-promotion
      - codegen-unary-promotion
  - id: build-and-test
    content: 编译并运行全量测试验证无回归
    status: pending
    dependencies:
      - add-tests
  - id: update-audit
    content: 更新审计文档条目 1 和 2 状态
    status: pending
    dependencies:
      - build-and-test
---

## 核心需求

彻底修复 CodeGen 层的两个遗留问题：

### 问题 1: 整数提升 (integral promotion)

- `EmitBinaryOperator` 的 `promoteToInt32` lambda 硬编码 i32，不使用 Sema 推导的公共类型
- `EmitCompoundAssignment` 和 `EmitUnaryOperator` 完全无类型提升
- 已有 `EmitScalarConversion` 函数能力完备但未被复用
- Sema 层类型推导正确，`BinaryOp->getType()` 已包含正确的公共类型

### 问题 2: 指针差值 (pointer - pointer)

- Sema 层 `getBinaryOperatorResultType` 对指针运算无特殊分发，`getCommonType` 对两指针返回 T1
- CodeGen 层仅有 `pointer ± integer`，无 `pointer - pointer` 分支
- 需返回 `ptrdiff_t`（long 类型）并正确除以元素大小

### 预期效果

- `short + long` 运算数正确提升到 i64（非 i32）
- `short x += int_y` 的 RHS 先提升、运算后截断回 short
- `-short_val` 和 `~short_val` 提升到 int 后运算
- `int *p1, *p2; p1 - p2` 返回 ptrdiff_t（i64），值为字节差 / sizeof(int)

## 技术方案

### 核心策略：复用 EmitScalarConversion

Sema 层已正确推导类型，`BinaryOp->getType()` 包含正确的公共类型。CodeGen 层只需复用已有的 `EmitScalarConversion` 将操作数转换到目标类型即可，无需重新实现提升逻辑。

### 修改范围

#### 文件 1: `src/Sema/TypeCheck.cpp` — 指针运算类型推导

**`getBinaryOperatorResultType`（第 416-421 行）**：在 `return getCommonType(LHS, RHS)` 之前，增加指针运算特殊分发：

```
if Op == Add:
  - pointer + integer → return LHS (指针类型)
  - integer + pointer → return RHS (指针类型)
if Op == Sub:
  - pointer - integer → return LHS (指针类型)
  - pointer - pointer → return Context.getLongType() (ptrdiff_t)
```

这样 `BinaryOp->getType()` 在 CodeGen 层就包含正确的类型信息。

#### 文件 2: `src/CodeGen/CodeGenExpr.cpp` — 4 处修改

**修改 1: `EmitBinaryOperator` 整数提升（第 369-385 行）**

删除硬编码的 `promoteToInt32` lambda 及其调用。在 EmitExpr 获取操作数值之后、指针运算判断之前，对整数运算数用 `EmitScalarConversion` 转换到 `BinaryOp->getType()` 推导的公共类型：

```cpp
// 替换 promoteToInt32 lambda
QualType LHSType = BinaryOp->getLHS()->getType();
QualType RHSType = BinaryOp->getRHS()->getType();
if (LeftHandSide->getType()->isIntegerTy() && RightHandSide->getType()->isIntegerTy()) {
  llvm::Type *ResultLLVMTy = CGM.getTypes().ConvertType(ResultType);
  if (ResultLLVMTy && LeftHandSide->getType() != ResultLLVMTy) {
    LeftHandSide = EmitScalarConversion(LeftHandSide, LHSType, ResultType);
  }
  if (ResultLLVMTy && RightHandSide->getType() != ResultLLVMTy) {
    RightHandSide = EmitScalarConversion(RightHandSide, RHSType, ResultType);
  }
}
```

同时移除冗余的 `!isPointerTy()` 检查（整数类型不可能是指针类型）。

**修改 2: `EmitBinaryOperator` 指针差值（第 387-408 行之后）**

在 `pointer ± integer` 分支之后、`ResultType->isIntegerType()` 分支之前，新增 `pointer - pointer` 分支：

```cpp
// 指针差值: pointer - pointer → ptrdiff_t
if (LeftHandSide->getType()->isPointerTy() &&
    RightHandSide->getType()->isPointerTy() &&
    Opcode == BinaryOpKind::Sub) {
  // (ptrtoint p1) - (ptrtoint p2) → 字节差
  llvm::Type *PtrDiffTy = llvm::Type::getInt64Ty(CGM.getLLVMContext());
  llvm::Value *LHSInt = Builder.CreatePtrToInt(LeftHandSide, PtrDiffTy, "pdiff.l");
  llvm::Value *RHSInt = Builder.CreatePtrToInt(RightHandSide, PtrDiffTy, "pdiff.r");
  llvm::Value *ByteDiff = Builder.CreateSub(LHSInt, RHSInt, "pdiff.bytes");
  // 除以元素大小得到元素个数差
  QualType PointeeType;
  if (auto *PtrType = llvm::dyn_cast<PointerType>(
          BinaryOp->getLHS()->getType().getTypePtr())) {
    PointeeType = QualType(PtrType->getPointeeType(), Qualifier::None);
  }
  if (!PointeeType.isNull()) {
    uint64_t ElemSize = CGM.getTarget().getTypeSize(PointeeType);
    if (ElemSize > 1) {
      llvm::Value *ElemSizeVal = llvm::ConstantInt::get(PtrDiffTy, ElemSize);
      return Builder.CreateSDiv(ByteDiff, ElemSizeVal, "pdiff");
    }
  }
  return ByteDiff;
}
```

**修改 3: `EmitCompoundAssignment` 类型提升（第 252-261 行）**

在获取 LeftValue 和 RightValue 之后、运算之前，对 RHS 做 `EmitScalarConversion` 提升到公共运算类型。运算完成后，对 Result 做 `EmitScalarConversion` 截断回 LHS 类型再存储：

```cpp
// 运算前：将 RHS 提升到公共类型
QualType LHSType = BinaryOp->getLHS()->getType();
QualType RHSType = BinaryOp->getRHS()->getType();
// 用 BinaryOp->getType() 获取 Sema 推导的复合赋值结果类型
// 但复合赋值结果类型 = LHS 类型，运算应按 Sema 推导的公共类型进行
// 为简化：直接对 LeftValue/RightValue 做提升
if (LeftValue->getType()->isIntegerTy() && RightValue->getType()->isIntegerTy()) {
  llvm::Type *LHSLLVMTy = CGM.getTypes().ConvertType(LHSType);
  if (LeftValue->getType() != LHSLLVMTy) {
    // 截断/提升 LeftValue 到 LHS 的 LLVM 类型（确保对齐）
    // 实际上 LeftValue 从 EmitExpr 获取，应已是 LHS 类型
  }
  if (RightValue->getType() != LeftValue->getType()) {
    // RHS 类型与 LHS 不同时，将 RHS 转换到 LHS 类型
    RightValue = EmitScalarConversion(RightValue, RHSType, LHSType);
  }
}
// 浮点同理
if (LeftValue->getType()->isFloatingPointTy() && RightValue->getType()->isFloatingPointTy()) {
  if (RightValue->getType() != LeftValue->getType()) {
    RightValue = EmitScalarConversion(RightValue, RHSType, LHSType);
  }
}
```

**修改 4: `EmitUnaryOperator` 类型提升（第 505-525 行）**

对 Minus 和 Not 分支，运算后将结果转换到 `UnaryOp->getType()`（Sema 已设为提升后类型）：

```cpp
case UnaryOpKind::Minus: {
  llvm::Value *Operand = EmitExpr(UnaryOp->getSubExpr());
  if (!Operand) return nullptr;
  llvm::Value *Result = nullptr;
  if (Operand->getType()->isIntegerTy()) {
    Result = Builder.CreateNeg(Operand, "neg");
  } else if (Operand->getType()->isFloatingPointTy()) {
    Result = Builder.CreateFNeg(Operand, "fneg");
  } else return Operand;
  // 将结果转换到 Sema 推导的提升后类型
  QualType ResultType = UnaryOp->getType();
  return EmitScalarConversion(Result, UnaryOp->getSubExpr()->getType(), ResultType);
}
```

Not 分支同理。

#### 文件 3: `tests/lit/CodeGen/function-call.test` — 新增测试用例

覆盖所有修复场景的 lit 测试。

#### 文件 4: `docs/dev status/PHASE6-6.2-AUDIT.md` — 更新条目状态

### 性能考虑

- `EmitScalarConversion` 内部先检查 `Src->getType() == DstLLVMTy`，类型相同时直接返回，零开销
- 指针差值路径只在两操作数都是指针且 opcode 为 Sub 时触发，不影响其他路径
- 所有修改都是纯增加分支逻辑，无额外数据结构或状态

### 向后兼容

- `EmitScalarConversion` 已有函数签名不变，仅复用
- Sema 层 `getBinaryOperatorResultType` 增加分发逻辑，不改变已有行为
- `getCommonType` 中两指针分支不变（保留用于比较运算符），新逻辑在 `getBinaryOperatorResultType` 中处理