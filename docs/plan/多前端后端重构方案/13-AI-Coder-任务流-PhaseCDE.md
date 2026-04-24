# AI Coder 可执行任务流 — Phase CDE：后端适配 + 管线重构 + 验证迁移

> 本文档是 AI coder 可直接执行的改造任务流。每个 Task 自包含所有必要信息。

---

## 执行规则

1. **严格按 Task 编号顺序执行**，每个 Task 完成并通过验收后再开始下一个
2. **接口签名不可修改**——本文档中的 class/struct/enum/函数签名是硬约束
3. **验收标准必须全部通过**
4. **命名空间**：后端层代码在 `namespace blocktype::backend` 中
5. **头文件路径**：`include/blocktype/Backend/`，源文件路径：`src/Backend/`
6. **依赖限制**：`libblocktype-backend-llvm` 依赖 `blocktype-ir` + `LLVM_ADT_LIBS` + `LLVM_IR_LIBS`
7. **Git 提交与推送**：每个 Task 完成并通过验收后，**必须立即**执行以下操作：
   ```bash
   git add -A
   git commit -m "feat(<phase>): 完成 <Task编号> — <Task标题>"
   git push origin HEAD
   ```
   - commit message 格式：`feat(C): 完成 C.1 — BackendBase 抽象基类 + BackendOptions`
   - **不得跳过此步骤**——确保每个 Task 的产出都有远端备份，防止工作丢失
   - 如果 push 失败，先 `git pull --rebase origin HEAD` 再重试

---

## 接口关联关系

### 持有关系

```
BackendBase ──ref──→ BackendOptions（配置，值语义）
BackendBase ──ref──→ DiagnosticsEngine&（诊断，引用不拥有）

BackendRegistry ──owns──→ StringMap<BackendFactory>（工厂函数映射）
BackendRegistry ──owns──→ StringMap<string> NameToTriple（后端名→默认Triple映射）

LLVMBackend ──owns──→ unique_ptr<llvm::LLVMContext> LLVMCtx
LLVMBackend ──owns──→ unique_ptr<IRToLLVMConverter> Converter
LLVMBackend ──owns──→ unique_ptr<llvm::TargetMachine> TM
LLVMBackend ──ref──→ BackendOptions（继承自BackendBase）
LLVMBackend ──ref──→ DiagnosticsEngine&（继承自BackendBase）

IRToLLVMConverter ──ref──→ ir::IRTypeContext& IRCtx
IRToLLVMConverter ──ref──→ llvm::LLVMContext& LLVMCtx
IRToLLVMConverter ──ref──→ const BackendOptions& Opts
IRToLLVMConverter ──owns──→ unique_ptr<llvm::Module> TheModule
IRToLLVMConverter ──owns──→ DenseMap<const ir::IRType*, llvm::Type*> TypeMap
IRToLLVMConverter ──owns──→ DenseMap<const ir::IRValue*, llvm::Value*> ValueMap
IRToLLVMConverter ──owns──→ DenseMap<const ir::IRFunction*, llvm::Function*> FunctionMap
IRToLLVMConverter ──owns──→ DenseMap<const ir::IRBasicBlock*, llvm::BasicBlock*> BlockMap

BackendCapability ──val──→ uint32_t FeatureBits（位掩码）

CompilerInstance ──owns──→ unique_ptr<CompilerInvocation> Invocation
CompilerInstance ──owns──→ unique_ptr<DiagnosticsEngine> Diags
CompilerInstance ──owns──→ unique_ptr<FrontendBase> Frontend
CompilerInstance ──owns──→ unique_ptr<BackendBase> Backend
CompilerInstance ──owns──→ unique_ptr<ir::IRContext> IRCtx
CompilerInstance ──owns──→ unique_ptr<ir::IRModule> IRModule
CompilerInstance ──owns──→ unique_ptr<ir::IRTypeContext> IRTypeCtx
CompilerInstance ──owns──→ unique_ptr<ir::TargetLayout> Layout
```

### 调用关系

```
BackendBase::emitObject(IRModule, OutputPath)
  ──calls──→ getCapability() 检查后端能力
  ──calls──→ IRModule::getRequiredFeatures() 检查所需特性
  ──calls──→ Capability.getUnsupported(Required) 检查不支持的特性
  ──calls──→ 降级或中止（按降级策略表）

LLVMBackend::emitObject(IRModule, OutputPath)
  ──calls──→ IRToLLVMConverter::convert(IRModule)
  ──calls──→ llvm::PassBuilder::buildPerModuleDefaultPipeline() 优化
  ──calls──→ llvm::TargetMachine::addPassesToEmitFile() 代码生成

IRToLLVMConverter::convert(IRModule)
  ──calls──→ mapType() 对 IRModule 中每个 IRType
  ──calls──→ mapFunction() 对 IRModule 中每个 IRFunction
  ──calls──→ convertGlobalVariable() 对 IRModule 中每个 IRGlobalVariable
  ──calls──→ convertConstant() 对每个 IRConstant
  ──returns──→ unique_ptr<llvm::Module>

IRToLLVMConverter::mapType(IRType*)
  ──calls──→ TypeMap.lookup(T) 先查缓存
  ──calls──→ llvm::Type::getInt32Ty() 等LLVM类型工厂（按映射规则表）
  ──calls──→ mapType(ElementType) 递归映射子类型
  ──注意──→ IRStructType 使用 llvm::StructType::create() 先占位再设body

IRToLLVMConverter::mapFunction(IRFunction*)
  ──calls──→ FunctionMap.lookup(F) 先查缓存
  ──calls──→ llvm::Function::create(mapType(F->getFunctionType()))
  ──calls──→ mapBasicBlock() 对每个 IRBasicBlock

IRToLLVMConverter::convertFunction(IRFunction&, llvm::Function*)
  ──calls──→ convertInstruction() 对每条 IRInstruction
  ──calls──→ mapValue() 对每个 IRValue 操作数

IRToLLVMConverter::convertInstruction(IRInstruction&, IRBuilder&)
  ──calls──→ switch(Opcode) 按映射规则表分派
  ──calls──→ mapValue() 映射操作数
  ──calls──→ llvm::IRBuilder::Create*() 生成LLVM IR

CompilerInstance::compileFile(Filename)
  ──calls──→ FrontendRegistry::autoSelect() 或 create() 创建前端
  ──calls──→ BackendRegistry::autoSelect() 或 create() 创建后端
  ──calls──→ Frontend->compile() → IRModule
  ──calls──→ Backend->emitObject(IRModule, OutputPath)
  ──注意──→ 新路径：Frontend→IRModule→Backend；旧路径保留为fallback

CompilerInvocation::getFrontendName()
  ──returns──→ FrontendName 字段（默认 "cpp"）
CompilerInvocation::getBackendName()
  ──returns──→ BackendName 字段（默认 "llvm"）
```

### 生命周期约束

```
CompilerInstance ≥ FrontendBase / BackendBase ≥ IRToLLVMConverter
LLVMBackend ≥ llvm::LLVMContext（LLVMCtx 必须在 TheModule 之前销毁）
IRToLLVMConverter 的 TypeMap/ValueMap/FunctionMap 生命周期 = convert() 调用期间
BackendRegistry 全局单例
```

### 内存管理规则

| 对象 | 分配方式 | 拥有者 | 释放方式 |
|------|---------|--------|---------|
| BackendBase 子类 | unique_ptr | CompilerInstance | unique_ptr 析构 |
| llvm::LLVMContext | unique_ptr | LLVMBackend | unique_ptr 析构 |
| llvm::Module | unique_ptr | IRToLLVMConverter | convert() 返回后由 LLVMBackend 管理 |
| IRToLLVMConverter | unique_ptr | LLVMBackend | unique_ptr 析构 |
| TypeMap/ValueMap 等 | IRToLLVMConverter 直接持有 | IRToLLVMConverter | 随 IRToLLVMConverter 析构释放 |

