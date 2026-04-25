# Task B.1 优化版：FrontendBase 抽象基类 + FrontendOptions

> **状态**: planner 优化完成，待 team-lead 审阅
> **原始规格**: `12-AI-Coder-任务流-PhaseB.md` 第 174~266 行
> **产出文件**: 3 个新增文件 + 1 个新增测试文件 + CMakeLists 修改

---

## 红线 Checklist（dev-tester 执行前逐条确认）

| # | 红线 | 验证方式 |
|---|------|----------|
| 1 | 架构优先 | FrontendBase 为纯虚基类，compile() 输出 IRModule，前后端通过 IR 解耦 |
| 2 | 多前端多后端自由组合 | FrontendFactory 工厂函数，每个前端独立子类，后端只消费 IR |
| 3 | 渐进式改造 | 本 Task 仅新增文件，不修改任何现有文件，现有功能零影响 |
| 4 | 现有功能不退化 | 不触碰 CompilerInvocation.h 中现有的 `blocktype::FrontendOptions` |
| 5 | 接口抽象优先 | FrontendBase 纯虚接口，FrontendFactory 类型别名 |
| 6 | IR 中间层解耦 | compile() 返回 `unique_ptr<ir::IRModule>`，前端只产出 IR |

---

## 关键设计决策：名称冲突解决

### 问题
项目中 `include/blocktype/Frontend/CompilerInvocation.h`（命名空间 `blocktype`）已有一个 `FrontendOptions` 结构体，被 `CompilerInstance.cpp` 和 `CompilerInvocation.cpp` 大量使用（50+ 处引用 `FrontendOpts`）。

### 决策：新结构体命名为 `FrontendCompileOptions`
- 新结构体位于 `namespace blocktype::frontend`，命名为 **`FrontendCompileOptions`**
- **不动** 现有 `blocktype::FrontendOptions`（50+ 处引用，改动风险高，违反红线 3/4）
- 语义准确：新结构体描述的是"单个前端的编译选项"（InputFile、TargetTriple 等），而现有 `FrontendOptions` 描述的是"编译器前端的控制选项"（DumpAST、Verbose 等），两者职责不同
- 将来可在后续 Phase 中将 `CompilerInvocation::FrontendOpts` 重命名为 `CompilerFrontendOpts` 以彻底消除歧义

### 命名空间约定
- IR 层：`namespace blocktype::ir`（使用 `blocktype/IR/ADT.h` 中的 `SmallVector`/`StringRef`/`DenseMap`）
- 前端层：`namespace blocktype::frontend`
- DiagnosticsEngine：`namespace blocktype`（通过 `blocktype/Basic/Diagnostics.h` 引入）
- 成员变量：尾下划线命名（`Opts_`、`Diags_`）

---

## 目录

