# Phase C Spec 预审报告

> 审查人：planner
> 审查日期：2026-04-30
> 审查范围：C.1~C.9 + C-F1~C-F7（Phase C 主任务 + 增强任务）

---

## 审查摘要

- 审查范围：C.1~C.9 + C-F1~C-F7
- 发现问题数：**12**
- 严重问题数：**4**
- 建议修改数：**8**
- **结论：SPEC_NEEDS_FIX** — 存在 4 个严重问题，需修复后才能开始开发

---

## 问题列表

### P1（严重）

| # | Task | 问题描述 | 建议修改 |
|---|------|---------|---------|
| 1 | C.1 | **IRFeature 枚举重复定义且位值冲突**：spec 在 `blocktype::backend` 命名空间定义了新的 `IRFeature` 枚举，但 Phase A 已在 `blocktype::ir` 命名空间（`include/blocktype/IR/IRModule.h`）定义了 `IRFeature`。两者的位值布局不同：spec 将 `Coroutines=1<<5, DebugInfo=1<<6, VarArg=1<<7`，实际代码为 `DebugInfo=1<<5, VarArg=1<<6, Coroutines=1<<11`。此外 spec 新增了 `FFICall=1<<12`，实际代码无此项。如果 backend 层重新定义会导致类型不兼容。 | **删除** spec C.1 中的 `IRFeature` 枚举定义，改为 `#include "blocktype/IR/IRModule.h"` 复用 `ir::IRFeature`。BackendBase 和 BackendCapability 中的 IRFeature 引用统一使用 `ir::IRFeature`。 |
| 2 | C.1 | **BackendCapability 类重复定义**：spec 在 `blocktype::backend` 命名空间定义了新的 `BackendCapability` 类（带 `getFeatureBits()` 方法），但 Phase A 已在 `blocktype::ir` 命名空间（`include/blocktype/IR/BackendCapability.h`）实现了 `BackendCapability`（带 `getSupportedMask()` 方法）。两套类的方法名不同但功能相同。 | **删除** spec C.1 中的 `BackendCapability` 类定义，改为 `#include "blocktype/IR/BackendCapability.h"` 复用 `ir::BackendCapability`。将 `BackendCapability.h` 文件从产出列表中移除。如果后端需要额外方法，在 `blocktype::backend` 命名空间提供适配器或别名。 |
| 3 | C.1 | **BackendBase 接口与 01-总体架构设计.md 不一致**：01-总体架构设计 §1.4.1 定义 BackendBase 有 `canHandle(StringRef)` 和 `optimize(IRModule&)` 纯虚方法，但 spec C.1 的 BackendBase 没有这两个方法。这会导致后续 Phase D 的 CompilerInstance 无法调用这些接口。 | 在 BackendBase 中**新增** `virtual bool canHandle(StringRef TargetTriple) const = 0;` 和 `virtual bool optimize(ir::IRModule& IRModule) = 0;`（与 01-总体架构设计保持一致）。LLVMBackend 实现时 `optimize()` 可委托给内部 `optimize(Module&, OptLevel)`。 |
| 4 | C.2 | **IRArrayType 不支持零长度数组**：spec C.2 映射规则表列出 `IRArrayType(T, 0)` → 零长度数组，但实际代码 `include/blocktype/IR/IRType.h` 中 `IRArrayType` 构造函数有 `assert(N > 0)` 断言，不支持 N=0。 | **修改映射表**：移除 `IRArrayType(T, 0)` 行，添加注释说明 "IR 层不支持零长度数组，后端无需处理此情况"。或在 IR 层修改断言以支持零长度数组（但需评估影响范围）。 |

### P2（建议）

