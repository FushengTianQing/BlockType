# 三、渐进式改造方案 — Phase A/B

> 融合方案1的细粒度Task拆分+每Phase红线对照，和方案0的Phase B/C并行策略

---

## 改造阶段总览

```
Phase A ──→ Phase B ──→ Phase C ──→ Phase D ──→ Phase E ──→ Phase F ──→ Phase G ──→ Phase H
IR基础设施    前端抽象层   后端抽象层    管线重构      验证迁移    高级特性期   增量编译期   AI辅助与极致优化期
```

每个 Phase 完成后必须：编译通过 + 全部现有测试通过 + 新增 IR 层测试通过。

**Phase B/C 并行机会**（取自方案0）：
- Phase B 的 Task B.1-B.2（前端接口+基础转换）与 Phase C 的 Task C.1（后端接口）可并行
- Phase B 的 Task B.4-B.6（表达式/语句/C++ 特有）与 Phase C 的 Task C.2（IR→LLVM 转换）可部分并行

---

## Phase A：IR 层基础设施

**目标**：建立独立的 IR 库，包含类型系统、值系统、构建器、验证器、序列化。

### 红线对照

| 红线 | 合规性 | 说明 |
|------|--------|------|
| R1 架构优先 | ✓ | IR层独立于LLVM和AST，架构正确 |
| R2 自由组合 | ✓ | IR层是前后端自由组合的基础 |
| R3 渐进式 | ✓ | 纯新增代码，不影响现有功能 |
| R4 功能不退化 | ✓ | 现有功能零影响 |
| R5 接口抽象 | ✓ | IR层全部通过抽象接口设计 |
| R6 IR中间层解耦 | ✓ | IR层实现前后端解耦的核心机制 |

### Task 分解