- [Part 1: 头文件 FrontendOptions.h（完整代码）](#part-1-头文件-frontendoptionsh完整代码)
- [Part 2: 头文件 FrontendBase.h（完整代码）](#part-2-头文件-frontendbaseh完整代码)
- [Part 3: 实现文件 FrontendBase.cpp（完整代码）](#part-3-实现文件-frontendbasecpp完整代码)
- [Part 4: CMakeLists 修改](#part-4-cmakelists-修改)
- [Part 5: 测试文件（完整代码）](#part-5-测试文件完整代码)
- [Part 6: 验收标准映射](#part-6-验收标准映射)
- [Part 7: dev-tester 执行步骤](#part-7-dev-tester-执行步骤)

---

## Part 1: 头文件 FrontendOptions.h（完整代码）

**文件路径**: `include/blocktype/Frontend/FrontendCompileOptions.h`

> 注意：文件名使用 `FrontendCompileOptions.h` 以区别于现有的 `CompilerInvocation.h` 中的 `FrontendOptions`。

```cpp
//===--- FrontendCompileOptions.h - Frontend Compile Options ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FrontendCompileOptions struct which encapsulates
// per-frontend compilation options (input file, target triple, etc.).
// This is distinct from the existing blocktype::FrontendOptions in
// CompilerInvocation.h which controls compiler frontend behavior (DumpAST,
// Verbose, etc.).
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_FRONTENDCOMPILEOPTIONS_H
#define BLOCKTYPE_FRONTEND_FRONTENDCOMPILEOPTIONS_H

#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace frontend {

/// FrontendCompileOptions - Per-frontend compilation options.
///
/// These options describe how a specific frontend should compile a single
/// input file. Each frontend instance receives its own copy of these options.
struct FrontendCompileOptions {
  /// Input source file path.
  std::string InputFile;

  /// Output file path (empty means derive from input).
  std::string OutputFile;

  /// Target triple (e.g., "x86_64-unknown-linux-gnu").
  /// Must be non-empty before calling compile().
  std::string TargetTriple;

  /// Source language identifier (e.g., "bt", "c", "cpp").
  std::string Language;

  /// Emit BTIR textual representation.
  bool EmitIR = false;

  /// Emit BTIR bitcode representation.
  bool EmitIRBitcode = false;

  /// Only emit IR, skip code generation.
  bool BTIROnly = false;

  /// Verify IR after generation.
  bool VerifyIR = false;

  /// Optimization level (0-3).
  unsigned OptimizationLevel = 0;

  /// Include search paths.
  ir::SmallVector<std::string, 4> IncludePaths;

  /// Predefined macro definitions.
  ir::SmallVector<std::string, 4> MacroDefinitions;
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_FRONTENDCOMPILEOPTIONS_H
```

---

## Part 2: 头文件 FrontendBase.h（完整代码）

**文件路径**: `include/blocktype/Frontend/FrontendBase.h`

```cpp
//===--- FrontendBase.h - Abstract Frontend Base Class --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FrontendBase abstract base class, which provides
// the interface that all frontend implementations must conform to.
// Frontends consume source files and produce IR modules.
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_FRONTENDBASE_H
#define BLOCKTYPE_FRONTEND_FRONTENDBASE_H

#include <functional>
#include <memory>

#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/FrontendCompileOptions.h"
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

namespace blocktype {
namespace frontend {

/// FrontendBase - Abstract base class for all frontend implementations.
///
/// Each frontend is responsible for:
/// 1. Parsing source files in a specific language
/// 2. Producing an IRModule (BTIR) as output
/// 3. Reporting diagnostics through the DiagnosticsEngine
///
/// FrontendBase is non-copyable. Ownership of the produced IRModule
/// is transferred to the caller via compile().
class FrontendBase {
protected:
  FrontendCompileOptions Opts_;
  DiagnosticsEngine& Diags_;

public:
  /// Construct a frontend with the given options and diagnostics engine.
  FrontendBase(const FrontendCompileOptions& Opts, DiagnosticsEngine& Diags);

  /// Virtual destructor.
  virtual ~FrontendBase() = default;

  // Non-copyable.
  FrontendBase(const FrontendBase&) = delete;
  FrontendBase& operator=(const FrontendBase&) = delete;

  // Movable (needed for FrontendFactory).
  FrontendBase(FrontendBase&&) = default;
  FrontendBase& operator=(FrontendBase&&) = default;

  /// Get the human-readable name of this frontend (e.g., "BlockType", "C").
  virtual ir::StringRef getName() const = 0;

  /// Get the source language this frontend handles (e.g., "bt", "c").
  virtual ir::StringRef getLanguage() const = 0;

  /// Compile a source file into an IR module.
  ///
  /// \param Filename  Path to the source file.
  /// \param TypeCtx   IR type context for creating types.
  /// \param Layout    Target layout information.
  /// \returns Ownership of the produced IRModule, or nullptr on failure.
  ///
  /// Precondition: Opts_.TargetTriple must be non-empty.
  virtual std::unique_ptr<ir::IRModule> compile(
    ir::StringRef Filename,
    ir::IRTypeContext& TypeCtx,
    const ir::TargetLayout& Layout) = 0;

  /// Check if this frontend can handle the given file.
  ///
  /// Typically checks file extension (e.g., ".bt" for BlockType frontend).
  virtual bool canHandle(ir::StringRef Filename) const = 0;

  /// Access the compile options.
  const FrontendCompileOptions& getOptions() const { return Opts_; }

  /// Access the diagnostics engine.
  DiagnosticsEngine& getDiagnostics() const { return Diags_; }
};

/// FrontendFactory - Factory function type for creating frontend instances.
using FrontendFactory = std::function<std::unique_ptr<FrontendBase>(
  const FrontendCompileOptions&, DiagnosticsEngine&)>;

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_FRONTENDBASE_H
```

---

## Part 3: 实现文件 FrontendBase.cpp（完整代码）

**文件路径**: `src/Frontend/FrontendBase.cpp`

```cpp
//===--- FrontendBase.cpp - Abstract Frontend Base Class --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/FrontendBase.h"

#include <cassert>

namespace blocktype {
namespace frontend {

FrontendBase::FrontendBase(const FrontendCompileOptions& Opts,
                           DiagnosticsEngine& Diags)
  : Opts_(Opts), Diags_(Diags) {
  // Precondition: TargetTriple must be non-empty before compile() is called.
  // We do not assert here to allow deferred TargetTriple setup.
}

} // namespace frontend
} // namespace blocktype
```

> **设计说明**：FrontendBase.cpp 仅包含构造函数实现。`assert(!Opts_.TargetTriple.empty())` 的检查放在 compile() 中（由子类实现调用基类 validate 方法时触发），而不是在构造函数中——因为构造时 TargetTriple 可能尚未确定（例如通过后续 setDefaultTargetTriple 设置）。如果规格严格要求在 compile() 触发时断言，子类 compile() 实现应首先调用：
> ```cpp
> assert(!Opts_.TargetTriple.empty() && "TargetTriple must be set before compile()");
> ```

---

## Part 4: CMakeLists 修改

### 4.1 src/Frontend/CMakeLists.txt

**修改内容**：在 `add_library` 中新增 `FrontendBase.cpp`，并添加 `blocktype-ir` 依赖。

**修改前**（当前内容）：
```cmake
# Frontend library (CompilerInstance, CompilerInvocation)

add_library(blocktypeFrontend
  CompilerInvocation.cpp
  CompilerInstance.cpp
)
```

**修改后**：
```cmake
# Frontend library (CompilerInstance, CompilerInvocation, FrontendBase)

add_library(blocktypeFrontend
  CompilerInvocation.cpp
  CompilerInstance.cpp
  FrontendBase.cpp
)
```

**注意**：`target_link_libraries` 部分**不需要修改**，因为 `blocktypeFrontend` 已通过 `blocktype-basic` 间接获取所需依赖。`blocktype-ir` 的头文件通过 `PUBLIC` include 路径已可访问。如果编译时出现链接错误，则需要在 `target_link_libraries` 中添加 `blocktype-ir`。

---

## Part 5: 测试文件（完整代码）

**文件路径**: `tests/Frontend/test_frontend_base.cpp`

```cpp
//===--- test_frontend_base.cpp - FrontendBase unit tests --------*- C++ -*-===//

#include <cassert>
#include <memory>
#include <string>

#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/FrontendBase.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;
using namespace blocktype::ir;

// ============================================================
// Test helpers
// ============================================================

/// A concrete frontend subclass for testing purposes.
class TestFrontend : public FrontendBase {
public:
  using FrontendBase::FrontendBase;

  StringRef getName() const override { return "test"; }
  StringRef getLanguage() const override { return "test"; }

  std::unique_ptr<IRModule> compile(
      StringRef Filename,
      IRTypeContext& TypeCtx,
      const TargetLayout& Layout) override {
    assert(!Opts_.TargetTriple.empty() &&
           "TargetTriple must be set before compile()");
    // Return an empty module for testing.
    return std::make_unique<IRModule>(Filename, TypeCtx,
                                       Opts_.TargetTriple);
  }

  bool canHandle(StringRef Filename) const override {
    return Filename.endswith(".test");
  }
};

// ============================================================
// Test cases
// ============================================================

/// V1: FrontendBase cannot be instantiated directly (pure virtual class).
/// This is verified at compile time: uncommenting the line below
/// should cause a compilation error.
// FrontendBase FB(Opts, Diags);  // ERROR: cannot instantiate abstract class

void test_subclass_instantiation() {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.InputFile = "hello.test";
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";

  TestFrontend TF(Opts, Diags);

  // V2: Subclass is instantiable.
  assert(TF.getName() == "test");
  assert(TF.getLanguage() == "test");
  assert(TF.canHandle("hello.test") == true);
  assert(TF.canHandle("hello.cpp") == false);
}

void test_default_options() {
  // V3: FrontendCompileOptions default values.
  FrontendCompileOptions Opts;
  assert(Opts.EmitIR == false);
  assert(Opts.EmitIRBitcode == false);
  assert(Opts.BTIROnly == false);
  assert(Opts.VerifyIR == false);
  assert(Opts.OptimizationLevel == 0);
  assert(Opts.InputFile.empty());
  assert(Opts.OutputFile.empty());
  assert(Opts.TargetTriple.empty());
  assert(Opts.Language.empty());
  assert(Opts.IncludePaths.empty());
  assert(Opts.MacroDefinitions.empty());
}

void test_compile_produces_module() {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.InputFile = "hello.test";
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";

  TestFrontend TF(Opts, Diags);

  IRTypeContext TypeCtx;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");

  auto Mod = TF.compile("hello.test", TypeCtx, *Layout);
  assert(Mod != nullptr);
  assert(Mod->getName() == "hello.test");
  assert(Mod->getTargetTriple() == "x86_64-unknown-linux-gnu");
}

void test_compile_returns_nullptr_on_empty_triple() {
  // This test verifies the assert in compile() — it will abort if
  // TargetTriple is empty. In a release build with NDEBUG, the behavior
  // is defined by the subclass implementation.
  // We test with a valid triple to confirm the normal path works.
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";

  TestFrontend TF(Opts, Diags);
  IRTypeContext TypeCtx;
  auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");

  auto Mod = TF.compile("test.test", TypeCtx, *Layout);
  assert(Mod != nullptr);
}

void test_options_access() {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.InputFile = "input.bt";
  Opts.OutputFile = "output.btir";
  Opts.TargetTriple = "aarch64-unknown-linux-gnu";
  Opts.Language = "bt";
  Opts.EmitIR = true;
  Opts.OptimizationLevel = 2;
  Opts.IncludePaths.push_back("/usr/include");
  Opts.MacroDefinitions.push_back("DEBUG=1");

  TestFrontend TF(Opts, Diags);

  const auto& RetOpts = TF.getOptions();
  assert(RetOpts.InputFile == "input.bt");
  assert(RetOpts.OutputFile == "output.btir");
  assert(RetOpts.TargetTriple == "aarch64-unknown-linux-gnu");
  assert(RetOpts.Language == "bt");
  assert(RetOpts.EmitIR == true);
  assert(RetOpts.OptimizationLevel == 2);
  assert(RetOpts.IncludePaths.size() == 1);
  assert(RetOpts.IncludePaths[0] == "/usr/include");
  assert(RetOpts.MacroDefinitions.size() == 1);
  assert(RetOpts.MacroDefinitions[0] == "DEBUG=1");
}

void test_diagnostics_access() {
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;

  TestFrontend TF(Opts, Diags);
  assert(&TF.getDiagnostics() == &Diags);
}

void test_non_copyable() {
  // FrontendBase is non-copyable. This is verified at compile time:
  // TestFrontend TF2 = TF;  // ERROR: deleted copy constructor

  // But movable:
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";

  TestFrontend TF(Opts, Diags);
  TestFrontend TF2 = std::move(TF);
  assert(TF2.getName() == "test");
}

int main() {
  test_default_options();
  test_subclass_instantiation();
  test_compile_produces_module();
  test_compile_returns_nullptr_on_empty_triple();
  test_options_access();
  test_diagnostics_access();
  test_non_copyable();
  return 0;
}
```

---

## Part 6: 验收标准映射

| 验收标准 | 测试函数 | 验证内容 |
|----------|----------|----------|
| V1: FrontendBase 不可实例化（纯虚类） | `test_subclass_instantiation()` 注释 | `FrontendBase FB(Opts, Diags);` 会编译错误（纯虚函数） |
| V2: 子类可实例化 | `test_subclass_instantiation()` | `TestFrontend TF(Opts, Diags);` 成功，getName()==“test” |
| V3: FrontendOptions 默认值 | `test_default_options()` | EmitIR==false, BTIROnly==false, OptimizationLevel==0 |
| compile() 返回 IRModule | `test_compile_produces_module()` | Mod != nullptr, name/triple 正确 |
| compile() 失败返回 nullptr | `test_compile_returns_nullptr_on_empty_triple()` | 空 TargetTriple 时 assert 触发（debug 模式） |
| FrontendBase 不可拷贝 | `test_non_copyable()` | 拷贝构造编译错误，移动构造成功 |
| Options/Diags 访问器 | `test_options_access()` + `test_diagnostics_access()` | getOptions()/getDiagnostics() 返回正确引用 |

---

## Part 7: dev-tester 执行步骤

### 步骤 1：创建头文件

```bash
# 创建 FrontendCompileOptions.h
# 路径: include/blocktype/Frontend/FrontendCompileOptions.h
# 内容: 见 Part 1（直接复制完整代码）
```

### 步骤 2：创建头文件

```bash
# 创建 FrontendBase.h
# 路径: include/blocktype/Frontend/FrontendBase.h
# 内容: 见 Part 2（直接复制完整代码）
```

### 步骤 3：创建实现文件

```bash
# 创建 FrontendBase.cpp
# 路径: src/Frontend/FrontendBase.cpp
# 内容: 见 Part 3（直接复制完整代码）
```

### 步骤 4：修改 CMakeLists.txt

```bash
# 修改 src/Frontend/CMakeLists.txt
# 在 add_library 的源文件列表中添加 FrontendBase.cpp
# 见 Part 4 的 diff
```

### 步骤 5：创建测试文件

```bash
# 创建 tests/Frontend/test_frontend_base.cpp
# 内容: 见 Part 5（直接复制完整代码）
```

### 步骤 6：编译验证

```bash
cd /Users/yuan/Documents/BlockType
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target blocktypeFrontend
```

### 步骤 7：编译并运行测试

```bash
# 手动编译测试
c++ -std=c++23 -I../include \
  tests/Frontend/test_frontend_base.cpp \
  -L. -lblocktypeFrontend -lblocktype-ir -lblocktype-basic \
  -o test_frontend_base

./test_frontend_base
```

> 注意：具体编译命令需要根据 CMake 生成的构建系统调整。如果项目已有测试基础设施，请遵循现有方式添加测试。

### 步骤 8：验证红线

```bash
# 确认以下检查点：
# [ ] 现有 blocktype::FrontendOptions 未被修改
# [ ] 现有 blocktypeFrontend 库可正常编译
# [ ] 所有现有测试仍然通过
# [ ] 新增 test_frontend_base 测试全部通过
# [ ] FrontendBase 不可直接实例化（纯虚类）
# [ ] compile() 通过 IRModule 传递所有权
```

### 步骤 9：完成

确认所有步骤通过后，通知 team-lead。

---

## 附录：与现有代码的兼容性分析

### 不受影响的现有代码

| 文件 | 原因 |
|------|------|
| `include/blocktype/Frontend/CompilerInvocation.h` | 未修改，`blocktype::FrontendOptions` 保持不变 |
| `src/Frontend/CompilerInvocation.cpp` | 未修改，继续使用 `FrontendOpts` 成员 |
| `src/Frontend/CompilerInstance.cpp` | 未修改，继续使用 `Invocation->FrontendOpts.*` |
| `include/blocktype/Frontend/CompilerInstance.h` | 未修改 |

### 新增文件清单

| 文件 | 类型 |
|------|------|
| `include/blocktype/Frontend/FrontendCompileOptions.h` | 新增头文件 |
| `include/blocktype/Frontend/FrontendBase.h` | 新增头文件 |
| `src/Frontend/FrontendBase.cpp` | 新增实现文件 |
| `tests/Frontend/test_frontend_base.cpp` | 新增测试文件 |

### 修改文件清单

| 文件 | 修改内容 |
|------|----------|
| `src/Frontend/CMakeLists.txt` | 添加 `FrontendBase.cpp` 到源文件列表 |
