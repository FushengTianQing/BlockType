---
name: stage-4.4-overload-resolution
overview: 实现 Stage 4.4 重载决议的三个任务：Task 4.4.1 重载候选集、Task 4.4.2 隐式转换与排序、Task 4.4.3 最佳函数选择。头文件已存在（Conversion.h, Overload.h），需创建对应的 .cpp 实现文件，并更新 Sema.cpp 中的 stub 方法。
todos:
  - id: impl-conversion
    content: 实现 Conversion.cpp：ConversionChecker 全部静态方法 + SCS/ICS compare() + 更新 CMakeLists.txt
    status: completed
  - id: impl-overload
    content: 实现 Overload.cpp：OverloadCandidate checkViability/compare + OverloadCandidateSet 全部方法
    status: completed
    dependencies:
      - impl-conversion
  - id: impl-sema-integration
    content: 填充 Sema.cpp 的 ResolveOverload() 和 AddOverloadCandidate() stub
    status: completed
    dependencies:
      - impl-overload
  - id: build-verify
    content: 编译验证 + 运行全量测试（402 个）确保无回归
    status: completed
    dependencies:
      - impl-sema-integration
---

## 产品概述

实现 BlockType 编译器的重载决议系统（Stage 4.4），包括隐式转换分析和最佳函数选择。

## 核心功能

- **Task 4.4.1 重载候选集**: 实现 OverloadCandidate 的可行性检查和候选比较，以及 OverloadCandidateSet 的候选收集管理（从 LookupResult 中提取函数候选）
- **Task 4.4.2 隐式转换与排序**: 实现 ConversionChecker 全套静态方法（整数提升、浮点提升、标准转换、限定转换、派生到基类转换），以及 StandardConversionSequence 和 ImplicitConversionSequence 的 compare() 排序方法
- **Task 4.4.3 最佳函数选择**: 实现 OverloadCandidateSet::resolve() 核心算法，填充 Sema::ResolveOverload() 和 Sema::AddOverloadCandidate() 的 stub

## 技术栈

- 语言: C++17
- 构建: CMake
- 依赖: LLVM ADT (SmallVector, StringRef, ArrayRef), llvm::dyn_cast/cast/isa
- 测试: Google Test (gtest)

## 实现方案

### 整体策略

按依赖关系分三层实现：ConversionChecker（底层转换分析）→ OverloadCandidate（中层可行性+比较）→ OverloadCandidateSet::resolve + Sema 集成（顶层决议算法）。

### 关键技术决策

1. **ConversionChecker 使用静态方法模式**: 头文件已定义为 static 方法，无需实例化。所有转换分析通过 QualType 直接判断，基于 BuiltinKind 枚举值的 rank 比较。

2. **整数提升 rank 表**: 使用 BuiltinKind 的隐式排序：Bool(0) < Char(1) < Short(3) < Int(5) < Long(7) < LongLong(9)。浮点：Float(0) < Double(1) < LongDouble(2)。有符号与无符号同 rank。提升方向为从小 rank 到大 rank。

3. **OverloadCandidate 使用 ImplicitConversionSequence 而非 ConversionRank**: 当前头文件中 ArgRanks 是 `SmallVector<ConversionRank, 4>`，但比较时需要更细粒度的信息。为避免修改头文件，在 compare() 中通过 ConversionChecker::GetConversion() 重新计算 ICS 用于比较。

4. **resolve() 算法遵循 C++ [over.match.best]**:

- 检查所有候选的可行性（参数数量+每个参数转换）
- 收集可行候选
- 逐对比较，找到唯一最佳或报告歧义
- 处理变参函数（isVariadic）的省略号匹配

### 实现要点

#### Conversion.cpp 核心逻辑

- `isIntegralPromotion(From, To)`: 检查 From 是否为整数/枚举类型，To 是否为更大的整数类型。特殊规则：bool→int, char→int, short→int 是提升；int→long 是转换（不是提升）。
- `isFloatingPointPromotion(From, To)`: 仅 float→double 是提升。
- `isQualificationConversion(From, To)`: 检查指针/引用的 CVR 限定符增加（如 int *→ const int*）。
- `isDerivedToBaseConversion(From, To)`: 检查 RecordType 的继承关系（使用 CXXRecordDecl::isDerivedFrom）。
- `GetStandardConversion(From, To)`: 组合以上检查，构建 StandardConversionSequence，设置三步转换和 Rank。
- `GetConversion(From, To)`: 优先尝试标准转换，失败则返回 BadConversion（用户定义转换暂不实现）。
- `SCS::compare()` 和 `ICS::compare()`: 按 Rank 数值比较，同 Rank 比较子步骤。

