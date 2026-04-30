# Phase C-C9 优化版：集成测试

> 基于 PhaseC-Planner-Report 审查后生成
> 无严重问题

---

## 依赖关系

- C.1 ~ C.8 全部完成

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `tests/integration/backend/` 下所有测试文件 |

---

## 验收标准

```cpp
// V1: BackendRegistry 注册+创建+编译
auto& Reg = BackendRegistry::instance();
Reg.registerBackend("llvm", createLLVMBackend);
auto BE = Reg.create("llvm", Opts, Diags);
assert(BE != nullptr);
bool ok = BE->emitObject(*IRMod, "test.o");
assert(ok == true);

// V2: 完整管线（前端→IR→后端）
// 注：需要 Phase D 的 CompilerInstance 集成
// Phase C 阶段仅测试后端独立功能

// V3: 多平台目标
BackendOpts.TargetTriple = "x86_64-unknown-linux-gnu";
auto BE1 = Reg.create("llvm", BackendOpts, Diags);
assert(BE1 != nullptr);

BackendOpts.TargetTriple = "aarch64-unknown-linux-gnu";
auto BE2 = Reg.create("llvm", BackendOpts, Diags);
assert(BE2 != nullptr);
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| C.1: BackendRegistry | 注册和创建后端 |
| C.5: LLVMBackend | emitObject 端到端测试 |
| C.2~C.4: IRToLLVMConverter | 通过 LLVMBackend 间接测试 |
| C.7: 调试信息 | 验证含调试信息的目标文件 |
| C.8: VTable/RTTI | 验证含虚函数的 C++ 代码 |

---

> Git 提交：`feat(C): 完成 C.9 — 集成测试`