| # | Task | 问题描述 | 建议修改 |
|---|------|---------|---------|
| 1 | C.1 | **BackendRegistry::getRegisteredNames 返回 StringRef 有悬空风险**：spec 返回 `SmallVector<StringRef, 4>`，但 StringMap 条目可能被删除。FrontendRegistry 实际代码返回 `SmallVector<std::string, 4>` 更安全。 | 改为 `SmallVector<std::string, 4>` 与 FrontendRegistry 保持一致。 |
| 2 | C.3 | **convertInstruction 映射表缺少大量已实现 Opcode**：实际代码（IRValue.h）定义了 50+ Opcode，但 spec C.3 映射表仅列出约 45 种。缺少的 Opcode 包括：`URem, SRem, FREm, Memcpy, Memset, FPToUI, UIToFP, ExtractElement, InsertElement, ShuffleVector, Resume, DbgLabel, AtomicLoad, AtomicStore, AtomicRMW, AtomicCmpXchg, Fence, DynamicCast, VtableDispatch, RTTITypeid, TargetIntrinsic, MetaInlineAlways, MetaInlineNever, MetaHot, MetaCold, FFICall, FFICheck, FFICoerce, FFIUnwind`。 | 将映射表补全为与 04-后端适配.md §2.3.1 一致的完整版本（含 URem/SRem/FRem/Memcpy 等），并新增 Phase A 增强任务中引入的 Opcode（atomic/FFI/dialect 相关）的映射规则。对于远期 Opcode（DynamicCast/VtableDispatch 等），标注为 "Phase C.8 实现" 或 "未实现时发出警告"。 |
| 3 | C.1 | **LLVMBackend::getCapability() 与 BackendCaps::LLVM() 冲突**：spec C.5 的 `getCapability()` 声明了除 Coroutines 外的所有特性（注释"未来支持"），但 Phase A 的 `BackendCaps::LLVM()` 工厂函数已包含 `Coroutines`。另外 spec 新增了 `FFICall` 特性但 `getCapability()` 未声明。 | 确保 `LLVMBackend::getCapability()` 实现与 `BackendCaps::LLVM()` 一致。如果需要区分，则在 Phase C 代码中不使用 `BackendCaps::LLVM()`，而是在 `LLVMBackend` 中自行声明。 |
| 4 | C.1 | **BackendOptions 与 CodeGenOptions 职责重叠**：BackendOptions 包含 `OptimizationLevel, EmitAssembly, EmitIR, DebugInfo` 等字段，与现有 `CodeGenOptions`（在 CompilerInvocation.h 中）高度重叠。Phase D 需要 CompilerInvocation 同时传递两者，关系不清。 | 明确 BackendOptions 是后端专用选项子集，从 CodeGenOptions 提取。在 Task D.1 中实现 `BackendOptions::fromCodeGenOptions(const CodeGenOptions&)` 工厂方法。 |
| 5 | C.2 | **IRToLLVMConverter 的 mapType 等方法为 private 但验收标准直接调用**：spec 定义 mapType/mapValue 等为 private，但验收标准中 `Converter.mapType(IRInt32Ty)` 直接调用。 | 将 mapType 等方法改为 **public**（提供测试可见性），或在验收标准中使用 `convert()` 级别的端到端测试代替直接调用 mapType。推荐后者更合理——mapType 应保持 private，验收标准应改用 convert() 后检查结果。 |
| 6 | C.5 | **LLVMBackend 成员命名不一致**：spec 用 `Converter`，01-总体架构设计用 `IRConverter`。 | 统一为 `Converter`（spec 优先），并在 01 文档中同步更新。 |
| 7 | C-F1/C-F2 | **C-F1/C-F2 文件路径不合理**：C-F1 将 `IRPlugin.h` 放在 `include/blocktype/IR/`，C-F2 将 `CompilerPlugin.cpp` 放在 `src/IR/`。但 PluginManager 管理前端/后端插件，属于编译管线层，不是 IR 层。 | 将 `IRPlugin.h` 改为 `include/blocktype/Frontend/PluginManager.h`（或独立 `include/blocktype/Plugin/` 目录），实现文件放在对应 `src/` 下。或保持当前路径但重命名为 `IRCompilerPlugin.h` 以明确用途。 |
| 8 | C.1 | **autoSelect 接口不在 01-总体架构设计中**：01 的 BackendRegistry 仅有 `registerBackend` 和 `create`，无 `autoSelect`。spec 新增了 `autoSelect(const IRModule&, ...)` 根据 TargetTriple 选择后端。 | 在 01-总体架构设计.md §1.4.2 的 BackendRegistry 中补上 autoSelect 方法签名，或在本报告中将其标记为 Phase C 新增接口（合理扩展，可接受）。 |

---

## 依赖验证

### 已验证存在的依赖