| Task | 内容 | 预估 | 输入 | 输出 | 实现约束 | 验收标准 | 测试用例 | 依赖文件 |
|------|------|------|------|------|---------|---------|---------|---------|
| A.1 | IRType体系+IRTypeContext | 2天 | blocktype-basic ADT (StringRef/SmallVector/DenseMap) | IRType.h, IRTypeContext.h, IRType.cpp, IRTypeContext.cpp | 1.不依赖LLVM 2.IRType必须虚析构 3.getSizeInBits必须const 4.所有子类不可拷贝 | 1.IRIntegerType(32).getSizeInBits(Layout)==32 2.IRTypeContext::getInt32Ty()返回唯一指针 3.IRTypeContext::getPointerType(getInt8Ty())两次调用返回同一指针 | mapType(Int)→IRIntegerType(32); mapType(Float64)→IRFloatType(64); getPointerType两次→同一指针 | include/blocktype/Basic/*.h |
| A.1.1 | IRContext+BumpPtrAllocator内存管理 | 1.5天 | blocktype-basic ADT | IRContext.h, IRContext.cpp | 1.不依赖LLVM(使用自实现BumpPtrAllocator) 2.IRContext拥有所有IR节点内存 3.IRModule仅拥有逻辑结构 | 1.IRContext::create<IRIntegerType>(32)成功分配 2.~IRContext()自动释放所有内存 3.addCleanup()回调正确执行 | create<IRIntegerType>(32)→非空指针; 连续create 1000次→无内存泄漏 | include/blocktype/Basic/*.h |
| A.1.2 | IRThreadingMode枚举+seal接口 | 0.5天 | IRContext.h | 修改IRContext | 1.枚举值: SingleThread/MultiInstance/SharedReadOnly 2.sealModule()标记IRModule不可变 | 1.IRThreadingMode::SingleThread为默认值 2.sealModule()后IRModule不可修改 | setThreadingMode(MultiInstance)→getThreadingMode()==MultiInstance | IRContext.h |
| A.2 | TargetLayout(独立于LLVM DataLayout) | 1天 | blocktype-basic ADT | TargetLayout.h, TargetLayout.cpp | 1.不依赖LLVM DataLayout 2.从Triple字符串推断布局 3.支持x86_64/aarch64 | 1.x86_64: PointerSize=8, LittleEndian=true 2.aarch64: PointerSize=8, LittleEndian=true 3.getTypeSizeInBits(IRIntegerType(32))==32 | Create("x86_64-unknown-linux-gnu")→PointerSize=8; Create("aarch64-unknown-linux-gnu")→LittleEndian=true | include/blocktype/Basic/*.h |
| A.3 | IRValue+IRConstant+Use/User体系 | 2天 | IRType体系, blocktype-basic ADT | IRValue.h, IRInstruction.h, IRConstant.h, 对应.cpp | 1.Use-Def chain必须双向正确 2.IRConstant子类不可变(immutable) 3.IRConstantUndef::get(TypeCtx, T)必须缓存 | 1.IRConstantInt::get(TypeCtx, 32, 42)->getValue()==42 2.Use/User双向链接正确 3.IRConstantUndef::get两次返回同一指针 | IRConstantInt(32,42).getValue()==42; Use::set(V)→getUser()->getOperand(0)==V | IRType.h, IRTypeContext.h |
| A.3.1 | IRFormatVersion+IRFileHeader | 1天 | blocktype-basic ADT | IRFormatVersion.h, IRFormatVersion.cpp | 1.版本号独立于编译器版本 2.魔数"BTIR" 3.isCompatibleWith()语义正确 | 1.IRFormatVersion{1,0,0}.isCompatibleWith({1,0,0})==true 2.IRFormatVersion{2,0,0}.isCompatibleWith({1,0,0})==false 3.IRFileHeader大小固定 | Current()=={1,0,0}; {2,0,0}.isCompatibleWith({1,5,0})==false | include/blocktype/Basic/*.h |
| A.4 | IRModule/IRFunction/IRBasicBlock/IRFunctionDecl | 2天 | IRValue, IRType, blocktype-basic ADT | IRModule.h, IRFunction.h, IRBasicBlock.h, 对应.cpp | 1.IRFunction持有IRBasicBlock列表 2.IRModule持有所有函数/全局变量 3.getOrInsertFunction语义与LLVM一致 | 1.IRModule::getOrInsertFunction("foo", FTy)创建新函数 2.IRFunction::addBasicBlock("entry")成功 3.IRBasicBlock::getTerminator()正确 | getOrInsertFunction("foo")→非空; addBasicBlock("entry")→非空 | IRValue.h, IRType.h |
| A.5 | IRBuilder(含常量工厂) | 2天 | IRValue, IRInstruction, IRType | IRBuilder.h, IRBuilder.cpp | 1.所有create*方法返回非空指针 2.InsertPoint正确维护 3.常量工厂与IRTypeContext缓存一致 | 1.Builder.createAdd(LHS,RHS)返回IRInstruction* 2.Builder.createRetVoid()正确 3.Builder.createCall(Fn,Args)参数正确 | createAdd(i32_1,i32_2)→非空; createRetVoid()→Ret指令 | IRValue.h, IRInstruction.h, IRType.h |
| A.6 | IRVerifier | 1天 | IRModule, IRInstruction | IRVerifier.h, IRVerifier.cpp | 1.验证类型完整性(无OpaqueType残留) 2.验证SSA性质 3.验证终结指令(每BB恰好一个) | 1.合法IRModule通过验证 2.含OpaqueType的IRModule不通过 3.无终结指令的BB不通过 | verify(合法Module)→true; verify(含OpaqueType)→false | IRModule.h, IRType.h |
| A.7 | IRSerializer(文本+二进制格式) | 1天 | IRModule, IRFormatVersion | IRSerializer.h, IRSerializer.cpp | 1.文本格式人类可读 2.二进制格式紧凑 3.版本号写入文件头 | 1.writeText→parseText往返一致 2.writeBitcode→parseBitcode往返一致 3.IRFileHeader魔数"BTIR" | writeText(M)->parseText→等价Module; writeBitcode(M)->parseBitcode→等价Module | IRModule.h, IRFormatVersion.h |
| A.8 | CMake集成+单元测试 | 1天 | 所有A.1-A.7 | CMakeLists.txt, tests/ | 1.libblocktype-ir不链接LLVM 2.所有单元测试通过 3.lit测试通过 | 1.cmake --build成功 2.ctest全部通过 3.nm libblocktype-ir.a无LLVM符号 | cmake --build→成功; ctest→全部PASS | 所有A.1-A.7文件 |

**Phase A 总计**：~20天（原12天 + 新增2.5天 + 附加增强5.5人日）

### 附加增强任务（行业前沿补充）

> 来源：09-行业前沿补充优化方案 §3.1

| 任务ID | 任务描述 | 依赖 | 工作量估算 |
|--------|---------|------|-----------|
| A-F1 | IRInstruction/IRType 添加 DialectID 字段（默认 Core） | 无 | 0.5人日 |
| A-F2 | DialectCapability 扩展 BackendCapability | A-F1 | 0.5人日 |
| A-F3 | TelemetryCollector 基础框架 + PhaseGuard RAII | 无 | 1人日 |
| A-F4 | IRInstruction 添加 Optional DbgInfo 字段 | 无 | 0.5人日 |
| A-F5 | IRDebugMetadata 基础类型定义 | A-F4 | 1人日 |
| A-F6 | StructuredDiagnostic 基础结构定义 | 无 | 0.5人日 |
| A-F7 | CacheKey/CacheEntry 基础类型定义 | 无 | 0.5人日 |
| A-F8 | IRIntegrityChecksum 基础实现 | 无 | 1人日 |

**Phase A 附加增强工作量**：约 5.5 人日

### 文件变更清单

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRType.h` |
| 新增 | `include/blocktype/IR/IRTypeContext.h` |
| 新增 | `include/blocktype/IR/IRContext.h` |
| 新增 | `include/blocktype/IR/TargetLayout.h` |
| 新增 | `include/blocktype/IR/IRValue.h` |
| 新增 | `include/blocktype/IR/IRInstruction.h` |
| 新增 | `include/blocktype/IR/IRConstant.h` |
| 新增 | `include/blocktype/IR/IRFormatVersion.h` |
| 新增 | `include/blocktype/IR/IRModule.h` |
| 新增 | `include/blocktype/IR/IRFunction.h` |
| 新增 | `include/blocktype/IR/IRBasicBlock.h` |
| 新增 | `include/blocktype/IR/IRBuilder.h` |
| 新增 | `include/blocktype/IR/IRVerifier.h` |
| 新增 | `include/blocktype/IR/IRSerializer.h` |
| 新增 | `include/blocktype/IR/IRDebugInfo.h` |
| 新增 | `include/blocktype/IR/IRMetadata.h` |
| 新增 | `include/blocktype/IR/IRConsumer.h` |
| 新增 | `include/blocktype/IR/IRPass.h` |
| 新增 | `include/blocktype/IR/BackendCapability.h` |
| 新增 | `src/IR/` 下对应所有 .cpp 文件 |
| 新增 | `src/IR/CMakeLists.txt` |
| 修改 | `CMakeLists.txt`（添加 IR 子目录） |

---

## Phase B：前端抽象层 + C++ 前端适配

**目标**：建立 FrontendBase/FrontendRegistry，实现 CppFrontend，实现 ASTToIRConverter。

### 红线对照

| 红线 | 合规性 | 说明 |
|------|--------|------|
| R1 架构优先 | ✓ | 前端通过抽象接口与后端解耦 |
| R2 自由组合 | ✓ | 前端可独立替换 |
| R3 渐进式 | ✓ | CppFrontend与现有CodeGenModule并行存在，三阶段共存策略 |
| R4 功能不退化 | ✓ | 现有编译路径不变，新路径独立验证 |
| R5 接口抽象 | ✓ | FrontendBase抽象接口 |
| R6 IR中间层解耦 | ✓ | 前端产出IRModule，后端消费IRModule |

### Task 分解

| Task | 内容 | 预估 | 输入 | 输出 | 实现约束 | 验收标准 | 测试用例 | 依赖文件 |
|------|------|------|------|------|---------|---------|---------|---------|
| B.1 | FrontendBase+FrontendRegistry(含autoSelect) | 1天 | IRModule, IRTypeContext | FrontendBase.h, FrontendRegistry.h, 对应.cpp | 1.FrontendBase::compile()返回IRConversionResult 2.FrontendRegistry不依赖具体前端 3.autoSelect根据文件扩展名选择 | 1.FrontendRegistry::registerFrontend("cpp", CppFrontend)成功 2.FrontendRegistry::autoSelect("test.cpp")返回"cpp" 3.FrontendRegistry::create("cpp")返回非空 | registerFrontend("cpp")→成功; autoSelect("test.cpp")→"cpp" | IRModule.h, IRTypeContext.h |
| B.2 | IRMangler(独立于CodeGenModule) | 2天 | ASTContext, TargetLayout | IRMangler.h, IRMangler.cpp | 1.不依赖CodeGenModule 2.依赖ASTContext+TargetLayout 3.名称修饰结果与现有Mangler一致 | 1.IRMangler::mangleName(FunctionDecl)与现有Mangler输出一致 2.IRMangler不#include CodeGenModule.h | mangleName("void foo(int)")→"_Z3fooi" | ASTContext.h, TargetLayout.h, 现有Mangler.h(对比) |
| B.2.1 | IRConversionResult+错误占位策略 | 1.5天 | IRModule, DiagnosticsEngine | IRConversionResult.h, 对应.cpp | 1.IRConversionResult::isUsable()语义正确 2.错误占位值类型正确(IROpaqueType/IRConstantUndef) 3.错误计数准确 | 1.IRConversionResult::getInvalid(2).getNumErrors()==2 2.IRConversionResult(validModule).isUsable()==true 3.emitErrorPlaceholder(IRIntegerType(32))→IRConstantUndef | getInvalid(2).isInvalid()==true; emitErrorPlaceholder(Int32)→Undef | IRModule.h, IRType.h |
| B.3 | IRTypeMapper: QualType→IRType转换 | 2天 | ASTContext QualType体系, IRTypeContext | IRTypeMapper.h, IRTypeMapper.cpp | 1.每种QualType有精确映射规则(见03 §2.2.1) 2.缓存已映射类型 3.失败时返回IROpaqueType("error") | 1.mapType(BuiltinType::Int)→IRIntegerType(32) 2.mapType(PointerType(Int))→IRPointerType(Int) 3.mapType(RecordType)→IRStructType | mapType(Int)→IRIntegerType(32); mapType(Char_U)→IRIntegerType(8) | ASTContext.h, IRTypeContext.h, IRType.h |
| B.3.1 | DeadFunctionEliminationPass | 1天 | IRModule, IRPass框架 | DeadFunctionEliminationPass.h, 对应.cpp | 1.ExternalLinkage函数不可删除 2.被Call指令引用的函数不可删除 3.Pass::run()返回true表示有修改 | 1.含2个内部函数+1个被调用的→删除1个 2.ExternalLinkage函数保留 3.空模块→无删除 | run(2内部+1被调用)→删除1个; ExternalLinkage→保留 | IRModule.h, IRPass.h, IRInstruction.h |
| B.3.2 | ConstantFoldingPass | 1.5天 | IRModule, IRPass框架 | ConstantFoldingPass.h, 对应.cpp | 1.仅折叠所有操作数为常量的指令 2.不改变语义(有符号/无符号区分) 3.tryFold返回Optional | 1.add i32 1, 2 → 折叠为constant 3 2.非常量操作数→不折叠 3.溢出语义正确 | fold(add i32 1, 2)→constant 3; fold(add i32 %x, 1)→不折叠 | IRModule.h, IRPass.h, IRConstant.h |
| B.3.3 | TypeCanonicalizationPass | 1天 | IRModule, IRPass框架 | TypeCanonicalizationPass.h, 对应.cpp | 1.消除OpaqueType(替换为实际类型) 2.计算StructType字段偏移 3.无OpaqueType时无修改 | 1.含OpaqueType→替换后无OpaqueType 2.StructType偏移与TargetLayout一致 | run(含OpaqueType)→无OpaqueType残留 | IRModule.h, IRPass.h, TargetLayout.h |
| B.3.4 | createDefaultIRPipeline() | 0.5天 | 所有IR Pass | 修改IRPass相关 | 1.管线顺序: Verifier→DeadFuncElim→ConstantFold→TypeCanon→Verifier 2.返回unique_ptr<PassManager> | 1.createDefaultIRPipeline()→非空PassManager 2.运行管线后IRModule通过Verifier | createDefaultIRPipeline()→非空; run→通过Verifier | IRPass.h, 所有Pass头文件 |
| B.4 | ASTToIRConverter:表达式(基础) | 3天 | IRBuilder, IRTypeMapper, Clang AST | IREmitExpr.h, ASTToIRConverter.cpp(表达式部分) | 1.所有Emit*Expr返回ir::IRValue* 2.失败时返回emitErrorPlaceholder(T) 3.BinaryOp/UnaryOp/CallExpr/MemberExpr/DeclRef/Cast必须实现 | 1.EmitBinaryExpr(Add, i32 1, i32 2)→createAdd 2.EmitCallExpr(printf)→createCall 3.EmitDeclRefExpr(变量x)→lookup DeclValues | EmitBinaryExpr(1+2)→createAdd; EmitCallExpr(foo())→createCall | IRBuilder.h, IRTypeMapper.h, Clang AST headers |
| B.4.1 | 契约验证函数+FrontendIRContractTest | 2天 | IRModule, IRVerifier | contract验证函数.h/.cpp, FrontendIRContractTest.cpp | 1.6个契约验证函数全部实现 2.融入GTest框架 3.Debug构建默认启用 | 1.verifyIRModuleContract(合法Module)→true 2.verifyTypeCompleteness(含OpaqueType)→false 3.verifyTerminatorContract(无终结指令BB)→false | verifyAllContracts(合法)→true; verifyTypeCompleteness(含Opaque)→false | IRModule.h, IRVerifier.h, GTest |
| B.5 | ASTToIRConverter:表达式(C++特有) | 3天 | IREmitExpr, Clang CXX AST | IREmitExpr.cpp(C++部分) | 1.CXXConstruct/New/Delete/This/VirtualCall必须实现 2.VirtualCall→间接调用(函数指针) 3.New/Delete→调用operator new/delete | 1.EmitCXXConstructExpr→createCall(构造函数) 2.EmitCXXThisExpr→lookup This指针 3.EmitCXXMemberCallExpr→虚表查找+间接调用 | EmitCXXThisExpr→This指针; EmitCXXNewExpr→call operator new | IREmitExpr.h, Clang CXX AST headers |
| B.6 | ASTToIRConverter:语句 | 2天 | IREmitStmt, IRBuilder | IREmitStmt.h, IREmitStmt.cpp | 1.If/For/While/Return/Switch必须实现 2.控制流BB正确连接 3.Phi节点正确生成 | 1.EmitIfStmt→CondBr+两个分支BB+MergeBB 2.EmitForStmt→CondBr+LoopBB+IncBB+AfterBB 3.EmitReturnStmt→createRet | EmitIfStmt→CondBr+2个BB; EmitForStmt→4个BB | IRBuilder.h, Clang AST headers |
| B.7 | ASTToIRConverter:C++类布局 | 2天 | IREmitCXXLayout, IRTypeMapper | IREmitCXXLayout.h, IREmitCXXLayout.cpp | 1.类布局与现有CGCXX一致 2.字段偏移计算正确 3.虚基类偏移正确 | 1.ComputeClassLayout(简单类)→字段偏移与CGCXX一致 2.GetBaseOffset(单一继承)→偏移正确 3.GetVirtualBaseOffset→虚基类偏移正确 | ComputeClassLayout({int x; double y})→偏移{0,8} | IRTypeMapper.h, TargetLayout.h, Clang CXX AST |
| B.8 | ASTToIRConverter:构造/析构函数 | 3天 | IREmitCXXCtor, IREmitCXXDtor | IREmitCXXCtor.h, IREmitCXXDtor.h, 对应.cpp | 1.构造函数:基类初始化→成员初始化→构造体 2.析构函数:析构体→成员析构→基类析构(逆序) 3.VTable指针初始化 | 1.EmitConstructor→按序生成基类/成员初始化IR 2.EmitDestructor→逆序生成析构IR 3.VTable指针初始化在构造函数最前面 | EmitConstructor→基类init→成员init→body; EmitDestructor→body→成员dtor→基类dtor | IREmitCXXLayout.h, IRBuilder.h, Clang CXX AST |
| B.9 | CppFrontend实现 | 1天 | FrontendBase, ASTToIRConverter | CppFrontend.h, CppFrontend.cpp | 1.继承FrontendBase 2.compile()调用ASTToIRConverter::convert() 3.支持--frontend=cpp选项 | 1.CppFrontend::compile("int foo(){return 1;}")→IRModule 2.IRModule通过契约验证 | compile("int f(){return 1;}")→isUsable()==true | FrontendBase.h, ASTToIRConverter.h |
| B.10 | 集成测试 | 2天 | 所有Phase B组件 | tests/下新增测试文件 | 1.现有测试全部通过 2.IR输出验证正确 3.端到端编译测试 | 1.所有现有lit test通过 2.新IR路径编译结果与旧路径语义一致 3.--frontend=cpp编译简单C++文件成功 | 现有lit test→PASS; 新路径编译hello.cpp→输出正确 | 所有Phase B文件 |

**Phase B 总计**：~42天（原21天 + 新增3天B.2.1 + 新增4天B.3.1-B.3.4 + 新增2天B.4.1 - 3天原B.3/B.4编号偏移 ≈ 24天 + 附加增强18人日）

### 附加增强任务（行业前沿补充）

> 来源：09-行业前沿补充优化方案 §3.2

| 任务ID | 任务描述 | 依赖 | 工作量估算 |
|--------|---------|------|-----------|
| B-F1 | DialectLoweringPass：bt_cpp → bt_core 降级 | A-F1, Phase B IR Pass框架 | 3人日 |
| B-F2 | DialectLoweringPass：invoke→call+landingpad | B-F1 | 2人日 |
| B-F3 | DialectLoweringPass：dynamic_cast→函数调用 | B-F1 | 1人日 |
| B-F4 | DialectLoweringPass：vtable→全局常量+间接调用 | B-F1 | 2人日 |
| B-F5 | TelemetryCollector 与 CompilerInstance 集成 | A-F3 | 1人日 |
| B-F6 | StructuredDiagEmitter 基础实现（文本+JSON） | A-F6 | 2人日 |
| B-F7 | DiagnosticGroupManager 基础实现 | A-F6 | 1人日 |
| B-F8 | LocalDiskCache 基础实现 | A-F7 | 2人日 |
| B-F9 | PassInvariantChecker 基础实现（SSA/类型不变量） | Phase B IR Pass框架 | 2人日 |
| B-F10 | IR调试信息：前端→IR调试元数据传递 | A-F4, A-F5 | 2人日 |

**Phase B 附加增强工作量**：约 18 人日

### CGCXX 拆分策略（在 B.7-B.8 期间执行）

1. 先拆分 CGCXX.cpp 为 7 个子文件（物理拆分，逻辑不变）
2. 然后逐步将各子文件的逻辑迁移到 ASTToIRConverter
3. VTable/RTTI 逻辑留在 LLVM 后端（Itanium ABI 特定）

### 文件变更清单

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/FrontendBase.h` |
| 新增 | `include/blocktype/Frontend/FrontendRegistry.h` |
| 新增 | `include/blocktype/Frontend/FrontendOptions.h` |
| 新增 | `include/blocktype/Frontend/ASTToIRConverter.h` |
| 新增 | `include/blocktype/Frontend/IREmitExpr.h` |
| 新增 | `include/blocktype/Frontend/IREmitStmt.h` |
| 新增 | `include/blocktype/Frontend/IREmitCXX.h` |
| 新增 | `include/blocktype/Frontend/IREmitCXXLayout.h` |
| 新增 | `include/blocktype/Frontend/IREmitCXXCtor.h` |
| 新增 | `include/blocktype/Frontend/IREmitCXXDtor.h` |
| 新增 | `include/blocktype/Frontend/IREmitCXXVTable.h` |
| 新增 | `include/blocktype/Frontend/IREmitCXXRTTI.h` |
| 新增 | `include/blocktype/Frontend/IREmitCXXInherit.h` |
| 新增 | `include/blocktype/Frontend/IREmitCXXThunk.h` |
| 新增 | `include/blocktype/Frontend/IRConstantEmitter.h` |
| 新增 | `include/blocktype/Frontend/IRTypeMapper.h` |
| 新增 | `include/blocktype/Frontend/IRDebugInfo.h` |
| 新增 | `src/Frontend/` 下对应所有 .cpp 文件 |
| 修改 | `include/blocktype/CodeGen/Mangler.h`（去掉 CodeGenModule 依赖） |
| 修改 | `src/CodeGen/Mangler.cpp`（改为依赖 ASTContext+TargetLayout） |
| 移动 | `include/blocktype/CodeGen/TargetInfo.h` → `include/blocktype/Target/TargetInfo.h` |
| 移动 | `include/blocktype/CodeGen/ABIInfo.h` → `include/blocktype/Backend/ABIInfo.h` |