---

## Task C.1：BackendBase + BackendRegistry + BackendCapability

### 依赖

- Phase A（IRModule/IRType）
- Phase B（FrontendBase/FrontendRegistry，对称设计）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/BackendBase.h` |
| 新增 | `include/blocktype/Backend/BackendOptions.h` |
| 新增 | `include/blocktype/Backend/BackendCapability.h` |
| 新增 | `include/blocktype/Backend/BackendRegistry.h` |
| 新增 | `src/Backend/BackendBase.cpp` |
| 新增 | `src/Backend/BackendRegistry.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::backend {

struct BackendOptions {
  std::string TargetTriple;
  std::string OutputPath;
  std::string OutputFormat = "elf";
  unsigned OptimizationLevel = 0;
  bool EmitAssembly = false;
  bool EmitIR = false;
  bool EmitIRBitcode = false;
  bool DebugInfo = false;
  bool DebugInfoForProfiling = false;
  std::string DebugInfoFormat = "dwarf5";
};

enum class IRFeature : uint32_t {
  IntegerArithmetic = 1 << 0,
  FloatArithmetic   = 1 << 1,
  VectorOperations  = 1 << 2,
  AtomicOperations  = 1 << 3,
  ExceptionHandling = 1 << 4,
  Coroutines        = 1 << 5,
  DebugInfo         = 1 << 6,
  VarArg            = 1 << 7,
  SeparateFloatInt  = 1 << 8,
  StructReturn      = 1 << 9,
  DynamicCast       = 1 << 10,
  VirtualDispatch   = 1 << 11,
  FFICall           = 1 << 12,
};

class BackendCapability {
  uint32_t FeatureBits = 0;
public:
  void declareFeature(IRFeature F) { FeatureBits |= static_cast<uint32_t>(F); }
  bool hasFeature(IRFeature F) const { return (FeatureBits & static_cast<uint32_t>(F)) != 0; }
  uint32_t getUnsupported(uint32_t Required) const { return Required & ~FeatureBits; }
  uint32_t getFeatureBits() const { return FeatureBits; }
};

class BackendBase {
protected:
  BackendOptions Opts;
  DiagnosticsEngine& Diags;
public:
  BackendBase(const BackendOptions& Opts, DiagnosticsEngine& Diags);
  virtual ~BackendBase() = default;
  virtual StringRef getName() const = 0;
  virtual bool emitObject(ir::IRModule& IRModule, StringRef OutputPath) = 0;
  virtual bool emitAssembly(ir::IRModule& IRModule, StringRef OutputPath) = 0;
  virtual bool emitIRText(ir::IRModule& IRModule, raw_ostream& OS) = 0;
  virtual BackendCapability getCapability() const = 0;
  const BackendOptions& getOptions() const { return Opts; }
  DiagnosticsEngine& getDiagnostics() const { return Diags; }
};

using BackendFactory = std::function<unique_ptr<BackendBase>(
  const BackendOptions&, DiagnosticsEngine&)>;

class BackendRegistry {
  llvm::StringMap<BackendFactory> Registry;
  llvm::StringMap<std::string> NameToTriple;
  BackendRegistry() = default;
public:
  static BackendRegistry& instance();
  void registerBackend(StringRef Name, BackendFactory Factory);
  unique_ptr<BackendBase> create(StringRef Name, const BackendOptions& Opts, DiagnosticsEngine& Diags);
  unique_ptr<BackendBase> autoSelect(const ir::IRModule& M, const BackendOptions& Opts, DiagnosticsEngine& Diags);
  bool hasBackend(StringRef Name) const;
  SmallVector<StringRef, 4> getRegisteredNames() const;
};

}
```

### 后端能力降级策略表

| 不支持的特性 | 降级策略 | 是否可降级 |
|-------------|---------|-----------|
| ExceptionHandling | invoke→call（无异常表），发出警告 | ✅ 可降级 |
| DebugInfo | 无调试信息，发出警告 | ✅ 可降级 |
| VectorOperations | 标量循环，发出警告 | ✅ 可降级 |
| VarArg | 不生成变参函数，发出警告 | ✅ 可降级 |
| DynamicCast | **不可降级**，中止编译 | ❌ |
| VirtualDispatch | **不可降级**，中止编译 | ❌ |
| Coroutines | **不可降级**，中止编译 | ❌ |

### 实现约束

1. BackendBase 不可拷贝
2. emitObject 失败返回 false
3. BackendRegistry 全局单例
4. autoSelect 根据 IRModule 的 TargetTriple 选择后端

### 验收标准

```cpp
// V1: BackendCapability 特性声明
BackendCapability Cap;
Cap.declareFeature(IRFeature::IntegerArithmetic);
Cap.declareFeature(IRFeature::FloatArithmetic);
assert(Cap.hasFeature(IRFeature::IntegerArithmetic) == true);
assert(Cap.hasFeature(IRFeature::Coroutines) == false);

// V2: 不支持特性检测
uint32_t Required = static_cast<uint32_t>(IRFeature::IntegerArithmetic)
                  | static_cast<uint32_t>(IRFeature::Coroutines);
uint32_t Unsupported = Cap.getUnsupported(Required);
assert((Unsupported & static_cast<uint32_t>(IRFeature::Coroutines)) != 0);

// V3: BackendRegistry 注册和创建
auto& Reg = BackendRegistry::instance();
Reg.registerBackend("llvm", createLLVMBackend);
auto BE = Reg.create("llvm", Opts, Diags);
assert(BE != nullptr);
assert(BE->getName() == "llvm");
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.1：BackendBase + BackendRegistry + BackendCapability" && git push origin HEAD
> ```

---

## Task C.2：IRToLLVMConverter 类型转换

### 依赖

- C.1（BackendBase）
- Phase A（IRType 体系）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/IRToLLVMConverter.h` |
| 新增 | `src/Backend/IRToLLVMConverter.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::backend {

class IRToLLVMConverter {
  ir::IRTypeContext& IRCtx;
  llvm::LLVMContext& LLVMCtx;
  const BackendOptions& Opts;
  unique_ptr<llvm::Module> TheModule;
  DenseMap<const ir::IRType*, llvm::Type*> TypeMap;
  DenseMap<const ir::IRValue*, llvm::Value*> ValueMap;
  DenseMap<const ir::IRFunction*, llvm::Function*> FunctionMap;
  DenseMap<const ir::IRBasicBlock*, llvm::BasicBlock*> BlockMap;
public:
  IRToLLVMConverter(ir::IRTypeContext& IRCtx,
                    llvm::LLVMContext& LLVMCtx,
                    const BackendOptions& Opts = BackendOptions());
  unique_ptr<llvm::Module> convert(ir::IRModule& IRModule);
private:
  llvm::Type* mapType(const ir::IRType* T);
  llvm::Value* mapValue(const ir::IRValue* V);
  llvm::Function* mapFunction(const ir::IRFunction* F);
  llvm::BasicBlock* mapBasicBlock(const ir::IRBasicBlock* BB);
  void convertFunction(ir::IRFunction& IRF, llvm::Function* LLVMF);
  void convertInstruction(ir::IRInstruction& I, llvm::IRBuilder<>& Builder);
  void convertGlobalVariable(ir::IRGlobalVariable& IRGV);
  llvm::Constant* convertConstant(const ir::IRConstant* C);
};

}
```

### IRType → llvm::Type 映射规则表

