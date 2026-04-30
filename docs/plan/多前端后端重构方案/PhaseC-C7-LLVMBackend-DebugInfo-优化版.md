# Phase C-C7 优化版：LLVMBackend 调试信息转换

> 基于 PhaseC-Planner-Report 审查后生成
> 无严重问题

---

## 依赖关系

- C.5（LLVMBackend）
- Phase A 增强任务 A-F5（IRDebugMetadata 基础类型）

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/IRToLLVMConverter.cpp` |
| 修改 | `src/Backend/LLVMBackend.cpp` |

---

## IR 调试元数据 → LLVM 调试元数据映射表

| IR 调试信息 | LLVM 调试信息 | 映射方式 |
|-------------|--------------|---------|
| ir::DICompileUnit | llvm::DICompileUnit | DIBuilder::createCompileUnit() |
| ir::DIType | llvm::DIType | DIBuilder::createBasicType/createStructType() |
| ir::DISubprogram | llvm::DISubprogram | DIBuilder::createFunction() |
| ir::DILocation | llvm::DILocation | DILocation::get() |

---

## 实现约束

1. 使用 llvm::DIBuilder 构建调试信息
2. DWARF5 为默认格式（由 BackendOptions.DebugInfoFormat 控制）
3. 无调试信息的 IRModule 不生成调试段
4. 调试信息映射发生在 IRToLLVMConverter::convert() 内部
5. SourceLocation 数据来自 ir::debug::IRInstructionDebugInfo

---

## 验收标准

```cpp
// V1: 含调试信息的 IRModule 转换
// IRModule 中 IRInstruction 带 DbgInfo
auto LLVMMod = Converter.convert(IRModWithDbg);
assert(LLVMMod != nullptr);
// llvm::Module 中有 DICompileUnit
assert(LLVMMod->debug_compile_units_begin() != LLVMMod->debug_compile_units_end());

// V2: 无调试信息的 IRModule 不生成调试段
auto LLVMMod2 = Converter.convert(IRModNoDbg);
assert(LLVMMod2 != nullptr);
// 无 DICompileUnit
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| C.5: LLVMBackend | emitObject 中处理 DebugInfo 选项 |
| Phase A: ir::DebugMetadata 系列 | 调试信息来源 |
| Phase A: ir::debug::IRInstructionDebugInfo | 指令级调试信息 |

---

> Git 提交：`feat(C): 完成 C.7 — LLVMBackend 调试信息转换`