| 依赖项 | 文件路径 | 命名空间 | 状态 |
|--------|---------|---------|------|
| IRType 体系（IRVoidType, IRIntegerType, IRFloatType, IRPointerType, IRArrayType, IRStructType, IRFunctionType, IRVectorType, IROpaqueType） | `include/blocktype/IR/IRType.h` | `blocktype::ir` | ✓ 存在 |
| IRTypeContext | `include/blocktype/IR/IRTypeContext.h` | `blocktype::ir` | ✓ 存在 |
| IRModule | `include/blocktype/IR/IRModule.h` | `blocktype::ir` | ✓ 存在 |
| IRFunction / IRFunctionDecl | `include/blocktype/IR/IRFunction.h` | `blocktype::ir` | ✓ 存在 |
| IRValue / User / Use | `include/blocktype/IR/IRValue.h` | `blocktype::ir` | ✓ 存在 |
| IRInstruction（含完整 Opcode 枚举） | `include/blocktype/IR/IRInstruction.h` | `blocktype::ir` | ✓ 存在 |
| IRConstant（含所有子类） | `include/blocktype/IR/IRConstant.h` | `blocktype::ir` | ✓ 存在 |
| IRBasicBlock | `include/blocktype/IR/IRBasicBlock.h` | `blocktype::ir` | ✓ 存在 |
| IRFeature 枚举 | `include/blocktype/IR/IRModule.h` | `blocktype::ir` | ✓ 存在 |
| BackendCapability 类 | `include/blocktype/IR/BackendCapability.h` | `blocktype::ir` | ✓ 存在 |
| IRDebugMetadata（DICompileUnit, DIType, DISubprogram, DILocation） | `include/blocktype/IR/IRDebugMetadata.h` | `blocktype::ir` | ✓ 存在 |
| IRDebugInfo（IRInstructionDebugInfo） | `include/blocktype/IR/IRDebugInfo.h` | `blocktype::ir` | ✓ 存在 |
| TargetLayout | `include/blocktype/IR/TargetLayout.h` | `blocktype::ir` | ✓ 存在 |
| FrontendBase | `include/blocktype/Frontend/FrontendBase.h` | `blocktype::frontend` | ✓ 存在 |
| FrontendRegistry | `include/blocktype/Frontend/FrontendRegistry.h` | `blocktype::frontend` | ✓ 存在 |
| FrontendCompileOptions | `include/blocktype/Frontend/FrontendCompileOptions.h` | `blocktype::frontend` | ✓ 存在 |
| DiagnosticsEngine | `include/blocktype/Basic/Diagnostics.h` | `blocktype` | ✓ 存在 |
| CompilerInvocation | `include/blocktype/Frontend/CompilerInvocation.h` | `blocktype` | ✓ 存在 |
| DialectID 枚举 | `include/blocktype/IR/IRType.h` | `blocktype::ir::dialect` | ✓ 存在 |
| PluginManager | `include/blocktype/Frontend/PluginManager.h` | `blocktype::frontend` | ✓ 存在 |
| StructuredDiagnostic | `include/blocktype/Frontend/StructuredDiagnostic.h` | `blocktype::frontend` | ✓ 存在 |

### 缺失的依赖

| 依赖项 | 需要 | 状态 |
|--------|------|------|
| 无 | — | 所有 Phase A/B 依赖均已验证存在 |

---

## 逐 Task 详细审查

### Task C.1：BackendBase + BackendRegistry + BackendCapability

**发现 4 个严重问题 + 3 个建议问题**（见上表 P1-1/2/3 和 P2-1/3/4/8）

**核心问题**：spec 试图在 `blocktype::backend` 命名空间重新定义已在 `blocktype::ir` 中存在的 `IRFeature` 和 `BackendCapability`。这违反了"不重复造轮子"原则，会导致类型系统分裂。

**建议修改方案**：

