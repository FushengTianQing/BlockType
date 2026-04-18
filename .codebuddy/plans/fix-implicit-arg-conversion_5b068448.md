---
name: fix-implicit-arg-conversion
overview: 修复 EmitCallExpr 中函数调用时未处理隐式参数类型转换的问题。新增 EmitScalarConversion 辅助函数，在参数传入函数前按形参类型进行类型匹配转换，覆盖 int↔int、float↔float、int↔float、pointer↔pointer 等场景。
todos:
  - id: add-emit-scalar-conversion
    content: 实现 EmitScalarConversion 辅助函数并声明到 CodeGenFunction.h
    status: completed
  - id: fix-three-call-sites
    content: 修改 EmitCallExpr 普通路径、虚函数路径、EmitCXXConstructExpr 三处参数发射逻辑
    status: completed
    dependencies:
      - add-emit-scalar-conversion
  - id: add-lit-tests
    content: 新增 lit 测试覆盖 int→long、float→double 等隐式转换场景
    status: completed
    dependencies:
      - fix-three-call-sites
  - id: build-and-test
    content: 编译项目并运行全量测试验证无回归
    status: completed
    dependencies:
      - add-lit-tests
  - id: update-audit-doc
    content: 更新审计文档条目 6 状态为已修复
    status: completed
    dependencies:
      - build-and-test
---

## Product Overview

修复 CodeGen 中函数调用时隐式参数类型转换缺失的问题，确保实参与形参类型不匹配时自动插入正确的 LLVM 转换指令。

## Core Features

- 新增 `EmitScalarConversion` 辅助函数，处理所有隐式标量类型转换（整数扩展/截断、浮点扩展/截断、整数与浮点互转、指针间 bitcast）
- 修改 `EmitCallExpr` 普通参数路径（第 672-677 行），按形参类型对实参做隐式转换
- 修改 `EmitCallExpr` 虚函数参数路径（第 652-657 行），同样应用隐式转换
- 修改 `EmitCXXConstructExpr` 构造函数参数路径（第 1189-1194 行），同样应用隐式转换
- 新增 lit 测试覆盖 `int→long`、`float→double`、`int→float` 等常见隐式转换场景
- 更新审计文档条目 6 状态

## Tech Stack

- 语言：C++ (LLVM IR 生成层)
- 构建系统：CMake
- 测试：lit + FileCheck

## Implementation Approach

### 问题根因

`EmitCallExpr` 第 672-677 行、虚函数路径第 652-657 行、`EmitCXXConstructExpr` 第 1189-1194 行，三处参数发射均直接 `EmitExpr(arg)` 后 push 到参数列表，没有将实参类型转换为形参类型。当 `void foo(long x)` 被 `foo(short_val)` 调用时，LLVM call 指令的参数类型（i16）与函数签名的形参类型（i64）不匹配，导致 LLVM 验证错误。

### 修复策略

新增 `EmitScalarConversion(llvm::Value *Src, QualType SrcTy, QualType DstTy)` 辅助函数，复用 `EmitCastExpr` 中已有的转换逻辑模式（整数扩展/截断、浮点扩展/截断、整数与浮点互转、指针间 bitcast），在所有参数发射点按形参类型执行隐式转换。

该函数的设计参考 Clang 的 `EmitScalarConversion`，但简化为仅处理 C/C++ 隐式转换中常见的标量类型转换场景。项目没有 `ImplicitCastExpr` AST 节点（Sema 层不插入），因此必须在 CodeGen 层自行处理。

### 转换逻辑（按优先级）

1. **类型相同**：直接返回（快速路径，零开销）
2. **整数→整数**：`CreateIntCast`（处理 int→long、short→int 等提升和截断）
3. **浮点→浮点**：`CreateFPCast`（处理 float→double 扩展和 double→float 截断）
4. **整数→浮点**：`CreateSIToFP`/`CreateUIToFP`（根据源类型有无符号）
5. **浮点→整数**：`CreateFPToSI`/`CreateFPToUI`（根据目标类型有无符号）
6. **指针→指针**：`CreateBitCast`（处理派生类指针→基类指针等）
7. **其他情况**：直接返回原值（保守策略，避免引入错误转换）

### 注意事项

- 对于成员函数，参数索引需偏移 1（第 0 个参数是 this 指针，第 i 个实参对应 `getParamDecl(i)`）
- 对于静态成员函数和自由函数，参数索引直接对应
- 对于构造函数，参数索引直接对应（this 已单独处理）
- 当实参数量超过形参数量时不做转换（兼容变参函数的未来扩展）

## Directory Structure

```
project-root/
├── include/blocktype/CodeGen/CodeGenFunction.h  # [MODIFY] 声明 EmitScalarConversion 方法
├── src/CodeGen/CodeGenExpr.cpp                   # [MODIFY] 实现 EmitScalarConversion；修改 3 处参数发射循环
├── tests/lit/CodeGen/function-call.test          # [MODIFY] 新增隐式参数转换测试用例
└── docs/dev status/PHASE6-6.2-AUDIT.md           # [MODIFY] 更新条目 6 状态为已修复
```

## Key Code Structures

```cpp
// CodeGenFunction.h — 新增声明（放在 EmitConversionToBool 旁边）
/// 隐式标量类型转换：将 Src 从 SrcType 转换为 DstType
/// 用于函数调用时实参到形参的隐式转换
llvm::Value *EmitScalarConversion(llvm::Value *Src, QualType SrcType,
                                  QualType DstType);
```