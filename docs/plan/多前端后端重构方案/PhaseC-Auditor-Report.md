# Phase C 代码审查报告

## 审查结论：REJECTED

## 审查摘要
- 审查范围：C.1~C.9
- 差异数：22
- 严重差异数（P1）：6
- 中等差异数（P2）：11
- 轻微差异数（P3）：5

---

## 逐 Task 审查结果

### Task C.1：BackendBase + BackendRegistry + BackendOptions

- **接口签名匹配度**：✅ 完全匹配
- **产出文件完整性**：✅ 全部产出
  - `include/blocktype/Backend/BackendOptions.h` ✅
  - `include/blocktype/Backend/BackendBase.h` ✅
  - `include/blocktype/Backend/BackendRegistry.h` ✅
  - `src/Backend/BackendBase.cpp` ✅
  - `src/Backend/BackendRegistry.cpp` ✅
  - `src/Backend/CMakeLists.txt` ✅
- **实现约束遵守**：
  - BackendBase 不可拷贝 ✅
  - emitObject/emitAssembly/emitIRText 失败返回 false ✅（纯虚函数，签名正确）
  - BackendRegistry 全局单例 ✅（`static BackendRegistry Inst`）
  - autoSelect 默认选 "llvm" ✅
  - 命名空间 `blocktype::backend` ✅
  - 复用 `ir::IRFeature` 和 `ir::BackendCapability` ✅
- **验收标准覆盖**：✅ V1~V4 均有测试
- **差异列表**：无

---

### Task C.2：IRToLLVMConverter 类型转换

- **接口签名匹配度**：❌ 存在差异（见差异 #1~#5）
- **产出文件完整性**：✅
  - `include/blocktype/Backend/IRToLLVMConverter.h` ✅
  - `src/Backend/IRToLLVMConverter.cpp` ✅
- **实现约束遵守**：
  - mapType 缓存 ✅
  - 递归类型先占位再设 body ✅
  - convert 失败发出诊断 ❌（见差异 #6）
- **验收标准覆盖**：V1~V4 均有测试 ✅
- **差异列表**：
  - #1: 缺少 `template<typename T> class IRBuilder;` 前向声明
  - #2: 多余 `class GlobalVariable;` 前向声明
  - #3: 多余 `~IRToLLVMConverter()` 析构函数声明
  - #4: 多余 `GlobalVarMap` 成员变量
  - #5: `convertInstruction` 参数类型不同（`IRBuilderBase&` vs `IRBuilder<>&`）
  - #6: 类型转换失败使用 `llvm::errs()` 而非 `diag::err_ir_to_llvm_conversion_failed`

---

### Task C.3：IRToLLVMConverter 值/指令转换

- **接口签名匹配度**：❌ 继承 C.2 差异（`convertInstruction` 签名不同）
- **产出文件完整性**：✅ 修改 `src/Backend/IRToLLVMConverter.cpp`
- **实现约束遵守**：
  - mapValue 缓存 ✅
  - Phi 两遍处理 ✅（第二遍 addIncoming）
  - 未实现 Opcode 发出警告 ❌（见差异 #7）
- **Opcode 映射完整性**：
  - 终结指令（Ret/Br/CondBr/Switch/Invoke/Unreachable/Resume）✅
  - 整数二元（Add~SRem）✅
  - 浮点二元（FAdd~FRem）✅
  - 位运算（Shl~Xor）✅
  - 内存（Alloca/Load/Store/GEP/Memcpy/Memset）✅
  - 转换（Trunc~BitCast）✅
  - 比较（ICmp/FCmp）✅
  - 调用（Call）✅
  - 聚合（Phi/Select/ExtractValue/InsertValue/ExtractElement/InsertElement/ShuffleVector）❌（见差异 #8~#9）
  - 调试信息（DbgDeclare/DbgValue/DbgLabel）✅（简化跳过）
  - 原子操作（AtomicLoad/AtomicStore/Fence）❌（见差异 #10~#11）
  - FFI（FFICall/FFICheck/FFICoerce/FFIUnwind）✅
  - Cpp Dialect（DynamicCast/VtableDispatch/RTTITypeid）✅
  - 未映射（TargetIntrinsic/Meta*）✅（跳过）
