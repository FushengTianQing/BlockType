# 三、渐进式改造方案 — Phase C/D/E

> Phase C 后端抽象层、Phase D 管线重构、Phase E 验证迁移

---

## Phase C：后端抽象层 + LLVM 后端适配

**目标**：建立 BackendBase/BackendRegistry，实现 LLVMBackend，实现 IRToLLVMConverter。

### 红线对照

| 红线 | 合规性 | 说明 |
|------|--------|------|
| R1 架构优先 | ✓ | 后端通过抽象接口与前端解耦 |
| R2 自由组合 | ✓ | 后端可独立替换 |
| R3 渐进式 | ✓ | LLVMBackend与现有编译路径并行 |
| R4 功能不退化 | ✓ | 现有功能通过新路径等价实现 |
| R5 接口抽象 | ✓ | BackendBase抽象接口 |
| R6 IR中间层解耦 | ✓ | 后端消费IRModule，不直接接触AST |

### Task 分解

| Task | 内容 | 预估 | 输入 | 输出 | 实现约束 | 验收标准 | 测试用例 | 依赖文件 |
|------|------|------|------|------|---------|---------|---------|---------|
| C.1 | BackendBase+BackendRegistry | 1天 | IRModule, BackendCapability | BackendBase.h, BackendRegistry.h, 对应.cpp | 1.BackendBase::emitObject()纯虚函数 2.BackendRegistry不依赖具体后端 3.autoSelect根据TargetTriple选择 | 1.BackendRegistry::registerBackend("llvm", LLVMBackend)成功 2.BackendRegistry::autoSelect("x86_64-linux")→"llvm" 3.BackendRegistry::create("llvm")→非空 | registerBackend("llvm")→成功; autoSelect("x86_64-linux")→"llvm" | IRModule.h, BackendCapability.h |
| C.1.1 | BackendCapability+LLVM后端能力声明 | 1天 | IRFeature枚举, BackendCapability | BackendCapability.h, 对应.cpp | 1.IRFeature枚举覆盖所有特性(见02 §2.1.15) 2.LLVM后端声明除Coroutines外所有特性 3.能力检查集成到编译管线 | 1.LLVMBackend::getCapability().hasFeature(ExceptionHandling)==true 2.CraneliftBackend::getCapability().hasFeature(ExceptionHandling)==false 3.getUnsupported()正确返回不支持的特性 | LLVM.hasFeature(ExceptionHandling)→true; Cranelift.hasFeature(ExceptionHandling)→false | IRModule.h, BackendBase.h |
| C.2 | IRToLLVMConverter:类型转换 | 2天 | IRType体系, LLVM Type体系 | IRToLLVMConverter.h, IRToLLVMConverter.cpp(类型部分) | 1.所有IRType→llvm::Type映射(见04 §2.3.1) 2.类型映射缓存 3.IROpaqueType→llvm::StructType::create()占位 | 1.mapType(IRIntegerType(32))→llvm::Type::getInt32Ty() 2.mapType(IRFloatType(80))→llvm::Type::getX86_FP80Ty() 3.mapType(IROpaqueType("T"))→命名结构体占位 | mapType(IRInt32)→LLVM Int32; mapType(IRFloat80)→X86_FP80 | IRType.h, LLVM/IR/IRBuilder.h |
| C.3 | IRToLLVMConverter:值/指令转换 | 3天 | IRInstruction, IRBuilder, LLVM IRBuilder | IRToLLVMConverter.cpp(指令部分) | 1.所有Opcode→LLVM指令映射(见04 §2.3.1) 2.值映射缓存 3.失败时发出diag并跳过该函数 | 1.convertInstruction(Add i32)→Builder.CreateAdd() 2.convertInstruction(Call)→Builder.CreateCall() 3.convertInstruction(Invoke)→Builder.CreateInvoke() | convertInstruction(Add)→CreateAdd; convertInstruction(Call)→CreateCall | IRInstruction.h, LLVM IRBuilder |
| C.4 | IRToLLVMConverter:函数/模块转换 | 2天 | IRModule, IRFunction, LLVM Module | IRToLLVMConverter.cpp(函数/模块部分) | 1.IRModule→llvm::Module映射 2.IRFunction→llvm::Function映射(含参数) 3.IRGlobalVariable→llvm::GlobalVariable映射 | 1.convert(IRModule)→unique_ptr<llvm::Module> 2.IRFunction映射后参数数量一致 3.IRGlobalVariable映射后类型一致 | convert(IRModule)→非空llvm::Module; 函数参数数量一致 | IRModule.h, IRFunction.h, LLVM Module.h |
| C.5 | LLVMBackend:优化pipeline | 1天 | LLVM Module, llvm::PassBuilder | LLVMBackend.h, LLVMBackend.cpp(优化部分) | 1.复用现有llvm::PassBuilder 2.这是后端实现细节，不暴露给IR层 3.支持-O0/-O1/-O2/-O3 | 1.-O2优化后IR指令数减少 2.-O0无优化 3.优化后目标代码可执行 | -O2→指令数减少; -O0→无优化 | LLVM PassBuilder.h |
| C.6 | LLVMBackend:目标代码生成 | 1天 | LLVM Module, llvm::TargetMachine | LLVMBackend.cpp(代码生成部分) | 1.复用现有TargetMachine逻辑 2.支持ELF/Mach-O/COFF输出 3.emitObject()和emitAssembly() | 1.emitObject()→生成.o文件 2.emitAssembly()→生成.s文件 3.目标文件可链接执行 | emitObject()→.o文件存在; 链接执行→输出正确 | LLVM TargetMachine.h |
| C.7 | LLVMBackend:调试信息转换 | 2天 | ir::debug::IRDebugMetadata, LLVM DIBuilder | LLVMBackend.cpp(调试部分) | 1.ir::debug::DICompileUnit→llvm::DICompileUnit 2.ir::debug::DIType→llvm::DIType 3.ir::debug::DISubprogram→llvm::DISubprogram | 1.-g选项生成DWARF5调试段 2.调试信息中的源位置与原始代码一致 3.gdb/lldb可读取调试信息 | -g→.debug_info段存在; lldb→断点命中 | IRDebugMetadata.h, LLVM DIBuilder.h |
| C.8 | LLVMBackend:VTable/RTTI生成 | 2天 | IRModule中的占位全局变量, Itanium ABI | LLVMBackend.cpp(VTable/RTTI部分) | 1.VTable布局按Itanium ABI 2.RTTI type_info结构按Itanium ABI 3.虚函数调用→虚表查找+间接调用 | 1.VTable全局变量布局正确 2.RTTI type_info结构正确 3.虚函数调用通过虚表正确分派 | VTable布局与g++一致; dynamic_cast→正确结果 | IRModule.h, Itanium ABI规范 |
| C.9 | 集成测试 | 2天 | 所有Phase C组件 | tests/下新增测试文件 | 1.端到端编译测试 2.目标代码可执行 3.调试信息可读 | 1.简单C程序编译→执行→输出正确 2.C++程序(含虚函数)编译→执行→输出正确 3.差分测试:新路径==旧路径 | 编译hello.c→执行→"Hello World"; 编译C++虚函数→执行→正确 | 所有Phase C文件 |

