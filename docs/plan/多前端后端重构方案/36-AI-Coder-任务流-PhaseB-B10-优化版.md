# Task B.10 CppFrontend 集成 + 契约验证 — 优化版规格

> 文档版本：优化版 v1.0（基于代码库 API 验证）
> 原始规格：`docs/plan/多前端后端重构方案/12-AI-Coder-任务流-PhaseB.md` Task B.10（第 1094~1219 行）
> 依赖：B.1 ~ B.9 全部完成 ✅

---

## 一、代码库验证摘要（12 项修正）

| # | 原始规格 | 实际代码库 | 修正措施 |
|---|---------|-----------|---------|
| 1 | `CppFrontend(const FrontendOptions& Opts, ...)` | 基类构造函数接收 `FrontendCompileOptions`（不是 `FrontendOptions`） | 改为 `FrontendCompileOptions` |
| 2 | `FrontendBase` 纯虚接口 | 精确签名已确认（`FrontendBase.h:40-90`），`compile()` 参数正确 | ✅ |
| 3 | `FrontendRegistry::registerFrontend("cpp", createCppFrontend)` | `registerFrontend(StringRef, FrontendFactory)` — `FrontendFactory = function<unique_ptr<FrontendBase>(const FrontendCompileOptions&, DiagnosticsEngine&)>` | 确认 factory 签名 |
| 4 | `unique_ptr<SourceManager>` / `unique_ptr<Preprocessor>` 等 | `SourceManager()` 默认构造，`Preprocessor(SM&, Diags&, HeaderSearch*, LanguageManager*, FileManager*)`，`Parser(PP&, ASTContext&, Sema&)`，`Sema(ASTContext&, DiagsEngine&)`，`ASTContext()` 默认构造 | 修正初始化顺序和构造参数 |
| 5 | `contract::verifyAllContracts()` 返回 `bool` | 实际返回 `ir::IRVerificationResult`（`IRContract.h:33`），含 `isValid()` + `getViolations()` | 修正返回类型 |
| 6 | 契约验证命名空间 `frontend::contract` | 实际为 `ir::contract`（`IR/IRContract.h`），已全部实现 | 不需新建，直接调用 |
| 7 | 6 个契约验证函数需新建 | 全部已存在于 `IR/IRContract.cpp`（6 函数 + `verifyAllContracts`） | 不需新建，只需在 CppFrontend 中调用 |
| 8 | `VerifierPass` 需新建 | 已存在 `IR/IRVerifier.h`，`VerifierPass::run(IRModule&)` 或 `VerifierPass::verify(IRModule&)` | 可选使用，与 contract 互补 |
| 9 | `IRConversionResult::isUsable()` / `takeModule()` | 已确认（`IRConversionResult.h:39-40`） | ✅ |
| 10 | `IRModule` 构造 `IRModule(StringRef, IRTypeContext&, StringRef Triple, StringRef DL)` | 已确认（`IRModule.h:110-111`） | ✅ |
| 11 | `canHandle()` 通过扩展名判断 | `FrontendRegistry::addExtensionMapping()` 已实现，但 `canHandle()` 是实例方法 | 本地实现扩展名检查 |
| 12 | V4 差分测试 | 集成测试级别，需要完整的编译器流水线 | 降级为 TODO，B.10 只做 V1~V3 |

---

## 二、产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/CppFrontend.h` |
| 新增 | `src/Frontend/CppFrontend.cpp` |
| 新增 | `src/Frontend/CppFrontendRegistration.cpp` |
| 新增 | `tests/unit/Frontend/CppFrontendTest.cpp` |

**不需要新建**的文件（已存在）：
- `IR/IRContract.h` — 6 个契约验证函数 + `verifyAllContracts()`
- `IR/IRVerifier.h` — `VerifierPass`

---

## 三、头文件依赖

```cpp
// CppFrontend.h
#pragma once

#include <memory>
#include <string>

#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Frontend/FrontendBase.h"
#include "blocktype/Frontend/FrontendCompileOptions.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Parse/Parser.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRContract.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"
```

```cpp
// CppFrontendRegistration.cpp
#include "blocktype/Frontend/CppFrontend.h"
#include "blocktype/Frontend/FrontendRegistry.h"
```

---

## 四、类定义（精确 API）

### 4.1 CppFrontend.h