```cpp
// include/blocktype/Backend/BackendBase.h
#pragma once
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/BackendCapability.h"  // 复用 ir::BackendCapability
#include "blocktype/IR/IRModule.h"           // 复用 ir::IRFeature
#include "blocktype/IR/ADT.h"

namespace blocktype::backend {

// BackendOptions — 后端专用配置（从 CodeGenOptions 提取的子集）
struct BackendOptions { ... };  // 保持 spec 原定义

// 不重新定义 IRFeature 和 BackendCapability
// 直接使用 ir::IRFeature 和 ir::BackendCapability

class BackendBase {
protected:
  BackendOptions Opts;
  DiagnosticsEngine& Diags;
public:
  BackendBase(const BackendOptions& Opts, DiagnosticsEngine& Diags);
  virtual ~BackendBase() = default;

  virtual StringRef getName() const = 0;
  virtual bool emitObject(ir::IRModule& IRModule, StringRef OutputPath) = 0;
  virtual bool emitAssembly(ir::IRModule& IRModule, StringRef OutputPath) = 0;
  virtual bool emitIRText(ir::IRModule& IRModule, raw_ostream& OS) = 0;
  virtual bool canHandle(StringRef TargetTriple) const = 0;       // 新增：与 01 一致
  virtual bool optimize(ir::IRModule& IRModule) = 0;              // 新增：与 01 一致
  virtual ir::BackendCapability getCapability() const = 0;         // 使用 ir:: 前缀

  const BackendOptions& getOptions() const { return Opts; }
  DiagnosticsEngine& getDiagnostics() const { return Diags; }
};

using BackendFactory = std::function<std::unique_ptr<BackendBase>(
  const BackendOptions&, DiagnosticsEngine&)>;

class BackendRegistry { ... };  // 保持 spec 原定义，getRegisteredNames 改用 std::string

} // namespace blocktype::backend
```

### Task C.2：IRToLLVMConverter 类型转换

**发现 1 个严重问题 + 1 个建议问题**（P1-4, P2-5）

类型映射表整体正确，与 04-后端适配.md §2.3.1 一致。递归类型处理算法清晰。

**需修改**：
1. 移除 `IRArrayType(T, 0)` 零长度数组行（P1-4）
2. mapType 等方法改为 public，或验收标准改为端到端测试（P2-5）

### Task C.3：IRToLLVMConverter 值/指令转换

**发现 1 个建议问题**（P2-2）

映射表缺少约 30 个实际存在的 Opcode。核心 Opcode（算术/内存/转换/比较/调用）覆盖完整。

**建议**：补充缺失 Opcode 到映射表，或明确标注"Phase C 仅实现核心 Opcode，其余发出警告"。这与 spec 自身的"未实现的 Opcode 发出 diag::warn_ir_opcode_not_supported"约束一致。

### Task C.4：IRToLLVMConverter 函数/模块/全局变量转换

**无问题**。实现约束和验收标准清晰可执行。

### Task C.5：LLVMBackend 优化 pipeline

**发现 1 个建议问题**（P2-6）

getCapability() 实现需与 `ir::BackendCaps::LLVM()` 协调。

### Task C.6：LLVMBackend 目标代码生成

**无问题**。实现约束和验收标准清晰。

### Task C.7：LLVMBackend 调试信息转换

**依赖已验证**：`ir::DebugMetadata` 系列（DICompileUnit/DIType/DISubprogram/DILocation）均已在 Phase A 中实现。

### Task C.8：LLVMBackend VTable/RTTI 生成

**依赖已验证**：B.7 IREmitCXX 相关文件已存在（IREmitCXXVTable.cpp 等）。

### Task C.9：集成测试

**无问题**。验收标准可执行。

### Task C-F1~C-F7：增强任务

**发现 1 个建议问题**（P2-7）：C-F1/C-F2 文件路径建议调整。

增强任务整体定义简洁，依赖关系正确。

---

## 结论

- [ ] ~~SPEC_OK — 无严重问题，可以直接开始开发~~
- [x] **SPEC_NEEDS_FIX — 存在严重问题，需要修复后才能开发**

**必须修复的 4 个 P1 问题**：

1. **IRFeature 枚举去重**：删除 backend 命名空间的重定义，复用 `ir::IRFeature`
2. **BackendCapability 类去重**：删除 backend 命名空间的重定义，复用 `ir::BackendCapability`
3. **BackendBase 接口补全**：新增 `canHandle()` 和 `optimize()` 方法，与 01-总体架构设计保持一致
4. **IRArrayType 零长度数组**：移除映射表中不支持的 N=0 情况

**建议修复后，即可开始开发。** P2 问题可在开发过程中逐步修复，不阻塞开发启动。

---

## 下一步

1. Planner（我）根据本报告修复 spec，生成优化版 task 文件
2. 优化版 task 文件将包含修正后的接口签名和验收标准
3. 开发按优化版 task 文件执行

> **文档版本**：v1.0
> **更新日期**：2026-04-30
