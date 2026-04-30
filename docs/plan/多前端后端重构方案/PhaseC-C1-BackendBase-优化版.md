# Phase C-C1 优化版：BackendBase + BackendRegistry + BackendOptions

> 基于 PhaseC-Planner-Report 修复后生成
> 修复内容：IRFeature/BackendCapability 去重、BackendBase 接口补全

---

## 依赖关系

- Phase A（IRModule/IRType/IRFeature/BackendCapability）
- Phase B（FrontendBase/FrontendRegistry，对称设计参考）

## 产出文件

| 操作 | 文件路径 | 说明 |
|------|----------|------|
| 新增 | `include/blocktype/Backend/BackendOptions.h` | 后端选项配置 |
| 新增 | `include/blocktype/Backend/BackendBase.h` | 后端抽象基类 |
| 新增 | `include/blocktype/Backend/BackendRegistry.h` | 后端注册表 |
| 新增 | `src/Backend/BackendBase.cpp` | BackendBase 实现 |
| 新增 | `src/Backend/BackendRegistry.cpp` | BackendRegistry 实现 |
| 新增 | `src/Backend/CMakeLists.txt` | CMake 构建文件 |

**注意**：不再新增 `BackendCapability.h` 和 `IRFeature` 定义——复用 Phase A 已有的：
- `include/blocktype/IR/IRModule.h`（`ir::IRFeature` 枚举）
- `include/blocktype/IR/BackendCapability.h`（`ir::BackendCapability` 类）

---

## 完整接口签名

```cpp
// === include/blocktype/Backend/BackendOptions.h ===
#pragma once
#include <string>
#include "blocktype/IR/ADT.h"

namespace blocktype::backend {

/// BackendOptions — 后端专用配置
/// 与 FrontendCompileOptions 对称，从 CodeGenOptions 提取的子集
struct BackendOptions {
  std::string TargetTriple;
  std::string OutputPath;
  std::string OutputFormat = "elf";  // elf, mach-o, coff
  unsigned OptimizationLevel = 0;
  bool EmitAssembly = false;
  bool EmitIR = false;
  bool EmitIRBitcode = false;
  bool DebugInfo = false;
  bool DebugInfoForProfiling = false;
  std::string DebugInfoFormat = "dwarf5";
};

} // namespace blocktype::backend
```

```cpp
// === include/blocktype/Backend/BackendBase.h ===
#pragma once
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/BackendCapability.h"  // ir::BackendCapability
#include "blocktype/IR/IRModule.h"           // ir::IRModule, ir::IRFeature
#include "blocktype/IR/ADT.h"
#include "blocktype/Backend/BackendOptions.h"
#include <functional>
#include <memory>

namespace blocktype::backend {

/// BackendBase — 所有后端的抽象基类
/// 对称于 FrontendBase（前端基类）
class BackendBase {
protected:
  BackendOptions Opts;
  DiagnosticsEngine& Diags;

public:
  BackendBase(const BackendOptions& Opts, DiagnosticsEngine& Diags);
  virtual ~BackendBase() = default;

  // 禁止拷贝
  BackendBase(const BackendBase&) = delete;
  BackendBase& operator=(const BackendBase&) = delete;

  /// 获取后端名称（如 "llvm", "cranelift"）
  virtual ir::StringRef getName() const = 0;

  /// 将 IRModule 编译为目标文件
  virtual bool emitObject(ir::IRModule& IRModule, ir::StringRef OutputPath) = 0;

  /// 将 IRModule 编译为汇编文件
  virtual bool emitAssembly(ir::IRModule& IRModule, ir::StringRef OutputPath) = 0;

  /// 将 IRModule 输出为 IR 文本
  virtual bool emitIRText(ir::IRModule& IRModule, ir::raw_ostream& OS) = 0;

  /// 检查后端是否能处理指定 TargetTriple
  /// 与 01-总体架构设计.md §1.4.1 一致
  virtual bool canHandle(ir::StringRef TargetTriple) const = 0;

  /// 优化 IRModule（后端可选实现）
  /// 与 01-总体架构设计.md §1.4.1 一致
  virtual bool optimize(ir::IRModule& IRModule) = 0;

  /// 获取后端能力声明
  /// 复用 ir::BackendCapability（Phase A 已实现）
  virtual ir::BackendCapability getCapability() const = 0;

  /// 访问选项
  const BackendOptions& getOptions() const { return Opts; }

  /// 访问诊断引擎
  DiagnosticsEngine& getDiagnostics() const { return Diags; }
};

/// BackendFactory — 后端工厂函数类型
using BackendFactory = std::function<std::unique_ptr<BackendBase>(
  const BackendOptions&, DiagnosticsEngine&)>;

} // namespace blocktype::backend
```