```cpp
namespace blocktype::frontend {

class CppFrontend : public FrontendBase {
  // === 编译流水线组件 ===
  std::unique_ptr<SourceManager> SM_;
  std::unique_ptr<ASTContext> ASTCtx_;
  std::unique_ptr<Sema> SemaPtr_;
  std::unique_ptr<Preprocessor> PP_;
  std::unique_ptr<Parser> ParserPtr_;

  // === IR 转换 ===
  std::unique_ptr<ir::IRContext> IRCtx_;
  // Note: IRConverter 在 compile() 中每次创建，不跨编译保持

public:
  /// 构造函数。
  /// @param Opts  编译选项（含 TargetTriple、IncludePaths 等）
  /// @param Diags 诊断引擎
  CppFrontend(const FrontendCompileOptions& Opts, DiagnosticsEngine& Diags);

  /// 获取前端名称。
  ir::StringRef getName() const override { return "cpp"; }

  /// 获取源语言标识。
  ir::StringRef getLanguage() const override { return "c++"; }

  /// 编译源文件为 IR 模块。
  /// @param Filename 源文件路径
  /// @param TypeCtx  IR 类型上下文
  /// @param Layout   目标布局信息
  /// @return IRModule 所有权，失败返回 nullptr
  std::unique_ptr<ir::IRModule> compile(
    ir::StringRef Filename,
    ir::IRTypeContext& TypeCtx,
    const ir::TargetLayout& Layout) override;

  /// 检查是否能处理给定文件（通过扩展名判断）。
  bool canHandle(ir::StringRef Filename) const override;

private:
  /// 读取源文件内容到字符串。
  /// @return 源文件内容，失败返回空字符串
  static std::string readSourceFile(ir::StringRef Filename);
};

} // namespace blocktype::frontend
```

### 4.2 构造函数实现

```cpp
// CppFrontend.cpp

CppFrontend::CppFrontend(const FrontendCompileOptions& Opts,
                          DiagnosticsEngine& Diags)
  : FrontendBase(Opts, Diags) {
  // 组件延迟初始化——在 compile() 中创建
  // 因为 SourceManager 需要文件内容，在编译时才可知
}
```

**初始化顺序**（在 `compile()` 中执行）：

```
1. SourceManager()           — 默认构造
2. ASTContext()              — 默认构造
3. Sema(ASTContext&, Diags&) — 依赖 ASTContext
4. Preprocessor(SM&, Diags&, HeaderSearch*, LanguageManager*, FileManager*)
   — 依赖 SourceManager
5. Parser(PP&, ASTContext&, Sema&)
   — 依赖 Preprocessor、ASTContext、Sema
6. SM_->createMainFileID(Filename, Content) — 加载源文件
7. Parser->parseTranslationUnit()          — 解析
8. IRContext()                             — IR 分配器
9. ASTToIRConverter(IRCtx, TypeCtx, Layout, Diags) — 转换器
```

**验证来源**：
- `SourceManager()` — `Basic/SourceManager.h:60`
- `ASTContext()` — `AST/ASTContext.h:88`
- `Sema(ASTContext&, Diags&)` — `Sema/Sema.cpp:69`
- `Preprocessor(SM&, Diags&, HeaderSearch*, LanguageManager*, FileManager*)` — `Lex/Preprocessor.cpp:89-91`
- `Parser(PP&, ASTContext&, Sema&)` — `Parse/Parser.h:108`

### 4.3 compile() 精确流程

