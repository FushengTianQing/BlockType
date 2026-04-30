# Phase C-C4 优化版：IRToLLVMConverter 函数/模块/全局变量转换

> 基于 PhaseC-Planner-Report 审查后生成
> 无严重问题

---

## 依赖关系

- C.3（值/指令转换）

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/IRToLLVMConverter.cpp` |

---

## 实现约束

1. convertGlobalVariable 对 IROpaqueType 全局变量创建 `llvm::StructType` 占位
2. convertConstant 递归处理 IRConstantStruct/IRConstantArray
3. IRConstantFunctionRef → llvm::Function 对应查找（使用 FunctionMap）
4. IRConstantGlobalRef → llvm::GlobalVariable 对应查找

## 验收标准

```cpp
// V1: 全局变量转换
// IR: @g1 : i32 = 42
// LLVM: @g1 = global i32 42

// V2: 函数转换（含函数体）
// IR: function @add(i32 %a, i32 %b) -> i32 { ... }
// LLVM: define i32 @add(i32 %a, i32 %b) { ... }

// V3: 占位全局变量（VTable/RTTI）
// IR: @__vtbl_Node : opaque
// LLVM: @__vtbl_Node = external global %struct.__vtbl_Node

// V4: 端到端 convert
auto LLVMMod = Converter.convert(IRMod);
assert(LLVMMod != nullptr);
assert(LLVMMod->global_size() > 0);  // 有全局变量
assert(LLVMMod->getFunctionList().size() > 0);  // 有函数
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| C.2: mapType() | 全局变量类型映射 |
| C.3: mapValue()/mapFunction()/convertInstruction() | 函数体转换 |

---

> Git 提交：`feat(C): 完成 C.4 — IRToLLVMConverter 函数/模块/全局变量转换`