- **差异列表**：
  - #7: 未映射 Opcode 未发出 `diag::warn_ir_opcode_not_supported` 警告
  - #8: `ExtractValue` 空实现（仅 `break;`）
  - #9: `InsertValue` 空实现（仅 `break;`）
  - #10: `AtomicRMW` 空实现（仅 `break;`）
  - #11: `AtomicCmpXchg` 空实现（仅 `break;`）

---

### Task C.4：IRToLLVMConverter 函数/模块/全局变量转换

- **接口签名匹配度**：✅（产出仅为 .cpp 修改）
- **产出文件完整性**：✅ 修改 `src/Backend/IRToLLVMConverter.cpp`
- **实现约束遵守**：
  - convertGlobalVariable 对 IROpaqueType 创建占位 ✅（通过 mapType 的 Opaque case）
  - convertConstant 递归处理 IRConstantStruct/IRConstantArray ✅
  - IRConstantFunctionRef → llvm::Function 查找 ✅
  - IRConstantGlobalRef → llvm::GlobalVariable 查找 ✅
- **验收标准覆盖**：V1~V4 均有测试 ✅
- **差异列表**：无额外差异

---

### Task C.5：LLVMBackend 优化 Pipeline

- **接口签名匹配度**：✅ 完全匹配
- **产出文件完整性**：✅
  - `include/blocktype/Backend/LLVMBackend.h` ✅
  - `src/Backend/LLVMBackend.cpp` ✅
- **实现约束遵守**：
  - LLVMCtx 构造时创建 ✅
  - LLVMCtx 生命周期（在 TheModule 之前销毁）✅（unique_ptr 成员顺序保证）
  - optimizeLLVMModule 使用 llvm::PassBuilder ❌（见差异 #12）
  - emitCode 使用 addPassesToEmitFile ✅
  - checkCapability 在 emitObject 开头调用 ✅
  - canHandle 对所有已知 TargetTriple 返回 true ✅
  - optimize 默认返回 true ✅
- **getCapability 实现**：✅ 精确匹配 spec
- **验收标准覆盖**：V1~V4 均有测试 ✅
- **差异列表**：
  - #12: `optimizeLLVMModule` 实际为空实现，仅 `return true;`，未使用 `llvm::PassBuilder`
  - #13: `LLVMBackend.cpp` 第 23 行有游离的 `// LLVMBackend 构造` 注释（缩进异常）
  - #14: `optimizeLLVMModule` 第 91~92 行重复注释

---

### Task C.6：LLVMBackend 目标代码生成

- **接口签名匹配度**：✅
- **产出文件完整性**：✅ 修改 `src/Backend/LLVMBackend.cpp`
- **实现约束遵守**：
  - 支持 ELF/Mach-O/COFF ✅（通过 TargetMachine）
  - 输出格式由 BackendOptions.OutputFormat 决定 ⚠️（实际通过 FileType 参数区分 0=Object/1=Assembly，OutputFormat 字段未直接使用）
  - emitAssembly 输出汇编文本 ✅
  - emitIRText 输出 LLVM IR 文本 ✅
  - 使用 addPassesToEmitFile ✅
- **验收标准覆盖**：V1~V3 部分覆盖（emitIRText 有测试，emitObject/emitAssembly 需要实际文件系统，测试中未验证文件生成）
- **差异列表**：
  - #15: `emitCode` 使用裸指针 `new`/`delete` 管理 `TargetMachine`，应使用 `unique_ptr`

---

### Task C.7：LLVMBackend 调试信息转换

- **接口签名匹配度**：✅（产出为修改 .cpp）
- **产出文件完整性**：✅ 修改 `src/Backend/IRToLLVMConverter.cpp` + `src/Backend/LLVMBackend.cpp`
- **实现约束遵守**：
  - 使用 llvm::DIBuilder ⚠️（见差异 #16）
  - DWARF5 默认格式 ⚠️（BackendOptions.DebugInfoFormat 未使用）
  - 无调试信息不生成调试段 ⚠️
  - 调试信息映射在 convert() 内部 ❌（见差异 #16）