```cpp
std::unique_ptr<ir::IRModule>
CppFrontend::compile(ir::StringRef Filename,
                      ir::IRTypeContext& TypeCtx,
                      const ir::TargetLayout& Layout) {
  // === 1. 读取源文件 ===
  std::string Content = readSourceFile(Filename);
  if (Content.empty() && !Opts_.InputFile.empty()) {
    Diags_.report(DiagID::err_cannot_open_input, Filename);
    return nullptr;
  }

  // === 2. 初始化编译流水线 ===
  SM_ = std::make_unique<SourceManager>();
  ASTCtx_ = std::make_unique<ASTContext>();
  SemaPtr_ = std::make_unique<Sema>(*ASTCtx_, Diags_);
  PP_ = std::make_unique<Preprocessor>(*SM_, Diags_, /*HeaderSearch=*/nullptr,
                                        /*LanguageManager=*/nullptr,
                                        /*FileManager=*/nullptr);
  ParserPtr_ = std::make_unique<Parser>(*PP_, *ASTCtx_, *SemaPtr_);

  // === 3. 加载源文件并解析 ===
  SM_->createMainFileID(Filename, Content);
  TranslationUnitDecl* TU = ParserPtr_->parseTranslationUnit();
  if (!TU) {
    Diags_.report(DiagID::err_parsing_failed, Filename);
    return nullptr;
  }

  // === 4. AST → IR 转换 ===
  auto IRCtx = std::make_unique<ir::IRContext>();
  ASTToIRConverter Converter(*IRCtx, TypeCtx, Layout, Diags_);
  ir::IRConversionResult Result = Converter.convert(TU);

  if (!Result.isUsable()) {
    Diags_.report(DiagID::err_ir_conversion_failed, Filename);
    return nullptr;
  }

  // === 5. 契约验证（可选） ===
  if (Opts_.VerifyIR) {
    ir::IRVerificationResult VR = ir::contract::verifyAllContracts(*Result.getModule());
    if (!VR.isValid()) {
      for (const auto& V : VR.getViolations()) {
        Diags_.report(DiagID::warn_ir_contract_violation, V);
      }
    }
  }

  // === 6. 返回 IRModule ===
  // 注意：IRContext 的生命周期必须延长到与 IRModule 一起
  // IRModule 引用了 IRContext 中的类型，需要保持 IRContext 存活
  // 方案 A：让 IRModule 持有 IRContext（当前 IRModule 引用 IRTypeContext&）
  // 方案 B：由调用方管理 IRContext 生命周期
  // 当前：IRModule 引用 IRTypeContext&（来自 IRContext::getTypeContext()）
  // 需要确保 IRContext 在 IRModule 使用期间不被销毁
  return Result.takeModule();
}
```

### 4.4 canHandle() 实现

```cpp
bool CppFrontend::canHandle(ir::StringRef Filename) const {
  static const ir::SmallVector<const char*, 8> Extensions = {
    ".cpp", ".cc", ".cxx", ".C", ".c", ".h", ".hpp", ".hxx"
  };
  for (auto* Ext : Extensions) {
    if (Filename.endswith(Ext))
      return true;
  }
  return false;
}
```

### 4.5 静态注册

```cpp
// CppFrontendRegistration.cpp

#include "blocktype/Frontend/CppFrontend.h"
#include "blocktype/Frontend/FrontendRegistry.h"

namespace blocktype::frontend {

/// 工厂函数：创建 CppFrontend 实例。
/// 签名必须匹配 FrontendFactory =
///   function<unique_ptr<FrontendBase>(const FrontendCompileOptions&, DiagnosticsEngine&)>
static std::unique_ptr<FrontendBase>
createCppFrontend(const FrontendCompileOptions& Opts,
                  DiagnosticsEngine& Diags) {
  return std::make_unique<CppFrontend>(Opts, Diags);
}

/// 静态注册器——程序启动时自动注册 C++ 前端。
static struct CppFrontendRegistrator {
  CppFrontendRegistrator() {
    auto& Reg = FrontendRegistry::instance();
    Reg.registerFrontend("cpp", createCppFrontend);
    Reg.addExtensionMapping(".cpp", "cpp");
    Reg.addExtensionMapping(".cc",  "cpp");
    Reg.addExtensionMapping(".cxx", "cpp");
    Reg.addExtensionMapping(".C",   "cpp");
    Reg.addExtensionMapping(".c",   "cpp");
    Reg.addExtensionMapping(".h",   "cpp");
    Reg.addExtensionMapping(".hpp", "cpp");
    Reg.addExtensionMapping(".hxx", "cpp");
  }
} CppFrontendRegistratorInstance;

} // namespace blocktype::frontend
```

**验证来源**：
- `FrontendFactory` 类型 — `FrontendBase.h:93-94`
- `registerFrontend(StringRef, FrontendFactory)` — `FrontendRegistry.h:64`
- `addExtensionMapping(StringRef, StringRef)` — `FrontendRegistry.h:96`

---

## 五、IRContext 生命周期问题（关键架构决策）

### 问题描述

`IRModule` 引用 `IRTypeContext&`（`IRModule.h:96`），而 `IRTypeContext` 存在于 `IRContext` 内部（`IRContext.h:31`）。`compile()` 返回 `unique_ptr<IRModule>` 后，如果 `IRContext` 被销毁，`IRModule` 的类型引用将悬空。

### 方案选择

**方案 A（推荐）**：将 `IRContext` 作为 `compile()` 参数传入