| IR Type | LLVM Type | 边界情况 |
|---------|-----------|---------|
| IRVoidType | llvm::Type::getVoidTy() | — |
| IRIntegerType(1) | llvm::Type::getInt1Ty() | 布尔特殊处理 |
| IRIntegerType(N) | llvm::Type::getIntNTy(N) | N 必须为 2 的幂 |
| IRFloatType(16) | llvm::Type::getHalfTy() | 半精度 |
| IRFloatType(32) | llvm::Type::getFloatTy() | 单精度 |
| IRFloatType(64) | llvm::Type::getDoubleTy() | 双精度 |
| IRFloatType(80) | llvm::Type::getX86_FP80Ty() | 仅 x86 支持 |
| IRFloatType(128) | llvm::Type::getFP128Ty() | 四精度 |
| IRFloatType(其他) | 报错 | 不支持的位宽 |
| IRPointerType(T, 0) | llvm::PointerType::get(mapType(T), 0) | 默认地址空间 |
| IRPointerType(T, N) | llvm::PointerType::get(mapType(T), N) | 非默认地址空间 |
| IRArrayType(T, 0) | llvm::ArrayType::get(mapType(T), 0) | 零长度数组 |
| IRArrayType(T, N) | llvm::ArrayType::get(mapType(T), N) | — |
| IRStructType(Name, Elems, false) | llvm::StructType::create(mapTypes(Elems), Name) | 非 packed |
| IRStructType(Name, Elems, true) | llvm::StructType::create(mapTypes(Elems), Name, true) | packed |
| IRStructType("", Elems, P) | llvm::StructType::get(mapTypes(Elems), P) | 匿名结构体 |
| IRFunctionType(Ret, Params, false) | llvm::FunctionType::get(mapType(Ret), mapTypes(Params)) | 非变参 |
| IRFunctionType(Ret, Params, true) | llvm::FunctionType::get(mapType(Ret), mapTypes(Params), true) | 变参 |
| IRVectorType(N, T) | llvm::VectorType::get(mapType(T), N, false) | 固定长度向量 |
| IROpaqueType(Name) | llvm::StructType::create(LLVMCtx, Name) | 不透明→命名结构体占位 |

### 递归类型处理算法

```
mapType(IRStructType "Node", [IRPointerType(Node), IRIntegerType(32)]):
  1. TypeMap["Node"] = llvm::StructType::create(LLVMCtx, "Node")  // 先占位
  2. mapType(IRPointerType(Node)) → llvm::PointerType::get(占位结构体)
  3. mapType(IRIntegerType(32)) → llvm::Type::getInt32Ty()
  4. 占位结构体->setBody([PointerType, Int32Ty])  // 后设 body
```

### 实现约束

1. mapType 必须缓存（相同 IRType* 返回相同 llvm::Type*）
2. 递归类型必须先占位再设 body
3. convert 失败时发出 diag::err_ir_to_llvm_conversion_failed

### 验收标准

```cpp
// V1: 整数类型映射
auto* LLVMInt32 = Converter.mapType(IRInt32Ty);
assert(LLVMInt32 == llvm::Type::getInt32Ty(LLVMCtx));

// V2: 指针类型映射
auto* LLVMPtr = Converter.mapType(IRPtrInt8Ty);
assert(isa<llvm::PointerType>(LLVMPtr));

// V3: 结构体映射（含递归）
auto* LLVMStruct = Converter.mapType(IRNodeStructTy);
assert(isa<llvm::StructType>(LLVMStruct));
assert(cast<llvm::StructType>(LLVMStruct)->isOpaque() == false);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.2：IRToLLVMConverter 类型转换" && git push origin HEAD
> ```

---

## Task C.3：IRToLLVMConverter 值/指令转换

### 依赖

- C.2（类型转换）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/IRToLLVMConverter.cpp` |

### convertInstruction 完整映射表

| IR Opcode | LLVM IR 映射 | 边界情况 |
|-----------|-------------|---------|
| Opcode::Ret | Builder.CreateRet(mapValue(Val)) | RetVoid: CreateRetVoid() |
| Opcode::Br | Builder.CreateBr(mapBasicBlock(Dest)) | — |
| Opcode::CondBr | Builder.CreateCondBr(mapValue(Cond), TrueBB, FalseBB) | — |
| Opcode::Switch | Builder.CreateSwitch(mapValue(Val), Default, NumCases) | — |
| Opcode::Invoke | Builder.CreateInvoke(Callee, NormalBB, UnwindBB, Args) | — |
| Opcode::Unreachable | Builder.CreateUnreachable() | — |
| Opcode::Add | Builder.CreateAdd(LHS, RHS) | 需检查 nuw/nsw 标志 |
| Opcode::Sub | Builder.CreateSub(LHS, RHS) | 同上 |
| Opcode::Mul | Builder.CreateMul(LHS, RHS) | 同上 |
| Opcode::UDiv | Builder.CreateUDiv(LHS, RHS) | 需检查 exact 标志 |
| Opcode::SDiv | Builder.CreateSDiv(LHS, RHS) | 同上 |
| Opcode::FAdd | Builder.CreateFAdd(LHS, RHS) | 需检查 fast-math 标志 |
| Opcode::FSub | Builder.CreateFSub(LHS, RHS) | 同上 |
| Opcode::FMul | Builder.CreateFMul(LHS, RHS) | 同上 |
| Opcode::FDiv | Builder.CreateFDiv(LHS, RHS) | 同上 |
| Opcode::Shl | Builder.CreateShl(LHS, RHS) | — |
| Opcode::LShr | Builder.CreateLShr(LHS, RHS) | — |
| Opcode::AShr | Builder.CreateAShr(LHS, RHS) | — |
| Opcode::And | Builder.CreateAnd(LHS, RHS) | — |
| Opcode::Or | Builder.CreateOr(LHS, RHS) | — |
| Opcode::Xor | Builder.CreateXor(LHS, RHS) | — |
| Opcode::Alloca | Builder.CreateAlloca(mapType(ElemTy), Align) | 需获取对齐 |
| Opcode::Load | Builder.CreateLoad(mapType(ElemTy), mapValue(Ptr), Align) | volatile 标志 |
| Opcode::Store | Builder.CreateStore(mapValue(Val), mapValue(Ptr), Align) | volatile 标志 |
| Opcode::GEP | Builder.CreateGEP(mapType(SourceTy), mapValue(Ptr), Indices) | 需获取 source type |
| Opcode::Trunc | Builder.CreateTrunc(mapValue(V), DstTy) | — |
| Opcode::ZExt | Builder.CreateZExt(mapValue(V), DstTy) | — |
| Opcode::SExt | Builder.CreateSExt(mapValue(V), DstTy) | — |
| Opcode::FPTrunc | Builder.CreateFPTrunc(mapValue(V), DstTy) | — |
| Opcode::FPExt | Builder.CreateFPExt(mapValue(V), DstTy) | — |
| Opcode::FPToSI | Builder.CreateFPToSI(mapValue(V), DstTy) | — |
| Opcode::SIToFP | Builder.CreateSIToFP(mapValue(V), DstTy) | — |
| Opcode::PtrToInt | Builder.CreatePtrToInt(mapValue(V), DstTy) | — |
| Opcode::IntToPtr | Builder.CreateIntToPtr(mapValue(V), DstTy) | — |
| Opcode::BitCast | Builder.CreateBitCast(mapValue(V), DstTy) | — |
| Opcode::ICmp | Builder.CreateICmp(Predicate, LHS, RHS) | 需获取谓词 |
| Opcode::FCmp | Builder.CreateFCmp(Predicate, LHS, RHS) | 需获取谓词 |
| Opcode::Call | Builder.CreateCall(mapFunction(Callee), Args) | 需获取 calling convention |
| Opcode::Phi | Builder.CreatePHI(mapType(Ty), NumIncoming) | 需后续 addIncoming() |
| Opcode::Select | Builder.CreateSelect(Cond, TrueVal, FalseVal) | — |
| Opcode::ExtractValue | Builder.CreateExtractValue(Agg, Indices) | — |
| Opcode::InsertValue | Builder.CreateInsertValue(Agg, Val, Indices) | — |
| Opcode::DbgDeclare | Builder.CreateDbgDeclare(Variable, Expr) | 调试信息映射 |
| Opcode::DbgValue | Builder.CreateDbgValue(Variable, Expr) | 调试信息映射 |

### 实现约束

1. 所有 mapValue 必须缓存
2. Phi 节点需两遍处理：第一遍创建占位，第二遍 addIncoming()
3. 未实现的 Opcode 发出 diag::warn_ir_opcode_not_supported 并跳过

### 验收标准

```cpp
// V1: add 指令转换
// IR: %sum = add i32 %a, %b
// LLVM: %sum = add i32 %a, %b
// mapValue 返回正确的 llvm::Value*

