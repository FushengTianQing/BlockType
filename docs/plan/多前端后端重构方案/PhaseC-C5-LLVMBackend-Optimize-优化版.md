# Phase C-C5 优化版：LLVMBackend 优化 pipeline

> 基于 PhaseC-Planner-Report 审查后生成
> 修复内容：getCapability 与 ir::BackendCaps::LLVM() 协调、成员命名统一为 Converter

---

## 依赖关系

- C.4（IRToLLVMConverter 完整功能）

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/LLVMBackend.h` |
| 新增 | `src/Backend/LLVMBackend.cpp` |

---

## 完整接口签名

```cpp
// === include/blocktype/Backend/LLVMBackend.h ===
#pragma once
#include "blocktype/Backend/BackendBase.h"

// LLVM 前向声明
namespace llvm {
  class LLVMContext;
  class Module;
  class TargetMachine;
} // namespace llvm

namespace blocktype::backend {

class IRToLLVMConverter;

class LLVMBackend : public BackendBase {
  std::unique_ptr<llvm::LLVMContext> LLVMCtx;
  std::unique_ptr<IRToLLVMConverter> Converter;

public:
  LLVMBackend(const BackendOptions& Opts, DiagnosticsEngine& Diags);

  ir::StringRef getName() const override { return "llvm"; }
  bool emitObject(ir::IRModule& IRModule, ir::StringRef OutputPath) override;
  bool emitAssembly(ir::IRModule& IRModule, ir::StringRef OutputPath) override;
  bool emitIRText(ir::IRModule& IRModule, ir::raw_ostream& OS) override;
  bool canHandle(ir::StringRef TargetTriple) const override;
  bool optimize(ir::IRModule& IRModule) override;
  ir::BackendCapability getCapability() const override;

private:
  std::unique_ptr<llvm::Module> convertToLLVM(ir::IRModule& IRModule);
  bool optimizeLLVMModule(llvm::Module& M, unsigned OptLevel);
  bool emitCode(llvm::Module& M, ir::StringRef OutputPath,
                unsigned FileType);  // 0=Object, 1=Assembly
  bool checkCapability(ir::IRModule& IRModule);
};

/// 工厂函数：创建 LLVMBackend 实例
std::unique_ptr<BackendBase> createLLVMBackend(
  const BackendOptions& Opts, DiagnosticsEngine& Diags);

} // namespace blocktype::backend
```

---

## getCapability 精确实现

```cpp
ir::BackendCapability LLVMBackend::getCapability() const {
  ir::BackendCapability Cap;
  Cap.declareFeature(ir::IRFeature::IntegerArithmetic);
  Cap.declareFeature(ir::IRFeature::FloatArithmetic);
  Cap.declareFeature(ir::IRFeature::VectorOperations);
  Cap.declareFeature(ir::IRFeature::AtomicOperations);
  Cap.declareFeature(ir::IRFeature::ExceptionHandling);
  Cap.declareFeature(ir::IRFeature::DebugInfo);
  Cap.declareFeature(ir::IRFeature::VarArg);
  Cap.declareFeature(ir::IRFeature::SeparateFloatInt);
  Cap.declareFeature(ir::IRFeature::StructReturn);
  Cap.declareFeature(ir::IRFeature::DynamicCast);
  Cap.declareFeature(ir::IRFeature::VirtualDispatch);
  // 注意：Coroutines 暂不支持（与 ir::BackendCaps::LLVM() 不同）
  // 实际实现中可选择复用 ir::BackendCaps::LLVM() 或自行声明
  return Cap;
}
```

---

## 实现约束

1. LLVMCtx 在 LLVMBackend 构造时创建，析构时销毁
2. LLVMCtx 必须在 TheModule 之前销毁（生命周期约束）
3. optimizeLLVMModule 使用 llvm::PassBuilder
4. emitCode 使用 llvm::TargetMachine::addPassesToEmitFile()
5. checkCapability 在 emitObject 开头调用，不支持的不可降级特性中止编译
6. canHandle 对所有已知 TargetTriple 返回 true（LLVM 后端通用）
7. optimize 默认实现为空（返回 true），具体优化在 convertToLLVM 后的 LLVM 层执行

---

## 验收标准

```cpp
// V1: LLVMBackend 编译 IRModule 为目标文件
auto BE = createLLVMBackend(Opts, Diags);
bool ok = BE->emitObject(*IRMod, "test.o");
assert(ok == true);

// V2: 能力检查
auto Cap = BE->getCapability();
assert(Cap.hasFeature(ir::IRFeature::IntegerArithmetic) == true);
assert(Cap.hasFeature(ir::IRFeature::Coroutines) == false);

// V3: getName
assert(BE->getName() == "llvm");

// V4: canHandle
assert(BE->canHandle("x86_64-unknown-linux-gnu") == true);
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| C.1: BackendBase | LLVMBackend 的基类 |
| C.1: BackendOptions | 构造参数 |
| C.2~C.4: IRToLLVMConverter | convertToLLVM() 内部使用 |
| Phase A: ir::BackendCapability | getCapability() 返回类型 |

---

> Git 提交：`feat(C): 完成 C.5 — LLVMBackend 优化 pipeline`
