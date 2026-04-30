# Phase C-C6 优化版：LLVMBackend 目标代码生成

> 基于 PhaseC-Planner-Report 审查后生成
> 无严重问题

---

## 依赖关系

- C.5（LLVMBackend 优化 pipeline）

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/LLVMBackend.cpp` |

---

## 实现约束

1. 支持 ELF/Mach-O/COFF 输出格式
2. 输出格式由 BackendOptions.OutputFormat 决定
3. emitAssembly 输出汇编文本
4. emitIRText 输出 LLVM IR 文本
5. 使用 llvm::TargetMachine::addPassesToEmitFile() 生成代码

---

## 验收标准

```cpp
// V1: 目标文件生成
auto BE = createLLVMBackend(Opts, Diags);
bool ok = BE->emitObject(*IRMod, "test.o");
assert(ok == true);
// 文件 test.o 存在且非空

// V2: 汇编输出
ok = BE->emitAssembly(*IRMod, "test.s");
assert(ok == true);

// V3: IR 文本输出
ir::SmallVector<char, 1024> Buf;
ir::raw_svector_ostream OS(Buf);
ok = BE->emitIRText(*IRMod, OS);
assert(ok == true);
assert(Buf.size() > 0);
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| C.5: LLVMBackend 框架 | 在此基础上补充 emitCode 实现 |
| C.5: convertToLLVM() | 生成 llvm::Module |
| C.5: optimizeLLVMModule() | 优化后调用 emitCode |

---

> Git 提交：`feat(C): 完成 C.6 — LLVMBackend 目标代码生成`