```cpp
// === include/blocktype/Backend/BackendRegistry.h ===
#pragma once
#include "blocktype/Backend/BackendBase.h"
#include "blocktype/IR/ADT.h"
#include <cassert>

namespace blocktype::backend {

/// BackendRegistry — 全局单例后端注册表
/// 对称于 FrontendRegistry
class BackendRegistry {
  ir::StringMap<BackendFactory> Registry;
  ir::StringMap<std::string> NameToTriple;  // 后端名→默认Triple映射
  BackendRegistry() = default;

public:
  static BackendRegistry& instance();

  // 禁止拷贝/移动
  BackendRegistry(const BackendRegistry&) = delete;
  BackendRegistry& operator=(const BackendRegistry&) = delete;

  /// 注册后端工厂函数
  void registerBackend(ir::StringRef Name, BackendFactory Factory);

  /// 创建后端实例
  std::unique_ptr<BackendBase> create(
    ir::StringRef Name, const BackendOptions& Opts, DiagnosticsEngine& Diags);

  /// 根据 IRModule 的 TargetTriple 自动选择后端
  std::unique_ptr<BackendBase> autoSelect(
    const ir::IRModule& M, const BackendOptions& Opts, DiagnosticsEngine& Diags);

  /// 查询后端是否已注册
  bool hasBackend(ir::StringRef Name) const;

  /// 获取所有已注册后端名称
  /// 注意：返回 std::string（值语义），避免 StringRef 悬空风险
  ir::SmallVector<std::string, 4> getRegisteredNames() const;
};

} // namespace blocktype::backend
```

---

## 后端能力降级策略表

| 不支持的特性 | 降级策略 | 是否可降级 |
|-------------|---------|-----------|
| ExceptionHandling | invoke→call（无异常表），发出警告 | ✅ 可降级 |
| DebugInfo | 无调试信息，发出警告 | ✅ 可降级 |
| VectorOperations | 标量循环，发出警告 | ✅ 可降级 |
| VarArg | 不生成变参函数，发出警告 | ✅ 可降级 |
| DynamicCast | **不可降级**，中止编译 | ❌ |
| VirtualDispatch | **不可降级**，中止编译 | ❌ |
| Coroutines | **不可降级**，中止编译 | ❌ |

---

## 实现约束

1. BackendBase 不可拷贝
2. emitObject/emitAssembly/emitIRText 失败返回 false
3. BackendRegistry 全局单例（与 FrontendRegistry 实现模式一致）
4. autoSelect 根据 IRModule 的 TargetTriple 选择后端（默认选 "llvm"）
5. 命名空间统一为 `blocktype::backend`
6. **复用** `ir::IRFeature` 和 `ir::BackendCapability`，不重新定义

---

## 验收标准

```cpp
// V1: BackendRegistry 注册和创建
auto& Reg = BackendRegistry::instance();
Reg.registerBackend("llvm", createLLVMBackend);
auto BE = Reg.create("llvm", Opts, Diags);
assert(BE != nullptr);
assert(BE->getName() == "llvm");

// V2: BackendCapability 特性声明（使用 ir:: 命名空间）
ir::BackendCapability Cap;
Cap.declareFeature(ir::IRFeature::IntegerArithmetic);
Cap.declareFeature(ir::IRFeature::FloatArithmetic);
assert(Cap.hasFeature(ir::IRFeature::IntegerArithmetic) == true);
assert(Cap.hasFeature(ir::IRFeature::Coroutines) == false);

// V3: 不支持特性检测
uint32_t Required = static_cast<uint32_t>(ir::IRFeature::IntegerArithmetic)
                  | static_cast<uint32_t>(ir::IRFeature::Coroutines);
uint32_t Unsupported = Cap.getUnsupported(Required);
assert((Unsupported & static_cast<uint32_t>(ir::IRFeature::Coroutines)) != 0);

// V4: hasBackend 查询
assert(Reg.hasBackend("llvm") == true);
assert(Reg.hasBackend("nonexistent") == false);
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| Phase A: `ir::IRModule` | emitObject/emitAssembly/emitIRText 的输入参数 |
| Phase A: `ir::IRFeature` | BackendCapability 的特性枚举 |
| Phase A: `ir::BackendCapability` | getCapability() 返回类型 |
| Phase B: `FrontendRegistry` | BackendRegistry 的对称设计参考 |
| `DiagnosticsEngine` | 构造参数，诊断报告 |

---

> Git 提交：`feat(C): 完成 C.1 — BackendBase + BackendRegistry + BackendOptions`
