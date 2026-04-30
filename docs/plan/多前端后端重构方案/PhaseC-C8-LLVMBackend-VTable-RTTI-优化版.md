# Phase C-C8 优化版：LLVMBackend VTable/RTTI 生成

> 基于 PhaseC-Planner-Report 审查后生成
> 无严重问题

---

## 依赖关系

- C.5（LLVMBackend）
- Phase B Task B.7（IREmitCXX，VTable/RTTI 占位全局变量）

## 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/IRToLLVMConverter.cpp` |
| 修改 | `src/Backend/LLVMBackend.cpp` |

---

## VTable/RTTI 生成流程

```
1. 识别 IRModule 中的 IROpaqueType 全局变量（VTable/RTTI 占位符）
2. 根据 Itanium ABI 填充 VTable 布局：
   a. RTTI 指针（offset-to-top + typeinfo + 虚函数指针列表）
   b. 虚函数指针按声明顺序排列
   c. 多重继承的 VTT（Virtual Table Table）
3. 虚函数调用：IR 中的 VtableDispatch → LLVM 中的虚表查找 + 间接调用
4. dynamic_cast：IR 中的 DynamicCast → __dynamic_cast 运行时调用
5. RTTI type_info：IR 中的 RTTITypeid → type_info 全局变量查找
```

---

## 实现约束

1. 仅支持 Itanium ABI（不实现 MSVC ABI）
2. VTable 布局与 GCC/Clang 兼容
3. RTTI type_info 结构与 libstdc++/libc++ 兼容

---

## 验收标准

```cpp
// V1: VTable 生成
// 含虚函数的类 → VTable 全局变量不再为 opaque
// VTable 包含正确的虚函数指针列表

// V2: RTTI 生成
// 含 RTTI 的类 → type_info 全局变量不再为 opaque

// V3: 虚函数调用
// IR: VtableDispatch(%obj, 2) → LLVM: 间接调用虚表第3项
```

---

## 与前序 Task 的接口衔接

| 前序产出 | 本 Task 使用方式 |
|---------|----------------|
| C.5: LLVMBackend | emitObject 中处理 VTable/RTTI |
| B.7: IREmitCXXVTable | IR 中 VTable/RTTI 占位全局变量的来源 |
| C.3: VtableDispatch/DynamicCast/RTTITypeid Opcode | 指令转换 |

---

> Git 提交：`feat(C): 完成 C.8 — LLVMBackend VTable/RTTI 生成`