**Phase C 总计**：~28.5天（原16天 + 新增1天C.1.1 + 附加增强11.5人日）

### 附加增强任务（行业前沿补充）

> 来源：09-行业前沿补充优化方案 §3.3

| 任务ID | 任务描述 | 依赖 | 工作量估算 |
|--------|---------|------|-----------|
| C-F1 | PluginManager 基础框架 | Phase C FrontendRegistry | 2人日 |
| C-F2 | CompilerPlugin 基类 + 加载机制 | C-F1 | 2人日 |
| C-F3 | FFIFunctionDecl 基础实现 | Phase C 前端适配层 | 2人日 |
| C-F4 | FFITypeMapper C语言映射 | C-F3 | 1.5人日 |
| C-F5 | 前端模糊测试器 FrontendFuzzer 基础 | Phase C | 1.5人日 |
| C-F6 | SARIF格式诊断输出 | B-F6 | 1人日 |
| C-F7 | FixIt Hint 基础实现 | B-F6 | 1.5人日 |

**Phase C 附加增强工作量**：约 11.5 人日

### 文件变更清单

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/BackendBase.h` |
| 新增 | `include/blocktype/Backend/BackendRegistry.h` |
| 新增 | `include/blocktype/Backend/BackendOptions.h` |
| 新增 | `include/blocktype/Backend/BackendCapability.h` |
| 新增 | `include/blocktype/Backend/IRToLLVMConverter.h` |
| 新增 | `include/blocktype/Backend/LLVMBackend.h` |
| 新增 | `include/blocktype/Backend/ABIInfo.h`（从CodeGen移入） |
| 新增 | `src/Backend/` 下对应所有 .cpp 文件 |
| 新增 | `src/Backend/CMakeLists.txt` |

---

## Phase D：编译管线重构

**目标**：重构 CompilerInstance 为管线编排器，支持 `--frontend`/`--backend` 选项。

### 红线对照

| 红线 | 合规性 | 说明 |
|------|--------|------|
| R1 架构优先 | ✓ | CompilerInstance成为纯粹的编排器 |
| R2 自由组合 | ✓ | 前端/后端通过选项自由组合 |
| R3 渐进式 | ✓ | 渐进替换，旧路径保留到Phase E |
| R4 功能不退化 | ✓ | 默认行为不变（cpp+llvm） |
| R5 接口抽象 | ✓ | 通过FrontendBase*/BackendBase*接口操作 |
| R6 IR中间层解耦 | ✓ | 编排器只传递IRModule |

### Task 分解

| Task | 内容 | 预估 | 输入 | 输出 | 实现约束 | 验收标准 | 测试用例 | 依赖文件 |
|------|------|------|------|------|---------|---------|---------|---------|
| D.1 | CompilerInvocation新增FrontendName/BackendName选项 | 1天 | CompilerInvocation | 修改CompilerInvocation.h/.cpp | 1.新增FrontendName和BackendName字段 2.命令行--frontend/--backend解析 3.默认值"cpp"/"llvm" | 1.--frontend=cpp→FrontendName=="cpp" 2.--backend=llvm→BackendName=="llvm" 3.无选项→默认"cpp"/"llvm" | --frontend=cpp→"cpp"; 默认→"cpp" | CompilerInvocation.h |
| D.2 | CompilerInstance重构为管线编排器 | 2天 | CompilerInstance, FrontendRegistry, BackendRegistry | 修改CompilerInstance.h/.cpp | 1.CompilerInstance仅编排，不持有具体前端/后端 2.通过FrontendBase*/BackendBase*接口操作 3.IRModule作为前后端唯一交互媒介 | 1.CompilerInstance::compileFile()使用新管线 2.前端→IRModule→后端流程正确 3.编译结果与旧路径一致 | compileFile("test.cpp")→新管线→成功 | CompilerInstance.h, FrontendBase.h, BackendBase.h |
| D.3 | 保留旧路径作为fallback | 1天 | CompilerInstance | 修改CompilerInstance.cpp | 1.无--frontend/--backend时走旧路径 2.旧路径代码不删除 3.新旧路径可切换 | 1.无选项→旧路径编译 2.--frontend=cpp --backend=llvm→新路径 3.两种路径结果一致 | 无选项→旧路径; --frontend=cpp→新路径 | CompilerInstance.h |
| D.4 | 自动注册机制 | 1天 | FrontendRegistry, BackendRegistry | CppFrontendRegistration.cpp, LLVMBackendRegistration.cpp | 1.使用静态初始化注册 2.不依赖main()中的显式注册 3.注册顺序无关 | 1.CppFrontend自动注册到FrontendRegistry 2.LLVMBackend自动注册到BackendRegistry 3.编译时无需手动注册 | FrontendRegistry::hasFrontend("cpp")→true | FrontendRegistry.h, BackendRegistry.h |
| D.5 | 集成测试 | 1天 | 所有Phase D组件 | tests/下新增测试文件 | 1.所有编译场景通过 2.新旧路径结果一致 3.命令行选项正确 | 1.--frontend=cpp --backend=llvm编译通过 2.默认路径编译通过 3.差分测试:新路径==旧路径 | 新路径编译→成功; 旧路径编译→成功; 差分→一致 | 所有Phase D文件 |

**Phase D 总计**：~21.5天（原6天 + 附加增强15.5人日）

### 附加增强任务（行业前沿补充）

> 来源：09-行业前沿补充优化方案 §3.4

| 任务ID | 任务描述 | 依赖 | 工作量估算 |
|--------|---------|------|-----------|
| D-F1 | InstructionSelector 接口定义 | Phase D BackendBase | 1人日 |
| D-F2 | TargetInstruction 内部表示 | D-F1 | 1人日 |
| D-F3 | RegisterAllocator 接口定义 | Phase D | 1人日 |
| D-F4 | RegAllocFactory + Greedy/Basic实现 | D-F3 | 3人日 |
| D-F5 | DebugInfoEmitter 接口定义 | A-F5 | 0.5人日 |
| D-F6 | DWARF5调试信息发射（LLVM后端） | D-F5, Phase D LLVM后端 | 3人日 |
| D-F7 | 差分测试框架 DifferentialTester | Phase D 多后端 | 2人日 |
| D-F8 | IR模糊测试器 IRFuzzer 基础 | Phase D | 2人日 |
| D-F9 | 后端管线模块化拆分 | D-F1, D-F3 | 2人日 |

**Phase D 附加增强工作量**：约 15.5 人日

### 文件变更清单

| 操作 | 文件路径 |
|------|----------|
| 修改 | `include/blocktype/Frontend/CompilerInstance.h` |
| 修改 | `src/Frontend/CompilerInstance.cpp` |
| 修改 | `include/blocktype/Frontend/CompilerInvocation.h` |
| 修改 | `src/Frontend/CompilerInvocation.cpp` |
| 新增 | `src/Frontend/CppFrontendRegistration.cpp` |
| 新增 | `src/Backend/LLVMBackendRegistration.cpp` |

---

## Phase E：验证和迁移

**目标**：所有现有功能迁移到新架构，移除旧路径依赖，全面验证。

### 红线对照

| 红线 | 合规性 | 说明 |
|------|--------|------|
| R1 架构优先 | ✓ | 最终架构完全符合设计 |
| R2 自由组合 | ✓ | 前后端完全解耦 |
| R3 渐进式 | ✓ | 每个迁移步骤可独立验证 |
| R4 功能不退化 | ✓ | 全量测试回归 |
| R5 接口抽象 | ✓ | 无直接依赖具体实现 |
| R6 IR中间层解耦 | ✓ | 前后端仅通过IR交互 |

### Task 分解

| Task | 内容 | 预估 | 输入 | 输出 | 实现约束 | 验收标准 | 测试用例 | 依赖文件 |
|------|------|------|------|------|---------|---------|---------|---------|
| E.1 | 全量测试回归(新管线) | 2天 | 所有现有测试用例 | 无文件修改 | 1.所有现有测试必须通过 2.新管线为默认路径 3.发现失败必须修复 | 1.所有lit test通过 2.所有GTest通过 3.0个测试失败 | 所有lit test→PASS; 所有GTest→PASS | 所有测试文件 |
| E.2 | 性能基准测试 | 1天 | 基准测试源文件 | 基准测试结果文件 | 1.编译速度不低于旧路径95% 2.生成代码质量不低于旧路径 3.内存使用不超过旧路径130% | 1.编译速度≥旧路径95% 2.目标代码大小差异<5% 3.内存峰值≤旧路径130% | 编译速度比≥0.95; 代码大小差异<5% | 基准测试源文件, CompilerInstance |
| E.3 | 移除旧路径fallback | 1天 | CompilerInstance | 修改CompilerInstance.h/.cpp | 1.移除旧路径代码 2.移除fallback选项 3.编译通过 | 1.无fallback代码 2.CompilerInstance仅使用新管线 3.所有测试通过 | 编译→成功; 无fallback代码 | CompilerInstance.h, CompilerInstance.cpp |
| E.4 | CodeGen模块清理 | 2天 | CodeGen/* | 重构多个CodeGen文件 | 1.CodeGen仅被LLVM后端内部使用 2.移除对Frontend的直接依赖 3.CodeGenModule/CodeGenFunction改为LLVM后端内部类 | 1.CodeGen不#include FrontendBase.h 2.CodeGen仅被LLVMBackend.cpp引用 3.无外部代码引用CodeGen | CodeGen不依赖Frontend; CodeGen仅被Backend引用 | CodeGen/*, LLVMBackend.cpp |
| E.5 | 文档更新 | 1天 | 架构文档 | 更新docs/ | 1.架构文档与代码一致 2.API文档更新 3.用户文档更新 | 1.架构图反映新管线 2.API文档无过期引用 3.用户文档包含--frontend/--backend说明 | 文档与代码一致 | docs/* |

**Phase E 总计**：~22天（原7天 + 附加增强15人日）

### 附加增强任务（行业前沿补充）

> 来源：09-行业前沿补充优化方案 §3.5

| 任务ID | 任务描述 | 依赖 | 工作量估算 |
|--------|---------|------|-----------|
| E-F1 | CompilationCacheManager 完整集成 | B-F8 | 2人日 |
| E-F2 | IR缓存序列化/反序列化 | E-F1 | 2人日 |
| E-F3 | IREquivalenceChecker 语义等价检查 | B-F9 | 3人日 |
| E-F4 | Chrome Trace格式输出 | B-F5 | 1人日 |
| E-F5 | 可重现构建模式实现 | A-F8 | 2人日 |
| E-F6 | IR完整性验证集成 | A-F8 | 1人日 |
| E-F7 | FFICoerce/FFIUnwind 指令实现 | C-F3 | 2人日 |
| E-F8 | CodeView调试信息（Windows后端） | D-F5 | 2人日 |

**Phase E 附加增强工作量**：约 15 人日

### 文件变更清单

| 操作 | 文件路径 |
|------|----------|
| 修改 | 所有现有测试用例（添加新路径测试） |
| 删除 | `src/CodeGen/CodeGenModule.cpp`（旧路径，迁移完成后） |
| 删除 | `src/CodeGen/CodeGenFunction.cpp` |
| 删除 | `src/CodeGen/CodeGenExpr.cpp` |
| 删除 | `src/CodeGen/CodeGenStmt.cpp` |
| 删除 | `src/CodeGen/CodeGenTypes.cpp` |
| 删除 | `src/CodeGen/CodeGenConstant.cpp` |
| 删除 | `src/CodeGen/CGCXX.cpp` |
| 删除 | `src/CodeGen/CGDebugInfo.cpp` |
| 修改 | `CMakeLists.txt`（移除旧 CodeGen 源文件） |