#### Overload.cpp 核心逻辑

- `checkViability(Args)`: 检查参数数量（含默认参数和变参），对每个参数调用 ConversionChecker::GetConversion()，记录 ArgRanks。所有转换非 Bad 则 Viable=true。
- `compare(Other)`: 逐参数比较 ConversionRank，找到更优者。遵循 [over.match.best]: 至少一个参数更好，且无参数更差。
- `resolve(Args)`: 先 checkViability 所有候选，收集可行候选，逐对比较找最佳，检测歧义。

#### Sema.cpp 集成

- `ResolveOverload`: 创建 OverloadCandidateSet，调用 addCandidates，调用 resolve，处理诊断（无匹配/歧义）。
- `AddOverloadCandidate`: 创建 OverloadCandidate 并加入集合。

### 性能考虑

- 转换分析为纯类型比较，时间复杂度 O(1)（除派生到基类需遍历继承链）
- 候选比较 O(N^2) 其中 N 为候选数量，实际场景 N 很小（通常 < 16）
- 避免在 checkViability 中重复计算，ArgRanks 缓存每个参数的转换等级

## 目录结构

```
src/Sema/
├── CMakeLists.txt           # [MODIFY] 添加 Conversion.cpp, Overload.cpp
├── Conversion.cpp           # [NEW] ConversionChecker 全部静态方法实现 + SCS::compare + ICS::compare
├── Overload.cpp             # [NEW] OverloadCandidate::checkViability/compare + OverloadCandidateSet 全部方法
└── Sema.cpp                 # [MODIFY] 填充 ResolveOverload() 和 AddOverloadCandidate() 的 stub
```

### 文件详细说明

#### `src/Sema/Conversion.cpp` [NEW]

- 实现 `ConversionChecker::GetConversion()`: 从 From 到 To 的完整隐式转换序列
- 实现 `ConversionChecker::GetStandardConversion()`: 标准转换序列构建
- 实现 `ConversionChecker::isIntegralPromotion()`: 基于 BuiltinKind rank 表判断整数提升
- 实现 `ConversionChecker::isFloatingPointPromotion()`: float→double 判断
- 实现 `ConversionChecker::isStandardConversion()`: 整数转换、浮点转换、指针转换等
- 实现 `ConversionChecker::isQualificationConversion()`: CVR 限定符增加判断
- 实现 `ConversionChecker::isDerivedToBaseConversion()`: RecordType 继承链遍历
- 实现 `StandardConversionSequence::compare()`: 按 Rank 比较，同 Rank 比较子步骤
- 实现 `ImplicitConversionSequence::compare()`: 按 Kind 优先级比较，同 Kind 比较 Standard

#### `src/Sema/Overload.cpp` [NEW]

- 实现 `OverloadCandidate::checkViability()`: 参数数量检查 + 逐参数转换分析
- 实现 `OverloadCandidate::compare()`: 逐参数比较转换等级，遵循 [over.match.best]
- 实现 `OverloadCandidateSet::addCandidate()`: 创建并添加候选
- 实现 `OverloadCandidateSet::addCandidates()`: 从 LookupResult 提取 FunctionDecl 添加
- 实现 `OverloadCandidateSet::getViableCandidates()`: 过滤可行候选
- 实现 `OverloadCandidateSet::resolve()`: 核心决议算法

#### `src/Sema/Sema.cpp` [MODIFY]

- 填充 `Sema::ResolveOverload()`: 创建 OverloadCandidateSet → addCandidates → resolve → 错误诊断
- 填充 `Sema::AddOverloadCandidate()`: 包装 OverloadCandidate 创建和添加

#### `src/Sema/CMakeLists.txt` [MODIFY]

- 在 `add_library` 中添加 `Conversion.cpp` 和 `Overload.cpp`

## SubAgent

- **code-explorer**: 在实现阶段用于快速验证现有 API 接口（如 FunctionType::getParamTypes、Expr::getType 的精确签名），确保实现代码正确引用现有接口