```cpp
// 调用方提供 IRContext 并负责其生命周期
std::unique_ptr<ir::IRModule> compile(
  ir::StringRef Filename,
  ir::IRTypeContext& TypeCtx,
  const ir::TargetLayout& Layout) override;
// 内部：使用 TypeCtx（来自 IRContext.getTypeContext()），IRContext 由调用方持有
```

这与 `FrontendBase::compile()` 的签名一致——`TypeCtx` 参数由调用方提供。

**方案 B**：`IRModule` 持有 `unique_ptr<IRContext>`

需要修改 `IRModule` 类，超出 B.10 范围。

**当前建议**：采用方案 A。`IRTypeContext&` 由外部提供（可能是全局的或由 driver 管理的），`IRContext` 由同一层管理。`compile()` 内部可创建临时 `IRContext` 用 `create<T>()` 分配常量/指令，但最终 `IRModule` 使用的类型来自外部 `TypeCtx`。

### 实际代码调整

```cpp
// compile() 中不创建 IRContext，直接使用 TypeCtx 参数
// 但 ASTToIRConverter 需要 IRContext& 用于 create<T>()
// 解法：让 ASTToIRConverter 内部持有 IRContext 或由外部传入

// 当前 ASTToIRConverter 构造函数签名：
// ASTToIRConverter(IRContext&, IRTypeContext&, const TargetLayout&, DiagsEngine&)
// 需要 IRContext

// 方案：CppFrontend 持有 IRContext_ 成员
std::unique_ptr<ir::IRContext> IRCtx_;

// compile() 中：
IRCtx_ = std::make_unique<ir::IRContext>();
ASTToIRConverter Converter(*IRCtx_, TypeCtx, Layout, Diags_);
// ...
return Result.takeModule();
// IRModule 引用 TypeCtx（外部），不引用 IRCtx_ 内部的 TypeCtx
// 但 IRValue/IRConstant 是通过 IRContext::create<> 分配的
// 如果 IRCtx_ 被销毁，这些值将失效
// 所以 IRCtx_ 必须与 IRModule 生命周期绑定
```

**最终决策**：`IRCtx_` 作为类成员保持存活，直到下一次 `compile()` 调用或 `CppFrontend` 析构。调用方应确保 `CppFrontend` 在 `IRModule` 使用期间不被销毁。

---

## 六、契约验证（已实现，直接调用）

### 6.1 已有的契约验证 API

所有 6 项契约验证已在 `IR/IRContract.h` + `IR/IRContract.cpp` 中实现：

| 契约 | 函数 | 签名 |
|------|------|------|
| C1 | `verifyIRModuleContract` | `bool (const IRModule&)` — 模块名非空 |
| C2 | `verifyTypeCompleteness` | `bool (const IRModule&)` — 函数签名/全局变量无 Opaque |
| C3 | `verifyFunctionNonEmpty` | `bool (const IRModule&)` — 定义函数至少 1 个 BB |
| C4 | `verifyTerminatorContract` | `bool (const IRModule&)` — 每个 BB 有终结指令 |
| C5 | `verifyTypeConsistency` | `bool (const IRModule&)` — 二元操作数类型一致 |
| C6 | `verifyTargetTripleValid` | `bool (const IRModule&)` — Triple 格式含 '-' |
| 全部 | `verifyAllContracts` | `IRVerificationResult (const IRModule&)` |

### 6.2 在 compile() 中调用

```cpp
if (Opts_.VerifyIR) {
  ir::IRVerificationResult VR = ir::contract::verifyAllContracts(*Result.getModule());
  if (!VR.isValid()) {
    for (const auto& Violation : VR.getViolations()) {
      // 报告到 DiagnosticsEngine
      // 需要确认 Diags_.report() 的精确签名
    }
    // 选项：返回 nullptr（严格模式）或继续（宽松模式）
    // 默认：仅报告警告，不中断编译
  }
}
```

### 6.3 VerifierPass（可选补充）

`VerifierPass`（`IR/IRVerifier.h`）提供更详细的 IR 验证，可作为额外检查：

```cpp
if (Opts_.VerifyIR) {
  // 轻量契约验证
  auto VR = ir::contract::verifyAllContracts(*Result.getModule());
  // ...

  // 详细 VerifierPass（可选）
  ir::SmallVector<ir::VerificationDiagnostic, 32> Errors;
  ir::VerifierPass VP(&Errors);
  if (!VP.run(*Result.getModule())) {
    for (const auto& E : Errors) {
      // 报告详细错误
    }
  }
}
```

---