// V2: 函数调用转换
// IR: call @foo(i32 1, i32 2)
// LLVM: call void @foo(i32 1, i32 2)

// V3: 条件分支转换
// IR: condBr %cond, %then, %else
// LLVM: br i1 %cond, label %then, label %else
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.3：IRToLLVMConverter 值/指令转换" && git push origin HEAD
> ```

---

## Task C.4：IRToLLVMConverter 函数/模块/全局变量转换

### 依赖

- C.3（值/指令转换）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/IRToLLVMConverter.cpp` |

### 实现约束

1. convertGlobalVariable 对 IROpaqueType 全局变量创建 llvm::StructType 占位
2. convertConstant 递归处理 IRConstantStruct/IRConstantArray
3. IRConstantFunctionRef → llvm::Function 对应查找
4. IRConstantGlobalRef → llvm::GlobalVariable 对应查找

### 验收标准

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
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.4：IRToLLVMConverter 函数/模块/全局变量转换" && git push origin HEAD
> ```

---

## Task C.5：LLVMBackend 优化 pipeline

### 依赖

- C.4（IRToLLVMConverter 完整功能）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/LLVMBackend.h` |
| 新增 | `src/Backend/LLVMBackend.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::backend {

class LLVMBackend : public BackendBase {
  unique_ptr<llvm::LLVMContext> LLVMCtx;
  unique_ptr<IRToLLVMConverter> Converter;
public:
  LLVMBackend(const BackendOptions& Opts, DiagnosticsEngine& Diags);
  StringRef getName() const override { return "llvm"; }
  bool emitObject(ir::IRModule& IRModule, StringRef OutputPath) override;
  bool emitAssembly(ir::IRModule& IRModule, StringRef OutputPath) override;
  bool emitIRText(ir::IRModule& IRModule, raw_ostream& OS) override;
  BackendCapability getCapability() const override;
private:
  unique_ptr<llvm::Module> convertToLLVM(ir::IRModule& IRModule);
  bool optimize(llvm::Module& M, unsigned OptLevel);
  bool emitCode(llvm::Module& M, StringRef OutputPath, llvm::CodeGenFileType FileType);
  bool checkCapability(ir::IRModule& IRModule);
};

}
```

### getCapability 精确实现

```cpp
BackendCapability LLVMBackend::getCapability() const {
  BackendCapability Cap;
  Cap.declareFeature(IRFeature::IntegerArithmetic);
  Cap.declareFeature(IRFeature::FloatArithmetic);
  Cap.declareFeature(IRFeature::VectorOperations);
  Cap.declareFeature(IRFeature::AtomicOperations);
  Cap.declareFeature(IRFeature::ExceptionHandling);
  Cap.declareFeature(IRFeature::DebugInfo);
  Cap.declareFeature(IRFeature::VarArg);
  Cap.declareFeature(IRFeature::SeparateFloatInt);
  Cap.declareFeature(IRFeature::StructReturn);
  Cap.declareFeature(IRFeature::DynamicCast);
  Cap.declareFeature(IRFeature::VirtualDispatch);
  return Cap;
}
```

### 实现约束

1. LLVMCtx 在 LLVMBackend 构造时创建，析构时销毁
2. optimize 使用 llvm::PassBuilder
3. emitCode 使用 llvm::TargetMachine::addPassesToEmitFile()
4. checkCapability 在 emitObject 开头调用

### 验收标准

```bash
# V1: LLVMBackend 编译 IRModule 为目标文件
# blocktype --backend=llvm test.btir -o test.o
# 退出码 == 0

# V2: 优化级别生效
# blocktype --backend=llvm -O2 test.btir -o test.o
# 目标文件大小 < -O0 生成的大小

# V3: 能力检查
# 含 Coroutines 特性的 IRModule → 编译失败，报错 "unsupported feature: Coroutines"
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.5：LLVMBackend 优化 pipeline" && git push origin HEAD
> ```

---

## Task C.6：LLVMBackend 目标代码生成

### 依赖

- C.5（LLVMBackend 优化 pipeline）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/LLVMBackend.cpp` |

### 实现约束

1. 支持 ELF/Mach-O/COFF 输出格式
2. 输出格式由 BackendOptions.OutputFormat 决定
3. emitAssembly 输出汇编文本
4. emitIRText 输出 LLVM IR 文本

### 验收标准

```bash
# V1: ELF 输出
# blocktype --backend=llvm --output-format=elf test.btir -o test.o
# file test.o → "ELF 64-bit LSB relocatable"

# V2: 汇编输出
# blocktype --backend=llvm --emit-asm test.btir -o test.s
# grep "addl" test.s  # 包含 x86 汇编

# V3: LLVM IR 输出
# blocktype --backend=llvm --emit-llvm test.btir -o test.ll
# grep "define" test.ll  # 包含 LLVM IR 函数定义
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.6：LLVMBackend 目标代码生成" && git push origin HEAD
> ```

---

## Task C.7：LLVMBackend 调试信息转换

### 依赖

- C.5（LLVMBackend）
- A-F5（IRDebugMetadata 基础类型）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/IRToLLVMConverter.cpp` |

### IR 调试元数据 → LLVM 调试元数据映射表

| IR 调试信息 | LLVM 调试信息 |
|-------------|--------------|
| ir::debug::DICompileUnit | llvm::DICompileUnit |
| ir::debug::DIType | llvm::DIType |
| ir::debug::DISubprogram | llvm::DISubprogram |
| ir::debug::DILocation | llvm::DILocation |

### 实现约束

1. 使用 llvm::DIBuilder 构建调试信息
2. DWARF5 为默认格式
3. 无调试信息的 IRModule 不生成调试段

### 验收标准

```bash
# V1: 含调试信息的目标文件
# blocktype -g test.cpp -o test
# dwarfdump test.o | grep "DW_TAG_compile_unit"
# 输出非空

# V2: 无调试信息的目标文件
# blocktype test.cpp -o test
# dwarfdump test.o | grep "DW_TAG_compile_unit"
# 输出为空
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.7：LLVMBackend 调试信息转换" && git push origin HEAD
> ```

---

## Task C.8：LLVMBackend VTable/RTTI 生成

### 依赖

- C.5（LLVMBackend）
- B.7（IREmitCXX，VTable/RTTI 占位全局变量）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Backend/IRToLLVMConverter.cpp` |

### VTable/RTTI 生成流程

```
1. 识别 IRModule 中的 IROpaqueType 全局变量（VTable/RTTI 占位符）
2. 根据 Itanium ABI 填充 VTable 布局：
   a. RTTI 指针（offset-to-top + typeinfo + 虚函数指针列表）
   b. 虚函数指针按声明顺序排列
   c. 多重继承的 VTT（Virtual Table Table）