- **差异列表**：
  - #16: 调试信息映射在 `emitIRText()` 中而非 `IRToLLVMConverter::convert()` 内部（违反实现约束 #4）
  - #17: 未使用 `BackendOptions.DebugInfoFormat` 控制 DWARF 版本
  - #18: `emitIRText` 中 DIBuilder 仅检查已有 debug loc，未从 `ir::debug::IRInstructionDebugInfo` 转换
  - #19: 未使用 spec 中提到的 IR 调试类型（ir::DICompileUnit/ir::DIType/ir::DISubprogram/ir::DILocation）

---

### Task C.8：LLVMBackend VTable/RTTI 生成

- **接口签名匹配度**：✅（产出为修改 .cpp）
- **产出文件完整性**：✅ 修改 `src/Backend/IRToLLVMConverter.cpp` + `src/Backend/LLVMBackend.cpp`
- **实现约束遵守**：
  - 仅支持 Itanium ABI ❌（见差异 #20）
  - VTable 布局与 GCC/Clang 兼容 ❌
  - RTTI type_info 与 libstdc++/libc++ 兼容 ❌
- **差异列表**：
  - #20: `DynamicCast` 仅做 `BitCast`，未实现 `__dynamic_cast` 运行时调用
  - #21: `VtableDispatch` 仅做 `BitCast`，未实现虚表查找 + 间接调用
  - #22: `RTTITypeid` 仅做 `BitCast`，未实现 type_info 全局变量查找

---

### Task C.9：集成测试

- **接口签名匹配度**：N/A
- **产出文件完整性**：❌（见差异 #23）
- **验收标准覆盖**：V1/V3 覆盖 ✅，V2 注释说明需 Phase D ✅
- **差异列表**：
  - #23: Spec 要求新增 `tests/integration/backend/` 目录下的测试文件，实际集成测试合并到了 `tests/unit/Backend/IRToLLVMConverterTest.cpp`

---

## 差异汇总表