## 七、Parser::parseTranslationUnit() 确认

```cpp
// Parse/Parser.h:119
/// Parses a translation unit.
TranslationUnitDecl* parseTranslationUnit();
```

返回 `TranslationUnitDecl*`，与 `ASTToIRConverter::convert(TranslationUnitDecl*)` 衔接。

---

## 八、CMakeLists.txt 修改

```cmake
# src/Frontend/CMakeLists.txt
# 在 blocktype_frontend 源文件列表中添加：
  CppFrontend.cpp
  CppFrontendRegistration.cpp
```

```cmake
# tests/unit/Frontend/CMakeLists.txt
# 在测试源文件列表中添加：
  CppFrontendTest.cpp
```

---

## 九、测试方案（8 个 GoogleTest 用例）

```cpp
// CppFrontendTest.cpp
#include <gtest/gtest.h>
#include "blocktype/Frontend/CppFrontend.h"
#include "blocktype/Frontend/FrontendRegistry.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/TargetLayout.h"

using namespace blocktype;
using namespace blocktype::frontend;

class CppFrontendTest : public ::testing::Test {
protected:
  DiagnosticsEngine Diags;
  FrontendCompileOptions Opts;
  ir::IRContext IRCtx;
  ir::IRTypeContext& TC;
  ir::TargetLayout Layout;

  CppFrontendTest()
    : TC(IRCtx.getTypeContext()),
      Layout("x86_64-unknown-linux-gnu") {
    Opts.TargetTriple = "x86_64-unknown-linux-gnu";
  }
};

// T1: 构造 + getName/getLanguage
TEST_F(CppFrontendTest, BasicProperties) {
  CppFrontend FE(Opts, Diags);
  EXPECT_EQ(FE.getName(), "cpp");
  EXPECT_EQ(FE.getLanguage(), "c++");
}

// T2: canHandle — 正确扩展名
TEST_F(CppFrontendTest, CanHandleValidExtensions) {
  CppFrontend FE(Opts, Diags);
  EXPECT_TRUE(FE.canHandle("test.cpp"));
  EXPECT_TRUE(FE.canHandle("test.cc"));
  EXPECT_TRUE(FE.canHandle("test.cxx"));
  EXPECT_TRUE(FE.canHandle("test.c"));
  EXPECT_TRUE(FE.canHandle("test.h"));
  EXPECT_TRUE(FE.canHandle("test.hpp"));
}

// T3: canHandle — 无效扩展名
TEST_F(CppFrontendTest, CannotHandleInvalidExtensions) {
  CppFrontend FE(Opts, Diags);
  EXPECT_FALSE(FE.canHandle("test.bt"));
  EXPECT_FALSE(FE.canHandle("test.py"));
  EXPECT_FALSE(FE.canHandle("test.rs"));
  EXPECT_FALSE(FE.canHandle("Makefile"));
}

// T4: compile() — 不存在的文件
TEST_F(CppFrontendTest, CompileNonexistentFile) {
  CppFrontend FE(Opts, Diags);
  auto Mod = FE.compile("nonexistent_file.cpp", TC, Layout);
  EXPECT_EQ(Mod, nullptr);
}

// T5: compile() — 简单函数定义
TEST_F(CppFrontendTest, CompileSimpleFunction) {
  // 需要创建临时源文件
  const char* Source = "int main() { return 0; }";
  // 写入临时文件，编译，验证
  // 或：如果 SourceManager 支持 from-memory，直接用内存
  // 这取决于 Parser 是否支持从字符串解析
  // ... 实际测试可能需要文件系统支持
}

// T6: FrontendRegistry 注册验证
TEST_F(CppFrontendTest, RegistryRegistration) {
  auto& Reg = FrontendRegistry::instance();
  EXPECT_TRUE(Reg.hasFrontend("cpp"));

  auto FE = Reg.create("cpp", Opts, Diags);
  ASSERT_NE(FE, nullptr);
  EXPECT_EQ(FE->getName(), "cpp");
}

// T7: FrontendRegistry autoSelect
TEST_F(CppFrontendTest, RegistryAutoSelect) {
  auto& Reg = FrontendRegistry::instance();
  auto FE = Reg.autoSelect("test.cpp", Opts, Diags);
  ASSERT_NE(FE, nullptr);
  EXPECT_EQ(FE->getName(), "cpp");
}

// T8: 契约验证（使用空模块）
TEST_F(CppFrontendTest, ContractVerification) {
  ir::IRContext TmpCtx;
  ir::IRModule Mod("test", TmpCtx.getTypeContext(), "x86_64-unknown-linux-gnu");

  auto VR = ir::contract::verifyAllContracts(Mod);
  // 空模块应通过契约验证
  EXPECT_TRUE(VR.isValid());
}
```