3. 虚函数调用：IR 中的间接调用 → LLVM 中的虚表查找 + 间接调用
4. dynamic_cast：IR 中的 __dynamic_cast 调用 → LLVM 中的 RTTI 查找
```

### 实现约束

1. 仅支持 Itanium ABI（不实现 MSVC ABI）
2. VTable 布局与 GCC/Clang 兼容
3. RTTI type_info 结构与 libstdc++/libc++ 兼容

### 验收标准

```cpp
// V1: VTable 生成
// 含虚函数的类 → VTable 全局变量不再为 opaque
// VTable 包含正确的虚函数指针列表

// V2: RTTI 生成
// 含 RTTI 的类 → type_info 全局变量不再为 opaque

// V3: 虚函数调用
// IR: call @__vtbl_Node[2](%obj) → LLVM: 间接调用虚表第3项
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.8：LLVMBackend VTable/RTTI 生成" && git push origin HEAD
> ```

---

## Task C.9：集成测试

### 依赖

- C.1 ~ C.8 全部完成

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `tests/integration/backend/` 下所有测试文件 |

### 验收标准

```bash
# V1: 端到端编译
# blocktype test.cpp -o test
# ./test
# 退出码 == 0

# V2: 差分测试（新路径 vs 旧路径）
# diff <(blocktype-old test.cpp -o /dev/null && objdump -d test_old.o) \
#       <(blocktype-new test.cpp -o /dev/null && objdump -d test_new.o)
# 输出为空

# V3: 多平台支持
# blocktype --target=x86_64-unknown-linux-gnu test.cpp -o test_x86
# blocktype --target=aarch64-unknown-linux-gnu test.cpp -o test_arm
# 两个目标文件均可生成
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task C.9：集成测试" && git push origin HEAD
> ```

---

## Task D.1：CompilerInvocation 新增 FrontendName/BackendName

### 依赖

- C.1（BackendBase/BackendRegistry）
- B.1/B.2（FrontendBase/FrontendRegistry）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `include/blocktype/Frontend/CompilerInvocation.h` |
| 修改 | `src/Frontend/CompilerInvocation.cpp` |

### 必须新增的字段

```cpp
class CompilerInvocation {
  std::string FrontendName = "cpp";
  std::string BackendName = "llvm";
public:
  StringRef getFrontendName() const { return FrontendName; }
  StringRef getBackendName() const { return BackendName; }
  void setFrontendName(StringRef N) { FrontendName = N.str(); }
  void setBackendName(StringRef N) { BackendName = N.str(); }
};
```

### 命令行选项

```
--frontend=<name>    指定前端名称（默认 "cpp"）
--backend=<name>     指定后端名称（默认 "llvm"）
```

### 验收标准

```bash
# V1: 默认值
# blocktype test.cpp -o test
# 使用 cpp 前端 + llvm 后端