| # | Task | 差异类型 | 严重程度 | 描述 | 建议修复 |
|---|------|---------|---------|------|---------|
| 1 | C.2 | 接口偏离 | P3 | `IRToLLVMConverter.h` 缺少 `template\<typename T\> class IRBuilder;` 前向声明 | 补充前向声明 |
| 2 | C.2 | 多余 | P3 | `IRToLLVMConverter.h` 多余 `class GlobalVariable;` 前向声明 | 移除或在 spec 中补充说明 |
| 3 | C.2 | 接口偏离 | P3 | `IRToLLVMConverter.h` 多余 `~IRToLLVMConverter()` 析构函数声明（spec 无此声明） | 实际需要（unique_ptr\<llvm::Module\> 要求完整类型），建议更新 spec |
| 4 | C.2 | 多余 | P3 | `IRToLLVMConverter.h` 多余 `GlobalVarMap` 成员变量（C.2 spec 无此成员） | 实际为 C.4 所需，合理偏离，建议更新 spec |
| 5 | C.2 | 接口偏离 | P2 | `convertInstruction` 参数类型 `IRBuilderBase&` ≠ spec 的 `IRBuilder\<\>&` | 改为 `llvm::IRBuilder\<\>&` |
| 6 | C.2 | 实现偏离 | P2 | 类型转换失败用 `llvm::errs()` 输出，未使用 `diag::err_ir_to_llvm_conversion_failed` 诊断 | 改用 Diags 发出诊断 |
| 7 | C.3 | 实现偏离 | P2 | 未映射 Opcode（TargetIntrinsic/Meta*）未发出 `diag::warn_ir_opcode_not_supported` | 补充警告诊断 |
| 8 | C.3 | 遗漏 | P2 | `ExtractValue` 空实现（仅 `break;`） | 补充 ExtractValue 转换逻辑 |
| 9 | C.3 | 遗漏 | P2 | `InsertValue` 空实现（仅 `break;`） | 补充 InsertValue 转换逻辑 |
| 10 | C.3 | 遗漏 | P2 | `AtomicRMW` 空实现（仅 `break;`） | 补充 AtomicRMW 转换逻辑 |
| 11 | C.3 | 遗漏 | P2 | `AtomicCmpXchg` 空实现（仅 `break;`） | 补充 AtomicCmpXchg 转换逻辑 |
| 12 | C.5 | 实现偏离 | P2 | `optimizeLLVMModule` 为空实现，未使用 `llvm::PassBuilder`（spec 约束 #3） | 使用 PassBuilder 或更新 spec 为 "Phase C 初始版本可为空" |
| 13 | C.5 | 代码质量 | P3 | `LLVMBackend.cpp:23` 游离注释 `// LLVMBackend 构造` 缩进异常 | 修正注释位置或删除 |
| 14 | C.5 | 代码质量 | P3 | `LLVMBackend.cpp:91~92` 重复注释行 | 删除重复注释 |
| 15 | C.6 | 代码质量 | P3 | `emitCode` 使用裸指针 `new`/`delete` 管理 TargetMachine | 改用 `unique_ptr` |
| 16 | C.7 | 实现偏离 | P1 | 调试信息映射在 `emitIRText()` 而非 `IRToLLVMConverter::convert()` 内部 | 将 DIBuilder 逻辑移入 convert() |
| 17 | C.7 | 遗漏 | P1 | 未使用 `BackendOptions.DebugInfoFormat` 控制 DWARF 版本 | 补充 DWARF 版本控制 |
| 18 | C.7 | 遗漏 | P1 | 未从 `ir::debug::IRInstructionDebugInfo` 转换调试信息 | 补充 IR→LLVM 调试信息映射 |
| 19 | C.7 | 遗漏 | P1 | 未使用 spec 中提到的 IR 调试类型（ir::DICompileUnit 等） | 补充 IR 调试类型到 LLVM 调试类型的映射 |
| 20 | C.8 | 实现偏离 | P1 | DynamicCast 仅做 BitCast，未实现 `__dynamic_cast` 运行时调用 | 实现完整的 Itanium ABI __dynamic_cast |
| 21 | C.8 | 实现偏离 | P1 | VtableDispatch 仅做 BitCast，未实现虚表查找+间接调用 | 实现虚表查找和间接调用 |
| 22 | C.8 | 实现偏离 | P1 | RTTITypeid 仅做 BitCast，未实现 type_info 全局变量查找 | 实现 type_info 查找 |
| 23 | C.9 | 文件偏离 | P2 | 集成测试未放在 `tests/integration/backend/` 目录 | 创建独立集成测试文件 |

---

## 最终结论

- [ ] APPROVED — 可以提交
- [x] REJECTED — 需要修复后重新审查

### 主要问题总结

**P1 严重问题（6个）**：
1. **C.7 调试信息架构偏离**（#16~#19）：调试信息转换未按 spec 设计在 `IRToLLVMConverter::convert()` 内部完成，而是错误地放在了 `LLVMBackend::emitIRText()` 中。且未使用 IR 层的调试元数据类型（`ir::DICompileUnit` 等），未使用 `DebugInfoFormat` 控制 DWARF 版本。
2. **C.8 VTable/RTTI 为占位实现**（#20~#22）：`DynamicCast`、`VtableDispatch`、`RTTITypeid` 均仅做简单的 `BitCast`，未实现 Itanium ABI 的虚表布局、`__dynamic_cast` 运行时调用和 `type_info` 查找。与 spec 中"仅支持 Itanium ABI"、"VTable 布局与 GCC/Clang 兼容"的约束严重不符。

**P2 中等问题（11个）**：
- `convertInstruction` 签名偏差（#5）
- 诊断消息未使用 Diags 机制（#6~#7）
- 4 个 Opcode 空实现（#8~#11）
- optimizeLLVMModule 空实现（#12）
- 集成测试位置偏离（#23）

**P3 轻微问题（5个）**：
- 前向声明多余/缺失（#1~#2）
- 析构函数和 GlobalVarMap 偏离但合理（#3~#4）
- 代码风格问题（#13~#15）

---

> 审查时间：2026-04-30
> 审查员：auditor