---

## 十、验收标准

| 版本 | 验收项 | 验证方式 |
|------|--------|---------|
| V1 | CppFrontend 构造 + 属性 | `getName() == "cpp"`, `getLanguage() == "c++"` |
| V2 | canHandle() 扩展名判断 | 正确识别 `.cpp/.cc/.cxx/.c/.h/.hpp/.hxx` |
| V3 | FrontendRegistry 注册 | `hasFrontend("cpp")` + `create("cpp")` 非 null |
| V4 | compile() 基本流程 | 简单源文件 → IRModule 非 null（依赖 Parser 可用性） |
| V5 | 契约验证集成 | `Opts.VerifyIR = true` 时调用 `verifyAllContracts` |
| V6 | 不存在的文件 | `compile()` 返回 nullptr |

**降级说明**：
- 原始 V4（差分测试）为集成测试级别，需要完整的编译器流水线（AST→BTIR→LLVM IR→Object），超出 B.10 单任务范围。留待 Phase B 整体验收时执行。
- V5 中的 `compile()` 可能需要 Parser 完整实现才能成功解析。如果 Parser 尚未完全实现，可用 mock 测试替代。

---

## 十一、实现风险与注意事项

### 11.1 IRContext 生命周期

这是最关键的架构问题。`IRModule` 引用 `IRTypeContext&`，而 `IRValue`/`IRConstant` 通过 `IRContext::create<T>()` 在 BumpPtrAllocator 中分配。如果 `IRContext` 在 `IRModule` 之前被销毁，所有 IR 节点都将失效。

**缓解方案**：
1. `CppFrontend` 持有 `IRContext` 成员，生命周期与前端实例绑定
2. 调用方确保 `CppFrontend` 在 `IRModule` 使用期间不被销毁
3. 或：让 `compile()` 接受额外的 `IRContext&` 参数

### 11.2 DiagnosticsEngine::report() 签名

需要确认 `Diags_.report(DiagID, ...)` 的精确签名和可用的 `DiagID` 枚举值。如果 `err_cannot_open_input` 等不存在，需要使用通用错误报告方式。

### 11.3 Preprocessor 的 HeaderSearch/LanguageManager

`Preprocessor` 构造函数接受 `HeaderSearch*`、`LanguageManager*`、`FileManager*`（均可为 null）。V1 阶段传 null 即可，后续补充完整的头文件搜索支持。

### 11.4 Parser 的错误恢复

`Parser::parseTranslationUnit()` 在遇到错误时可能返回部分 AST（非 null 但不完整）。`ASTToIRConverter` 已有 error recovery（`emitErrorPlaceholder()`），可处理不完整的 AST。

### 11.5 readSourceFile() 实现

需要简单的文件读取工具函数：

```cpp
std::string CppFrontend::readSourceFile(ir::StringRef Filename) {
  std::ifstream IF(Filename.str());
  if (!IF.is_open()) return {};
  std::ostringstream SS;
  SS << IF.rdbuf();
  return SS.str();
}
```

---

## 十二、文件组织总结

```
include/blocktype/Frontend/
  CppFrontend.h              ← 新增（~50 行）
  FrontendBase.h             ← 已存在，不修改
  FrontendRegistry.h         ← 已存在，不修改
  FrontendCompileOptions.h   ← 已存在，不修改

src/Frontend/
  CppFrontend.cpp            ← 新增（~120 行）
  CppFrontendRegistration.cpp ← 新增（~35 行）

include/blocktype/IR/
  IRContract.h               ← 已存在，不修改
  IRVerifier.h               ← 已存在，不修改

tests/unit/Frontend/
  CppFrontendTest.cpp        ← 新增（~100 行）
```

---

## 十三、Git 提交

```bash
git add include/blocktype/Frontend/CppFrontend.h \
        src/Frontend/CppFrontend.cpp \
        src/Frontend/CppFrontendRegistration.cpp \
        tests/unit/Frontend/CppFrontendTest.cpp
git commit -m "feat(B): 完成 Task B.10：CppFrontend 集成 + 契约验证 — Phase B 完成"
```

> 此提交完成 Phase B 全部任务（B.1~B.10）。