# V2: 指定前端/后端
# blocktype --frontend=cpp --backend=llvm test.cpp -o test
# 退出码 == 0
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D.1：CompilerInvocation 新增 FrontendName/BackendName" && git push origin HEAD
> ```

---

## Task D.2：CompilerInstance 重构为管线编排器

### 依赖

- D.1（CompilerInvocation）
- C.5（LLVMBackend）
- B.10（CppFrontend）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `include/blocktype/Frontend/CompilerInstance.h` |
| 修改 | `src/Frontend/CompilerInstance.cpp` |

### 重构后的 CompilerInstance

```cpp
class CompilerInstance {
  shared_ptr<CompilerInvocation> Invocation;
  unique_ptr<DiagnosticsEngine> Diags;
  unique_ptr<FrontendBase> Frontend;
  unique_ptr<BackendBase> Backend;
  unique_ptr<ir::IRContext> IRCtx;
  unique_ptr<ir::IRModule> IRModule;
  unique_ptr<ir::IRTypeContext> IRTypeCtx;
  unique_ptr<ir::TargetLayout> Layout;
public:
  bool compileFile(StringRef Filename);
private:
  bool runFrontend(StringRef Filename);
  bool runIRPipeline();
  bool runBackend(StringRef OutputPath);
  bool runLinker(const vector<string>& ObjFiles, StringRef OutputPath);
};
```

### compileFile 精确流程

```
1. Invocation->getFrontendName() → FrontendRegistry::create()
2. Invocation->getBackendName() → BackendRegistry::create()
3. runFrontend(Filename) → Frontend->compile() → IRModule
4. runIRPipeline() → VerifierPass + PassManager（如果启用）
5. runBackend(OutputPath) → Backend->emitObject(IRModule, OutputPath)
6. runLinker() → 调用系统链接器（如果需要）
```

### OPT-03：LLVM 依赖分类处理

| 依赖类别 | 当前位置 | 重构后位置 | 处理方式 |
|---------|---------|-----------|---------|
| 管线编排 | CompilerInstance | CompilerInstance | 保留，通过 BackendBase 接口调用 |
| LLVM 上下文管理 | CompilerInstance | LLVMBackend | llvm::LLVMContext 移入后端 |
| LLVM 优化 | CompilerInstance | LLVMBackend | runOptimizationPasses() 移入 LLVMBackend::optimize() |
| 代码生成 | CompilerInstance | LLVMBackend | generateObjectFile() 移入 LLVMBackend::emitObject() |

### 实现约束

1. CompilerInstance.h 不再包含任何 `llvm/IR/*.h` 头文件
2. CompilerInstance.h 中 `llvm::` 引用数 = 0
3. 编译管线通过 FrontendBase/BackendBase 接口调用
4. IRModule 是前后端唯一交互媒介

### 验收标准

```bash
# V1: CompilerInstance.h 无 LLVM IR 头文件
# grep -c 'llvm/IR/' include/blocktype/Frontend/CompilerInstance.h
# 输出 == 0

# V2: 新管线完整编译
# blocktype --frontend=cpp --backend=llvm test.cpp -o test
# ./test
# 退出码 == 0

# V3: IRModule 是唯一交互媒介
# Frontend->compile() 返回 IRModule*
# Backend->emitObject() 接受 IRModule&
# 无其他数据传递
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D.2：CompilerInstance 重构为管线编排器" && git push origin HEAD
> ```

---

## Task D.3：保留旧路径作为 fallback

### 依赖

- D.2（CompilerInstance 重构）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Frontend/CompilerInstance.cpp` |

### 实现约束

1. 当 `--frontend` 和 `--backend` 都未指定时，使用旧路径（AST→LLVM IR→目标代码）
2. 当指定了任一选项时，使用新路径
3. 旧路径代码标记为 `// DEPRECATED: old pipeline`

### 验收标准

```bash
# V1: 旧路径仍可用
# blocktype test.cpp -o test_old
# ./test_old
# 退出码 == 0

# V2: 新路径可用
# blocktype --frontend=cpp --backend=llvm test.cpp -o test_new
# ./test_new
# 退出码 == 0

# V3: 新旧路径结果一致
# diff <(objdump -d test_old) <(objdump -d test_new)
# 输出为空
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D.3：保留旧路径作为 fallback" && git push origin HEAD
> ```

---

## Task D.4：自动注册机制

### 依赖

- D.2（CompilerInstance）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Frontend/CppFrontendRegistration.cpp` |
| 新增 | `src/Backend/LLVMBackendRegistration.cpp` |

### 必须实现的注册代码

```cpp
// CppFrontendRegistration.cpp
static struct CppFrontendRegistrator {
  CppFrontendRegistrator() {
    FrontendRegistry::instance().registerFrontend("cpp", createCppFrontend);
    auto& Reg = FrontendRegistry::instance();
    Reg.addExtensionMapping(".cpp", "cpp");
    Reg.addExtensionMapping(".cc", "cpp");
    Reg.addExtensionMapping(".cxx", "cpp");
    Reg.addExtensionMapping(".c", "cpp");
    Reg.addExtensionMapping(".h", "cpp");
  }
} CppFrontendRegistratorInstance;

// LLVMBackendRegistration.cpp
static struct LLVMBackendRegistrator {
  LLVMBackendRegistrator() {
    BackendRegistry::instance().registerBackend("llvm", createLLVMBackend);
  }
} LLVMBackendRegistratorInstance;
```

### 实现约束

1. 使用静态初始化注册（`static struct Registrator`）
2. CompilerInstance 析构时自动清理
3. 多 CompilerInstance 实例不冲突

### 验收标准

```cpp
// V1: 注册后可查找
assert(FrontendRegistry::instance().hasFrontend("cpp") == true);
assert(BackendRegistry::instance().hasBackend("llvm") == true);

// V2: 多实例不冲突
{
  CompilerInstance CI1;
  CompilerInstance CI2;
  // 两个实例独立工作
}
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D.4：自动注册机制" && git push origin HEAD
> ```

---

## Task D.5：集成测试

### 依赖

- D.1 ~ D.4 全部完成

### 验收标准

```bash
# V1: 新管线完整编译
# blocktype --frontend=cpp --backend=llvm test.cpp -o test
# ./test && echo "PASS"

# V2: 差分测试
# diff <(blocktype-old test.cpp -o /dev/null && objdump -d test_old.o) \
#       <(blocktype --frontend=cpp --backend=llvm test.cpp -o /dev/null && objdump -d test_new.o)
# 输出为空

# V3: 全部 lit 测试通过
# lit tests/ -v
# All tests passed
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D.5：集成测试" && git push origin HEAD
> ```

---

## Task E.1：全量测试回归

### 依赖

- Phase D 全部完成

### 验收标准

```bash
# V1: 新管线为默认路径
# blocktype test.cpp -o test
# 使用新管线（--frontend=cpp --backend=llvm）

# V2: 全部 lit 测试通过
# lit tests/ -v
# All tests passed

# V3: 全部 GTest 通过
# ctest --output-on-failure
# All tests passed
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E.1：全量测试回归" && git push origin HEAD
> ```

---

## Task E.2：性能基准测试

### 依赖

- E.1

### 验收标准

```bash
# V1: 编译速度 ≥ 95% 旧路径
# time blocktype-old test.cpp -o /dev/null  → T_old
# time blocktype-new test.cpp -o /dev/null  → T_new
# T_new / T_old >= 0.95

# V2: 目标代码大小差异 < 5%
# size test_old.o vs test_new.o
# 差异 < 5%

# V3: 内存峰值 ≤ 130% 旧路径
# /usr/bin/time -v blocktype-old test.cpp -o /dev/null  → M_old
# /usr/bin/time -v blocktype-new test.cpp -o /dev/null  → M_new
# M_new / M_old <= 1.30
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E.2：性能基准测试" && git push origin HEAD
> ```

---

## Task E.3：移除旧路径 fallback

### 依赖

- E.2

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Frontend/CompilerInstance.cpp` |

### 实现约束

1. 移除旧路径代码和 fallback 选项
2. 仅保留新管线
3. `--frontend` 和 `--backend` 选项保留

### 验收标准

```bash
# V1: 旧路径代码已移除
# grep -c "DEPRECATED: old pipeline" src/Frontend/CompilerInstance.cpp
# 输出 == 0

# V2: 新管线正常工作
# blocktype test.cpp -o test
# ./test && echo "PASS"
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E.3：移除旧路径 fallback" && git push origin HEAD
> ```

---

## Task E.4：CodeGen 模块清理

### 依赖

- E.3

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/CodeGen/` 下所有文件 |
| 修改 | `include/blocktype/CodeGen/` 下所有文件 |

### 实现约束

1. CodeGen 模块成为 LLVMBackend 内部实现细节
2. CodeGenModule/CodeGenFunction 等类不暴露给 IR 层
3. FrontendBase.h 不再包含任何 CodeGen 头文件
4. #include 依赖清理

### 验收标准

```bash
# V1: FrontendBase.h 不包含 CodeGen 头文件
# grep -c 'CodeGen/' include/blocktype/Frontend/FrontendBase.h
# 输出 == 0

# V2: 编译通过
# cmake --build build
# 退出码 == 0

# V3: 全部测试通过
# ctest --output-on-failure
# All tests passed
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E.4：CodeGen 模块清理" && git push origin HEAD
> ```

---

## Task E.5：文档更新

### 依赖

- E.4

### 验收标准

```bash
# V1: --frontend/--backend 用户文档存在
# blocktype --help | grep "frontend"
# blocktype --help | grep "backend"
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E.5：文档更新" && git push origin HEAD
> ```

---

## 附加增强任务

### Task C-F1：PluginManager 基础框架
**依赖**：C.1
**新增**：`include/blocktype/IR/IRPlugin.h`
**约束**：PluginManager 管理 CompilerPlugin 生命周期

### Task C-F2：CompilerPlugin 基类 + 加载机制
**依赖**：C-F1
**新增**：`src/IR/CompilerPlugin.cpp`
**约束**：CompilerPlugin 可注册 IRPass

### Task C-F3：FFIFunctionDecl 基础实现
**依赖**：Phase A（IRFunctionDecl）
**新增**：`include/blocktype/IR/IRFFI.h`
**约束**：FFIFunctionDecl 继承 IRFunctionDecl

### Task C-F4：FFITypeMapper C 语言映射
**依赖**：C-F3
**新增**：`src/Backend/FFITypeMapper.cpp`
**约束**：C ABI 类型映射

### Task C-F5：FrontendFuzzer 基础
**依赖**：Phase B
**新增**：`tests/fuzz/FrontendFuzzer.cpp`
**约束**：随机生成 C++ 源码测试前端

### Task C-F6：SARIF 格式诊断输出
**依赖**：B-F6（StructuredDiagEmitter）
**新增**：`src/Frontend/SARIFEmitter.cpp`
**约束**：SARIF v2.1 格式

### Task C-F7：FixIt Hint 基础实现
**依赖**：B-F6（StructuredDiagEmitter）
**新增**：`include/blocktype/Frontend/FixItHint.h`
**约束**：FixIt 与 StructuredDiagnostic 集成

### Task D-F1：InstructionSelector 接口定义 + LoweringRule + TargetInstruction

**依赖**：C.5

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/InstructionSelector.h` |
| 新增 | `src/Backend/InstructionSelector.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::backend {

struct LoweringRule {
  ir::Opcode SourceOp;
  ir::DialectID SourceDialect;
  std::string TargetPattern;
  std::string Condition;
  int Priority;
};

using TargetInstructionList = SmallVector<std::unique_ptr<TargetInstruction>, 8>;

class InstructionSelector {
public:
  virtual ~InstructionSelector() = default;
  virtual bool select(const ir::IRInstruction& I,
                      TargetInstructionList& Output) = 0;
  virtual bool loadRules(StringRef RuleFile) = 0;
  virtual bool verifyCompleteness() = 0;
};

class TargetInstruction {
  std::string Mnemonic;
  SmallVector<unsigned, 4> UsedRegs;
  SmallVector<unsigned, 4> DefRegs;
  SmallVector<ir::IRValue*, 2> IROperands;
public:
  StringRef getMnemonic() const { return Mnemonic; }
  ArrayRef<unsigned> getUsedRegs() const { return UsedRegs; }
  ArrayRef<unsigned> getDefRegs() const { return DefRegs; }
  ArrayRef<ir::IRValue*> getIROperands() const { return IROperands; }
};

}
```

**约束**：Phase A-E 仍使用 IRToLLVMConverter 直接映射，无行为变化；Phase F 引入 InstructionSelector 作为 IRToLLVMConverter 的内部子组件

**验收标准**

```cpp
// V1: LoweringRule 创建
backend::LoweringRule R;
R.SourceOp = ir::Opcode::Add;
R.SourceDialect = ir::DialectID::Core;
R.TargetPattern = "add32 %x %y";
R.Priority = 1;
assert(R.Priority == 1);

// V2: TargetInstruction 创建
auto TI = std::make_unique<backend::TargetInstruction>();
assert(TI->getMnemonic().empty());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D-F1：InstructionSelector 接口定义 + LoweringRule + TargetInstruction" && git push origin HEAD
> ```

---

### Task D-F2：RegisterAllocator 接口定义 + TargetFunction + TargetRegisterInfo + RegAllocFactory

**依赖**：C.5

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/RegisterAllocator.h` |
| 新增 | `src/Backend/RegisterAllocator.cpp` |
| 新增 | `src/Backend/GreedyRegAlloc.cpp` |
| 新增 | `src/Backend/BasicRegAlloc.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::backend {

enum class RegAllocStrategy {
  Greedy = 0,
  Fast   = 1,
  Basic  = 2,
};

class TargetFunction {
  std::string Name;
  ir::IRFunctionType* Signature;
  std::vector<TargetBasicBlock> Blocks;
  TargetFrameInfo FrameInfo;
public:
  StringRef getName() const { return Name; }
  ir::IRFunctionType* getSignature() const { return Signature; }
  std::vector<TargetBasicBlock>& getBlocks() { return Blocks; }
  TargetFrameInfo& getFrameInfo() { return FrameInfo; }
};

class TargetRegisterInfo {
  unsigned NumRegisters;
  SmallVector<unsigned, 16> CalleeSavedRegs;
  SmallVector<unsigned, 16> CallerSavedRegs;
  DenseMap<unsigned, unsigned> RegClassMap;
public:
  unsigned getNumRegisters() const { return NumRegisters; }
  ArrayRef<unsigned> getCalleeSavedRegs() const { return CalleeSavedRegs; }
  ArrayRef<unsigned> getCallerSavedRegs() const { return CallerSavedRegs; }
  bool isCalleeSaved(unsigned Reg) const;
  bool isCallerSaved(unsigned Reg) const;
};

class RegisterAllocator {
public:
  virtual ~RegisterAllocator() = default;
  virtual StringRef getName() const = 0;
  virtual RegAllocStrategy getStrategy() const = 0;
  virtual bool allocate(TargetFunction& F,
                        const TargetRegisterInfo& TRI) = 0;
};

class RegAllocFactory {
public:
  static std::unique_ptr<RegisterAllocator> create(RegAllocStrategy Strategy);
};

}
```

**策略选择指南**

| 策略 | 适用场景 | 质量 | 速度 | 实现阶段 |
|------|---------|------|------|---------|
| `Greedy` | 发布构建（-O2/-O3） | 高 | 中 | Phase D |
| `Fast` | 调试构建（-O0/-O1） | 中 | 快 | 远期 |
| `Basic` | 验证/测试 | 基础 | 快 | Phase D |

**约束**：Phase A-E 寄存器分配完全委托 LLVM（现有方式不变）

**验收标准**

```cpp
// V1: RegAllocFactory 创建
auto RA = backend::RegAllocFactory::create(backend::RegAllocStrategy::Greedy);
assert(RA != nullptr);
assert(RA->getStrategy() == backend::RegAllocStrategy::Greedy);

// V2: TargetFunction 创建
backend::TargetFunction TF;
assert(TF.getName().empty());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D-F2：RegisterAllocator 接口定义 + TargetFunction + TargetRegisterInfo + RegAllocFactory" && git push origin HEAD
> ```

---

### Task D-F3：后端管线模块化 — CodeEmitter/FrameLowering/TargetLowering 接口

**依赖**：D-F1, D-F2

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/CodeEmitter.h` |
| 新增 | `include/blocktype/Backend/FrameLowering.h` |
| 新增 | `include/blocktype/Backend/TargetLowering.h` |
| 修改 | `src/Backend/LLVMBackend.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::backend {

class CodeEmitter {
public:
  virtual ~CodeEmitter() = default;
  virtual bool emit(const TargetFunction& F,
                    const TargetMachine& TM,
                    raw_ostream& OS) = 0;
  virtual bool emitModule(const SmallVectorImpl<TargetFunction>& Functions,
                           const TargetMachine& TM,
                           raw_ostream& OS) = 0;
};

class FrameLowering {
public:
  virtual ~FrameLowering() = default;
  virtual void lower(TargetFunction& F,
                      const TargetRegisterInfo& TRI,
                      const ABIInfo& ABI) = 0;
  virtual uint64_t getStackSize(const TargetFunction& F) const = 0;
};

class TargetLowering {
public:
  virtual ~TargetLowering() = default;
  virtual bool lower(const ir::IRInstruction& I,
                      TargetInstructionList& Output) = 0;
  virtual bool supportsDialect(ir::DialectID D) const = 0;
};

}
```

**后端管线执行序列**

```
BackendBase::emitObject(IRModule)
    ├── 1. InstructionSelector::select(IRInst) → TargetInstructionList
    ├── 2. TargetLowering::lower(IRInst) → TargetInstructionList (Dialect!=Core)
    ├── 3. FrameLowering::lower(TargetFunction, TRI, ABI)
    ├── 4. RegisterAllocator::allocate(TargetFunction, TRI)
    ├── 5. DebugInfoEmitter::emitDebugInfo(IRModule, OS)
    └── 6. CodeEmitter::emitModule(Functions, TM, OS)
```

**约束**：步骤3必须在步骤4之前（栈帧大小影响寄存器分配）；步骤5可与步骤6并行；步骤6必须在步骤4之后

**验收标准**

```cpp
// V1: CodeEmitter 接口
// （仅接口定义，LLVM后端内部使用）

// V2: FrameLowering 接口
// （仅接口定义，LLVM后端内部使用）

// V3: TargetLowering 接口
// （仅接口定义，LLVM后端内部使用）
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D-F3：后端管线模块化 — CodeEmitter/FrameLowering/TargetLowering 接口" && git push origin HEAD
> ```

---

### Task D-F4：DebugInfoEmitter 接口定义 + DWARF5 发射

**依赖**：A-F5（IRDebugMetadata）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Backend/DebugInfoEmitter.h` |
| 新增 | `src/Backend/DWARF5Emitter.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::backend {

class DebugInfoEmitter {
public:
  virtual ~DebugInfoEmitter() = default;
  virtual bool emit(const ir::IRModule& M, raw_ostream& OS) = 0;
  virtual bool emitDWARF5(const ir::IRModule& M, raw_ostream& OS) = 0;
  virtual bool emitDWARF4(const ir::IRModule& M, raw_ostream& OS) = 0;
  virtual bool emitCodeView(const ir::IRModule& M, raw_ostream& OS) = 0;
};

}
```

**IR调试元数据→LLVM调试信息映射表**

| IR 调试信息 | LLVM 调试信息 |
|-------------|--------------|
| `ir::DICompileUnit` | `llvm::DICompileUnit` |
| `ir::DIType` | `llvm::DIType` |
| `ir::DISubprogram` | `llvm::DISubprogram` |
| `ir::DILocation` | `llvm::DILocation` |

**约束**：DWARF5 是默认格式；CodeView 为 Windows 后端（远期）

**验收标准**

```cpp
// V1: DebugInfoEmitter 接口
// （仅接口定义，Phase E 实现）
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D-F4：DebugInfoEmitter 接口定义 + DWARF5 发射" && git push origin HEAD
> ```

---

### Task D-F5：BackendDiffTestIntegration 后端差分测试

**依赖**：C.9

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Testing/BackendDiffTestIntegration.h` |
| 新增 | `tests/diff/BackendDiffTest.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::testing {

class BackendDiffTestIntegration {
public:
  struct BackendDiffConfig {
    std::vector<std::string> BackendNames;
    DifferentialTester::Config::Granularity Level;
  };

  static DifferentialTester::DiffResult
  testBackendEquivalence(const ir::IRModule& M,
                          const BackendDiffConfig& Cfg,
                          BackendRegistry& Registry);
};

class BackendFuzzIntegration {
public:
  struct BackendFuzzConfig {
    StringRef BackendName;
    uint64_t MaxIterations;
    uint64_t Seed;
  };

  static IRFuzzer::FuzzResult
  fuzzBackend(const BackendFuzzConfig& Cfg,
              BackendRegistry& Registry);
};

}
```

**编译选项**

```
--ftest-differential          运行差分测试
--ftest-fuzz=<iterations>     运行模糊测试
--ftest-fuzz-seed=<seed>      模糊测试种子
--ftest-equivalence           运行等价性检查（Pass前后）
```

**约束**：差分测试对比同一IRModule经不同后端编译的结果

**验收标准**

```cpp
// V1: BackendDiffConfig 创建
testing::BackendDiffTestIntegration::BackendDiffConfig Cfg;
Cfg.BackendNames = {"llvm"};
assert(Cfg.BackendNames.size() == 1);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task D-F5：BackendDiffTestIntegration 后端差分测试" && git push origin HEAD
> ```

---

### Task E-F1：CompilationCacheManager 完整集成

**依赖**：A-F7（CacheKey/CacheEntry/LocalDiskCache）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Frontend/CompilationCacheManager.cpp` |

**必须实现的类型定义**

（CompilationCacheManager 接口已在 A-F7 中定义，此处为实现）

**后端缓存集成流程**

```
CompilerInstance::compileFile()
    ↓
1. CacheKey::compute(Source, Options)
    ↓
2. CompilationCacheManager::lookupObject(Key)
    ↓ 命中 → 直接返回缓存的目标文件
    ↓ 未命中 ↓
3. Frontend::compile() → IRModule
    ↓
4. CompilationCacheManager::lookupIR(Key)
    ↓ 命中 → 跳过前端，用缓存IR继续
    ↓ 未命中 ↓
5. CompilationCacheManager::storeIR(Key, IRModule)
    ↓
6. Backend::emitObject(IRModule) → 目标文件
    ↓
7. CompilationCacheManager::storeObject(Key, ObjectData)
    ↓
8. 返回目标文件
```

**约束**：缓存查询在编译管线入口处执行，命中则跳过对应阶段；IR缓存和目标文件缓存独立管理

**验收标准**

```cpp
// V1: CompilationCacheManager 集成
cache::CompilationCacheManager CCM;
CCM.enable("/tmp/btcache", 1024*1024*1024);
auto IR = CCM.lookupIR(Key, Ctx);
assert(!IR.has_value()); // 首次查询未命中
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E-F1：CompilationCacheManager 完整集成" && git push origin HEAD
> ```

---

### Task E-F2：IR 缓存序列化/反序列化

**依赖**：E-F1, A.7（IRSerializer）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/IR/IRSerializer.cpp` |

**约束**：缓存格式使用 IRBitcode

**验收标准**

```bash
# V1: IR 序列化到缓存
# 通过 CompilationCacheManager::storeIR() 写入
# 通过 CompilationCacheManager::lookupIR() 读回
# 验证：读回的 IRModule 与原始 IRModule 结构一致
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E-F2：IR 缓存序列化/反序列化" && git push origin HEAD
> ```

---

### Task E-F3：IREquivalenceChecker 语义等价检查实现

**依赖**：B-F4（IREquivalenceChecker 接口）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/IR/IREquivalenceChecker.cpp` |

**约束**：对比两个 IRModule 的结构等价性

**验收标准**

```cpp
// V1: 相同模块等价
auto Result = ir::IREquivalenceChecker::check(Module, Module);
assert(Result.IsEquivalent);

// V2: 不同模块不等价
auto Result2 = ir::IREquivalenceChecker::check(ModuleA, ModuleB);
assert(!Result2.IsEquivalent);
assert(!Result2.Differences.empty());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E-F3：IREquivalenceChecker 语义等价检查实现" && git push origin HEAD
> ```

---

### Task E-F4：Chrome Trace 格式输出

**依赖**：A-F3（TelemetryCollector）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/IR/ChromeTraceEmitter.cpp` |

**约束**：Chrome Trace Event Format

**验收标准**

```bash
# V1: Chrome Trace 输出
# 通过 TelemetryCollector::writeChromeTrace() 输出
# 可在 chrome://tracing 中可视化
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E-F4：Chrome Trace 格式输出" && git push origin HEAD
> ```

---

### Task E-F5：可重现构建模式实现

**依赖**：A-F8（IRIntegrityChecksum）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Frontend/CompilerInstance.cpp` |

**可重现构建行为列表**

1. 时间戳：使用 SOURCE_DATE_EPOCH 环境变量值（默认0）
2. 随机数：所有随机数生成器使用确定性种子（seed=0x42）
3. 遍历顺序：所有哈希表遍历改为排序后遍历（按key排序）
4. 内部计数器：ValueID/InstructionID使用确定性起始值（从1递增）
5. 临时文件名：使用确定性命名（而非随机后缀）
6. DWARF生产者字符串：使用固定版本号（不含构建时间）

**约束**：`--freproducible-build` 选项启用

**验收标准**

```bash
# V1: 可重现构建
bt --freproducible-build -c test.cpp -o test1.o
bt --freproducible-build -c test.cpp -o test2.o
sha256sum test1.o test2.o
# 两个文件的哈希值相同
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E-F5：可重现构建模式实现" && git push origin HEAD
> ```

---

### Task E-F6：IR 完整性验证集成

**依赖**：A-F8（IRIntegrityChecksum）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/Frontend/CompilerInstance.cpp` |

**约束**：`--fir-integrity-check` 选项触发；加载 IR 时验证校验和

**验收标准**

```bash
# V1: IR 完整性验证
bt --fir-integrity-check -c test.cpp
# 正常编译，无校验和错误
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E-F6：IR 完整性验证集成" && git push origin HEAD
> ```

---

### Task E-F7：FFICoerce/FFIUnwind 指令实现

**依赖**：A-F12（FFI 接口）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 修改 | `include/blocktype/IR/IRInstruction.h` |

**约束**：新增 Opcode::FFICoerce / Opcode::FFIUnwind

**验收标准**

```cpp
// V1: FFI 指令创建
IRInstruction FFICoerce(Opcode::FFICoerce, ...);
IRInstruction FFIUnwind(Opcode::FFIUnwind, ...);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(CDE): 完成 Task E-F7：FFICoerce/FFIUnwind 指令实现" && git push origin HEAD
> ```

---

### Task E-F8：CodeView 调试信息（Windows 后端）

**依赖**：D-F4（DebugInfoEmitter）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Backend/CodeViewEmitter.cpp` |

**约束**：MSVC 调试器兼容；Windows 后端选择 CodeView，其他平台选择 DWARF5

**验收标准**

```cpp
// V1: CodeView 发射接口
// DebugInfoEmitter::emitCodeView() 可调用
```
