# BlockType 多前端多后端编译器架构重构方案

## 红线 Checklist

| # | 红线 | 说明 |
|---|------|------|
| R1 | 架构优先 | 任何决策以架构正确性和灵活性为最高优先级 |
| R2 | 多前端多后端自由组合 | 前端和后端独立替换、自由组合，无硬耦合 |
| R3 | 渐进式改造 | 每阶段可编译、可测试，禁止一次性重写 |
| R4 | 现有功能不退化 | 改造中所有功能保持等价或更优 |
| R5 | 接口抽象优先 | 新增代码通过抽象接口交互 |
| R6 | IR 中间层解耦 | 前端产出 IR，后端消费 IR，前后端不直接交互 |

---

## 一、总体架构设计

### 1.1 架构总览

```
┌────────────────────────────────────────────────────────────┐
│              CompilerInstance (管线编排器)                    │
│       --frontend=cpp --backend=llvm                        │
└──────────┬─────────────────────────────┬───────────────────┘
           │                             │
           ▼                             ▼
┌──────────────────┐          ┌──────────────────────────┐
│  前端层 Frontend  │          │    后端层 Backend         │
│                  │          │                          │
│ ┌──────────────┐ │          │ ┌──────────────┐         │
│ │ CppFrontend  │ │          │ │ LLVMBackend  │         │
│ │ PP→Parser→   │ │          │ │ IR→LLVM IR→  │         │
│ │ Sema→AST→IR  │ │          │ │ Opt→ObjFile  │         │
│ └──────┬───────┘ │          │ └──────────────┘         │
│ ┌──────┴───────┐ │          │ ┌──────────────┐         │
│ │ FrontendBase │ │          │ │BackendBase   │         │
│ │ (抽象接口)    │ │          │ │(抽象接口)     │         │
│ └──────────────┘ │          │ └──────────────┘         │
│ ┌──────────────┐ │          │ ┌──────────────┐         │
│ │ CFrontend*   │ │          │ │CraneliftBE*  │         │
│ └──────────────┘ │          │ └──────────────┘         │
└──────────┬───────┘          └───────────┬──────────────┘
           │ 产出 IR                      │ 消费 IR
           ▼                              ▼
┌──────────────────────────────────────────────────────────┐
│              IR 中间层 (BlockType IR / BTIR)              │
│  IRType | IRValue | IRModule/Function/Block | IRBuilder  │
│  IRVerifier | IRSerializer | IRDebugInfo | IRPassManager │
└──────────────────────────────────────────────────────────┘
```

**各层职责**：

| 层 | 职责 | 输入 | 输出 | 关键接口 |
|----|------|------|------|----------|
| 前端层 | 源代码→AST→IR | 源代码+LangOpts | `unique_ptr<IRModule>` | `FrontendBase::compile()` |
| IR层 | 类型/值/模块/验证/序列化 | 前端产出的IR | 后端可消费的IR | `IRModule`, `IRBuilder` |
| 后端层 | IR→目标代码 | `const IRModule&`+TargetOpts | 目标文件/汇编 | `BackendBase::emitObject()` |

**解耦策略**：前端只依赖IR层头文件，后端只依赖IR层头文件，前后端零直接依赖。IR层作为独立库`libblocktype-ir`，不依赖LLVM，不依赖AST。

**自由组合机制**：
```cpp
class FrontendRegistry {
  static void registerFrontend(StringRef Name, Factory F);
  static unique_ptr<FrontendBase> create(StringRef Name, const FrontendOptions&);
};
class BackendRegistry {
  static void registerBackend(StringRef Name, Factory F);
  static unique_ptr<BackendBase> create(StringRef Name, const BackendOptions&);
};
```

### 1.2 IR 层设计

#### 1.2.1 核心决策：自定义 IR vs MLIR vs LLVM IR

| 方案 | 优势 | 劣势 | 评分 |
|------|------|------|------|
| **自定义IR(BTIR)** | 完全可控、轻量、无外部依赖、精确匹配语义 | 需自建类型系统/验证器 | ★★★★★ |
| MLIR | 成熟框架、多级Dialect | 重度依赖LLVM、与解耦目标矛盾 | ★★ |
| LLVM IR | 无需额外实现 | 与LLVM硬耦合，违反R6 | ★ |

**选择：自定义IR（BTIR）**。理由：R6合规、R1合规、轻量渐进、已有先例（GCC GIMPLE/Rust MIR/Swift SIL）。

#### 1.2.2 IR 类型系统

```
IRType (基类: Kind枚举, equals(), toString(), getSizeInBits())
├── IRVoidType
├── IRIntegerType     (BitWidth: 1/8/16/32/64/128)
├── IRFloatType       (BitWidth: 16/32/64/80/128)
├── IRPointerType     (PointeeType*, AddressSpace)
├── IRArrayType       (NumElements, ElementType*)
├── IRStructType      (Name, FieldTypes[], IsPacked)
├── IRFunctionType    (ReturnType*, ParamTypes[], IsVarArg)
├── IRVectorType      (NumElements, ElementType*)
└── IROpaqueType      (前向声明)
```

`IRTypeContext`：类型工厂+缓存，拥有所有IRType实例生命周期。

**与llvm::Type*映射**（仅LLVM后端IRToLLVMConverter中）：

| BTIR | LLVM |
|------|------|
| IRIntegerType(32) | i32 |
| IRFloatType(64) | double |
| IRPointerType(T) | T* |
| IRStructType | llvm::StructType |
| IRFunctionType | llvm::FunctionType |

#### 1.2.3 IR 指令集

| 类别 | 操作码 |
|------|--------|
| 终结 | Ret, Br, CondBr, Switch, Unreachable |
| 整数二元 | Add, Sub, Mul, UDiv, SDiv, URem, SRem |
| 浮点二元 | FAdd, FSub, FMul, FDiv, FRem |
| 位运算 | Shl, LShr, AShr, And, Or, Xor |
| 内存 | Alloca, Load, Store, GEP |
| 转换 | Trunc, ZExt, SExt, FPTrunc, FPExt, FPToSI, SIToFP, PtrToInt, IntToPtr, BitCast |
| 比较 | ICmp, FCmp |
| 调用 | Call, Invoke |
| 其他 | Phi, Select, ExtractValue, InsertValue |
| 调试 | DbgDeclare, DbgValue |

#### 1.2.4 IR 值系统

```
IRValue (基类: IRType*, ValueID, Name)
├── IRInstruction (Opcode, Operands[], ParentBB)
├── IRArgument (ParamType, Name, Attributes)
├── IRConstant
│   ├── IRConstantInt, IRConstantFP, IRConstantNull
│   ├── IRConstantStruct, IRConstantArray
│   └── IRConstantUndef, IRConstantZero
└── IRBasicBlock (作为跳转目标)
```

#### 1.2.5 IR 模块层次

```
IRModule (Name, TargetTriple, DataLayoutStr)
├── IRGlobalVariable[] (Name, Linkage, Type, Initializer, Align, IsConstant)
├── IRFunction[] (Name, Linkage, CallingConv, Attributes, Arguments[], BasicBlocks[])
│   └── IRBasicBlock[] → IRInstruction[] (Opcode, Operands[], Metadata)
├── IRFunctionDecl[] (外部声明)
└── IRMetadata[] (调试信息等)
```

#### 1.2.6 IR 与 AST 映射

| AST | IR |
|-----|----|
| TranslationUnitDecl | IRModule |
| FunctionDecl | IRFunction |
| VarDecl(全局) | IRGlobalVariable |
| VarDecl(局部) | Alloca指令 |
| Stmt | IRInstruction[] |
| Expr | IRValue* |
| QualType | IRType* |
| RecordDecl | IRStructType |

#### 1.2.7 IR 构建器

```cpp
class IRBuilder {
  IRTypeContext& TypeCtx;
  IRBasicBlock* InsertBB;
public:
  void createRet(IRValue* V); void createRetVoid();
  void createBr(IRBasicBlock* Dest);
  void createCondBr(IRValue* C, IRBasicBlock* T, IRBasicBlock* F);
  IRValue* createAdd/Sub/Mul/SDiv/UDiv/...;
  IRValue* createAlloca(IRType* Ty);
  IRValue* createLoad(IRType* Ty, IRValue* Ptr);
  void createStore(IRValue* V, IRValue* Ptr);
  IRValue* createGEP(IRType* Ty, IRValue* Ptr, ArrayRef<IRValue*> Idx);
  IRValue* createTrunc/ZExt/SExt/BitCast/...;
  IRValue* createICmp(CmpPred, IRValue* L, IRValue* R);
  IRValue* createFCmp(CmpPred, IRValue* L, IRValue* R);
  IRValue* createCall(IRFunction* Callee, ArrayRef<IRValue*> Args);
  IRValue* createPhi(IRType* Ty, unsigned N);
  IRValue* createSelect(IRValue* C, IRValue* T, IRValue* F);
};
```

#### 1.2.8 IR 验证器

验证规则：类型一致性、终结指令（每BB恰好一个）、SSA规则、引用完整性、函数完整性。

#### 1.2.9 IR 序列化

文本格式（类LLVM IR）+ 未来二进制格式。

### 1.3 前端抽象层

```cpp
class FrontendBase {
protected:
  FrontendOptions Opts; DiagnosticsEngine Diags;
public:
  virtual unique_ptr<ir::IRModule> compile(
    StringRef Filename, ir::IRTypeContext& TypeCtx, const TargetLayout& Layout) = 0;
  virtual StringRef getName() const = 0;
  virtual StringRef getLanguage() const = 0;
};
```

**CppFrontend**：包装现有PP/Parser/Sema + 新增ASTToIRConverter。

### 1.4 后端抽象层

```cpp
class BackendBase {
protected:
  BackendOptions Opts;
public:
  virtual bool emitObject(const ir::IRModule& Mod, StringRef OutputPath) = 0;
  virtual bool emitAssembly(const ir::IRModule& Mod, StringRef OutputPath) = 0;
  virtual bool emitIRText(const ir::IRModule& Mod, raw_ostream& OS) = 0;
  virtual StringRef getName() const = 0;
};
```

**LLVMBackend**：IR→LLVM IR转换 + 优化 + 目标代码生成。

### 1.5 编译管线编排

CompilerInstance重构为管线编排器，通过FrontendRegistry/BackendRegistry选择前端/后端，前端产出IRModule，后端消费IRModule。新增选项：`--frontend=cpp`, `--backend=llvm`, `--emit-btir`, `--verify-btir`。

---

## 二、具体技术方案

### 2.1 IR 层实现

#### 2.1.1 文件布局

```
include/blocktype/IR/
  IRType.h, IRTypeContext.h, TargetLayout.h
  IRValue.h, IRInstruction.h, IRConstant.h
  IRModule.h, IRFunction.h, IRBasicBlock.h
  IRBuilder.h, IRVerifier.h, IRSerializer.h
  IRDebugInfo.h, IRMetadata.h
  FrontendBase.h, FrontendRegistry.h
  BackendBase.h, BackendRegistry.h

src/IR/
  IRType.cpp, IRTypeContext.cpp, TargetLayout.cpp
  IRValue.cpp, IRInstruction.cpp, IRConstant.cpp
  IRModule.cpp, IRFunction.cpp, IRBasicBlock.cpp
  IRBuilder.cpp, IRVerifier.cpp, IRSerializer.cpp
  IRDebugInfo.cpp, IRMetadata.cpp
  FrontendRegistry.cpp, BackendRegistry.cpp
  CMakeLists.txt
```

**CMake**：`libblocktype-ir` 不链接 LLVM，仅依赖 `blocktype-basic`（StringRef/SmallVector 等）。

#### 2.1.2 TargetLayout — 独立于 LLVM DataLayout

```cpp
class TargetLayout {
  std::string TripleStr;
  uint64_t PointerSize, PointerAlign;
  uint64_t IntSize, LongSize, LongLongSize;
  uint64_t FloatSize, DoubleSize, LongDoubleSize;
  uint64_t MaxVectorAlign;
  bool IsLittleEndian, IsLinux, IsMacOS;
public:
  explicit TargetLayout(StringRef TargetTriple);
  uint64_t getTypeSizeInBits(IRType* T) const;
  uint64_t getTypeAlignInBits(IRType* T) const;
  static unique_ptr<TargetLayout> Create(StringRef Triple);
};
```

与现有`TargetInfo`的关系：`TargetLayout`是`TargetInfo`的IR层镜像。未来`TargetInfo`可内部委托给`TargetLayout`。

#### 2.1.3 IRType 详细设计

```cpp
namespace blocktype::ir {

class IRType {
public:
  enum Kind { Void, Bool, Integer, Float, Pointer, Array, Struct, Function, Vector, Opaque };
  Kind getKind() const;
  virtual bool equals(const IRType* Other) const = 0;
  virtual std::string toString() const = 0;
  virtual uint64_t getSizeInBits(const TargetLayout& Layout) const = 0;
protected:
  IRType(Kind K) : KindVal(K) {}
  Kind KindVal;
};

class IRIntegerType : public IRType {
  unsigned BitWidth;
public:
  IRIntegerType(unsigned BW) : IRType(Integer), BitWidth(BW) {}
  unsigned getBitWidth() const { return BitWidth; }
};

class IRFloatType : public IRType {
  unsigned BitWidth; // 16, 32, 64, 80, 128
public:
  IRFloatType(unsigned BW) : IRType(Float), BitWidth(BW) {}
};

class IRPointerType : public IRType {
  IRType* PointeeType; unsigned AddressSpace;
public:
  IRPointerType(IRType* P, unsigned AS = 0) : IRType(Pointer), PointeeType(P), AddressSpace(AS) {}
};

class IRStructType : public IRType {
  std::string Name; SmallVector<IRType*, 16> FieldTypes; bool IsPacked;
public:
  IRStructType(StringRef N, SmallVector<IRType*, 16> F, bool P = false)
    : IRType(Struct), Name(N.str()), FieldTypes(std::move(F)), IsPacked(P) {}
};

class IRFunctionType : public IRType {
  IRType* ReturnType; SmallVector<IRType*, 8> ParamTypes; bool IsVarArg;
public:
  IRFunctionType(IRType* R, SmallVector<IRType*, 8> P, bool VA = false)
    : IRType(Function), ReturnType(R), ParamTypes(std::move(P)), IsVarArg(VA) {}
};

class IRTypeContext {
  DenseMap<unsigned, unique_ptr<IRIntegerType>> IntTypes;
  DenseMap<unsigned, unique_ptr<IRFloatType>> FloatTypes;
  IRType* VoidType; IRType* BoolType;
  DenseMap<std::pair<IRType*, unsigned>, unique_ptr<IRPointerType>> PointerTypes;
  DenseMap<std::string, unique_ptr<IRStructType>> NamedStructTypes;
public:
  IRType* getVoidType(); IRType* getBoolType();
  IRIntegerType* getIntType(unsigned BW);
  IRFloatType* getFloatType(unsigned BW);
  IRPointerType* getPointerType(IRType* P, unsigned AS = 0);
  IRStructType* getStructType(StringRef N, SmallVector<IRType*, 16> F);
  IRFunctionType* getFunctionType(IRType* R, SmallVector<IRType*, 8> P, bool VA = false);
};

} // namespace blocktype::ir
```

#### 2.1.4 IRValue/IRInstruction 详细设计

```cpp
namespace blocktype::ir {

enum class Opcode : uint8_t {
  Ret, Br, CondBr, Switch, Unreachable,
  Add, Sub, Mul, UDiv, SDiv, URem, SRem,
  FAdd, FSub, FMul, FDiv, FRem,
  Shl, LShr, AShr, And, Or, Xor,
  Alloca, Load, Store, GEP,
  Trunc, ZExt, SExt, FPTrunc, FPExt, FPToSI, SIToFP, PtrToInt, IntToPtr, BitCast,
  ICmp, FCmp, Call, Invoke, Phi, Select,
  ExtractValue, InsertValue, DbgDeclare, DbgValue
};

class IRValue {
  IRType* Ty; unsigned ValueID; std::string Name;
public:
  IRValue(IRType* T, unsigned ID, StringRef N = "") : Ty(T), ValueID(ID), Name(N.str()) {}
  IRType* getType() const { return Ty; }
  StringRef getName() const { return Name; }
};

class IRInstruction : public IRValue {
  Opcode Op; IRBasicBlock* Parent; SmallVector<IRValue*, 4> Operands;
public:
  Opcode getOpcode() const { return Op; }
  IRBasicBlock* getParent() const { return Parent; }
};

class IRConstantInt : public IRConstant { /* APInt Value */ };
class IRConstantFP : public IRConstant { /* APFloat Value */ };

class IRBasicBlock {
  IRFunction* Parent; std::string Name;
  SmallVector<unique_ptr<IRInstruction>> Instructions;
public:
  IRInstruction* getTerminator();
};

class IRFunction {
  IRFunctionType* FnType; std::string Name;
  LinkageType Linkage; unsigned CallingConv;
  SmallVector<unique_ptr<IRBasicBlock>> BasicBlocks;
  SmallVector<IRArgument*> Arguments;
};

class IRModule {
  IRTypeContext& TypeCtx; std::string Name; std::string TargetTriple;
  SmallVector<unique_ptr<IRFunction>> Functions;
  SmallVector<unique_ptr<IRGlobalVariable>> Globals;
  SmallVector<unique_ptr<IRMetadata>> Metadata;
};

} // namespace blocktype::ir
```

---

### 2.2 前端适配

#### 2.2.1 ASTToIRConverter — AST→IR 转换层

这是C++前端的核心新组件，将现有CodeGen逻辑从"直接生成LLVM IR"改为"生成BTIR"：

```cpp
class ASTToIRConverter {
  ir::IRTypeContext& TypeCtx;
  const TargetLayout& Layout;
  DiagnosticsEngine& Diags;
  unique_ptr<ir::IRModule> TheModule;
  unique_ptr<ir::IRBuilder> Builder;
  
  DenseMap<const Type*, ir::IRType*> TypeMap;
  DenseMap<const Decl*, ir::IRValue*> DeclMap;
  DenseMap<const VarDecl*, ir::IRValue*> LocalDecls;
  
public:
  ASTToIRConverter(ir::IRTypeContext& TC, const TargetLayout& L, DiagnosticsEngine& D);
  unique_ptr<ir::IRModule> convert(TranslationUnitDecl* TU);
private:
  ir::IRType* convertType(QualType T);
  void convertFunction(FunctionDecl* FD);
  ir::IRValue* convertExpr(Expr* E);
  void convertStmt(Stmt* S);
  // C++特有
  void convertCXXConstructor(CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);
  void convertCXXDestructor(CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);
  ir::IRValue* convertCXXConstructExpr(CXXConstructExpr* CCE);
  ir::IRValue* convertVirtualCall(CXXMemberCallExpr* MCE);
  std::string mangleName(const FunctionDecl* FD);
};
```

#### 2.2.2 现有 CodeGen 重构策略

**三阶段渐进式**：

| 阶段 | 策略 | 说明 |
|------|------|------|
| 共存期 | 新增ASTToIRConverter，与CodeGenModule并行 | CppFrontend用新路径，旧路径不变 |
| 迁移期 | 逐步将CodeGenFunction逻辑迁移到ASTToIRConverter | 每迁移一个Expr类型，验证BTIR正确性 |
| 替换期 | ASTToIRConverter覆盖全部后，CodeGenModule仅LLVM后端内部使用 | CodeGen从"前端组件"变为"后端内部实现" |

**组件映射**：

| 现有组件 | 新角色 |
|----------|--------|
| CodeGenModule | → ASTToIRConverter(前端侧) |
| CodeGenFunction | → ASTToIRConverter(前端侧) |
| CodeGenTypes | → ASTToIRConverter::convertType() |
| CodeGenConstant | → ASTToIRConverter::convertConstantExpr() |
| CGCXX | → ASTToIRConverter::convertCXX*() |
| Mangler | → IRMangler(独立于CodeGenModule) |
| CGDebugInfo | → IRDebugInfoBuilder(IR层元数据) |

#### 2.2.3 CGCXX（81KB）拆分方案

```
CGCXX.cpp (81KB)
├── CGClassLayout.cpp  ~15KB  — ComputeClassLayout, GetFieldOffset, GetClassSize
├── CGConstructor.cpp  ~15KB  — EmitConstructor, EmitBaseInitializer, EmitMemberInitializer
├── CGDestructor.cpp   ~10KB  — EmitDestructor, EmitDestructorBody, EmitDestructorCall
├── CGVTable.cpp       ~20KB  — EmitVTable, GetVTableIndex, EmitVirtualCall, EmitThunk, EmitVTT
├── CGRTTI.cpp         ~10KB  — EmitTypeInfo, EmitCatchTypeInfo, EmitDynamicCast
└── CGCXXMisc.cpp      ~11KB  — EmitStaticOperatorCall, EmitBaseOffset, EmitCastToBase/Derived
```

**适配策略**：
- 布局计算→移入ASTToIRConverter，输出IR结构体类型
- 构造/析构→移入ASTToIRConverter，输出IR指令序列
- VTable/RTTI→移入LLVM后端的IRToLLVMConverter（Itanium ABI特定概念）
- Mangler→独立为IRMangler，仅依赖AST类型，不依赖CodeGenModule

#### 2.2.4 Mangler 后端无关化

当前`Mangler`持有`CodeGenModule&`引用，仅用于获取`ASTContext`和`TargetInfo`。重构为`IRMangler`，仅依赖`ASTContext&`和`TargetLayout&`。

#### 2.2.5 TargetInfo/ABIInfo 归属调整

| 组件 | 当前归属 | 重构后归属 | 说明 |
|------|---------|-----------|------|
| TargetInfo | CodeGen/ | IR/(TargetLayout) + Backend/ | 布局信息→IR层，LLVM特定→后端 |
| ABIInfo | CodeGen/ | Backend/ | ABI分类是后端特定概念 |
| ABIArgInfo | CodeGen/CodeGenTypes.h | IR/ | 参数传递方式是IR层概念 |

**ABIArgInfo改造**：将`ABIArgInfo`中的`llvm::Type* SRetType`替换为`ir::IRType* SRetType`。

---

### 2.3 后端适配

#### 2.3.1 IR→LLVM IR 转换层

```cpp
class IRToLLVMConverter {
  llvm::LLVMContext& LLVMCtx;
  const BackendOptions& Opts;
  unique_ptr<llvm::Module> TheModule;
  
  DenseMap<const ir::IRType*, llvm::Type*> TypeMap;
  DenseMap<const ir::IRValue*, llvm::Value*> ValueMap;
  DenseMap<const ir::IRFunction*, llvm::Function*> FunctionMap;
  DenseMap<const ir::IRBasicBlock*, llvm::BasicBlock*> BlockMap;
  
public:
  unique_ptr<llvm::Module> convert(const ir::IRModule& Mod);
private:
  llvm::Type* convertType(const ir::IRType* T);
  llvm::Function* convertFunctionDecl(const ir::IRFunction* Fn);
  void convertFunctionBody(const ir::IRFunction* Fn, llvm::Function* LLVMFn);
  llvm::Value* convertInstruction(const ir::IRInstruction* Inst);
  llvm::Constant* convertConstant(const ir::IRConstant* C);
};
```

#### 2.3.2 优化 Pass 抽象

初期直接在LLVM后端内部使用LLVM Pass（复用现有`llvm::PassBuilder`），未来可扩展IR层Pass。

#### 2.3.3 目标代码生成抽象

`LLVMCodeGenerator`复用现有`CompilerInstance::generateObjectFile`逻辑。

#### 2.3.4 调试信息抽象

`IRDebugInfoBuilder`在IR层记录源位置信息，LLVM后端将IR调试元数据转换为LLVM DWARF元数据。

---

## 三、渐进式改造方案

### 3.1 改造阶段划分

```
Phase A ──→ Phase B ──→ Phase C ──→ Phase D ──→ Phase E
IR基础设施    前端抽象层   后端抽象层    管线重构      验证迁移
```

每个Phase完成后必须：编译通过 + 全部现有测试通过 + 新增IR层测试通过。

### 3.2 Phase A：IR 层基础设施

**目标**：建立独立的IR库，包含类型系统、值系统、构建器、验证器。

**红线对照**：
- R1✓ IR层独立于LLVM和AST，架构正确
- R2✓ IR层是前后端自由组合的基础
- R3✓ 纯新增代码，不影响现有功能
- R4✓ 现有功能零影响
- R5✓ IR层全部通过抽象接口设计
- R6✓ IR层实现前后端解耦的核心机制

**Task 分解**：

| Task | 内容 | 预估 | 文件变更 | 验收标准 | 风险 |
|------|------|------|---------|---------|------|
| A.1 | IRType体系+IRTypeContext | 3天 | 新增6个文件 | 所有类型可创建、缓存、比较 | 低 |
| A.2 | TargetLayout(独立于LLVM DataLayout) | 1天 | 新增2个文件 | x86_64/aarch64布局正确 | 低 |
| A.3 | IRValue+IRConstant体系 | 2天 | 新增4个文件 | 常量可创建、类型正确 | 低 |
| A.4 | IRModule/IRFunction/IRBasicBlock | 2天 | 新增4个文件 | 模块结构可构建 | 低 |
| A.5 | IRBuilder | 2天 | 新增2个文件 | 所有指令可创建 | 低 |
| A.6 | IRVerifier | 1天 | 新增2个文件 | 验证规则全部实现 | 低 |
| A.7 | IRSerializer(文本格式) | 1天 | 新增2个文件 | IR可序列化/反序列化 | 低 |
| A.8 | CMake集成+单元测试 | 1天 | CMakeLists.txt | libblocktype-ir编译通过，测试通过 | 低 |

**Phase A 总计**：~13天

### 3.3 Phase B：前端抽象层 + C++ 前端适配

**目标**：建立FrontendBase/FrontendRegistry，实现CppFrontend，实现ASTToIRConverter。

**红线对照**：
- R1✓ 前端通过抽象接口与后端解耦
- R2✓ 前端可独立替换
- R3✓ CppFrontend与现有CodeGenModule并行存在，渐进迁移
- R4✓ 现有编译路径不变，新路径独立验证
- R5✓ FrontendBase抽象接口
- R6✓ 前端产出IRModule，后端消费IRModule

**Task 分解**：

| Task | 内容 | 预估 | 文件变更 | 验收标准 | 风险 |
|------|------|------|---------|---------|------|
| B.1 | FrontendBase+FrontendRegistry | 1天 | 新增4个文件 | 注册/创建前端正常 | 低 |
| B.2 | IRMangler(独立于CodeGenModule) | 2天 | 新增2个文件 | 名称修饰结果与现有Mangler一致 | 中 |
| B.3 | ASTToIRConverter:类型转换 | 2天 | 新增2个文件 | 所有QualType→IRType转换正确 | 中 |
| B.4 | ASTToIRConverter:表达式(基础) | 3天 | 新增2个文件 | BinaryOp/UnaryOp/CallExpr/MemberExpr/DeclRef/Cast转换正确 | 中 |
| B.5 | ASTToIRConverter:表达式(C++特有) | 3天 | 新增2个文件 | CXXConstruct/New/Delete/This/VirtualCall转换正确 | 高 |
| B.6 | ASTToIRConverter:语句 | 2天 | 新增2个文件 | If/For/While/Return/Switch转换正确 | 中 |
| B.7 | ASTToIRConverter:C++类布局 | 2天 | 新增2个文件 | 类布局计算与现有CGCXX一致 | 高 |
| B.8 | ASTToIRConverter:构造/析构函数 | 3天 | 新增2个文件 | 构造/析构IR生成正确 | 高 |
| B.9 | CppFrontend实现 | 1天 | 新增2个文件 | CppFrontend可编译源文件到IR | 中 |
| B.10 | 集成测试 | 2天 | 新增测试文件 | 现有测试全部通过+IR输出验证 | 中 |

**Phase B 总计**：~21天

**CGCXX拆分策略**（在B.7-B.8期间执行）：
1. 先拆分CGCXX.cpp为6个子文件（物理拆分，逻辑不变）
2. 然后逐步将各子文件的逻辑迁移到ASTToIRConverter
3. VTable/RTTI逻辑留在LLVM后端（Itanium ABI特定）

### 3.4 Phase C：后端抽象层 + LLVM 后端适配

**目标**：建立BackendBase/BackendRegistry，实现LLVMBackend，实现IRToLLVMConverter。

**红线对照**：
- R1✓ 后端通过抽象接口与前端解耦
- R2✓ 后端可独立替换
- R3✓ LLVMBackend与现有编译路径并行
- R4✓ 现有功能通过新路径等价实现
- R5✓ BackendBase抽象接口
- R6✓ 后端消费IRModule，不直接接触AST

**Task 分解**：

| Task | 内容 | 预估 | 文件变更 | 验收标准 | 风险 |
|------|------|------|---------|---------|------|
| C.1 | BackendBase+BackendRegistry | 1天 | 新增4个文件 | 注册/创建后端正常 | 低 |
| C.2 | IRToLLVMConverter:类型转换 | 2天 | 新增2个文件 | 所有IRType→llvm::Type转换正确 | 中 |
| C.3 | IRToLLVMConverter:值/指令转换 | 3天 | 新增2个文件 | 所有IR指令→LLVM IR指令转换正确 | 中 |
| C.4 | IRToLLVMConverter:函数/模块转换 | 2天 | 新增2个文件 | IRModule→llvm::Module转换正确 | 中 |
| C.5 | LLVMBackend:优化pipeline | 1天 | 新增2个文件 | 复用现有优化逻辑 | 低 |
| C.6 | LLVMBackend:目标代码生成 | 1天 | 新增2个文件 | 复用现有TargetMachine逻辑 | 低 |
| C.7 | LLVMBackend:调试信息转换 | 2天 | 新增2个文件 | IR调试元数据→DWARF | 中 |
| C.8 | LLVMBackend:VTable/RTTI生成 | 2天 | 新增2个文件 | VTable/RTTI在LLVM后端生成 | 高 |
| C.9 | 集成测试 | 2天 | 新增测试文件 | 端到端编译测试通过 | 中 |

**Phase C 总计**：~16天

### 3.5 Phase D：编译管线重构

**目标**：重构CompilerInstance为管线编排器，支持`--frontend`/`--backend`选项。

**红线对照**：
- R1✓ CompilerInstance成为纯粹的编排器
- R2✓ 前端/后端通过选项自由组合
- R3✓ 渐进替换，旧路径保留到Phase E
- R4✓ 默认行为不变（cpp+llvm）
- R5✓ 通过FrontendBase*/BackendBase*接口操作
- R6✓ 编排器只传递IRModule

**Task 分解**：

| Task | 内容 | 预估 | 文件变更 | 验收标准 | 风险 |
|------|------|------|---------|---------|------|
| D.1 | CompilerInvocation新增FrontendName/BackendName选项 | 1天 | 修改2个文件 | 命令行解析正确 | 低 |
| D.2 | CompilerInstance重构为管线编排器 | 2天 | 修改2个文件 | 新管线编译通过 | 中 |
| D.3 | 保留旧路径作为fallback | 1天 | 修改1个文件 | 无frontend/backend选项时走旧路径 | 低 |
| D.4 | Driver层适配 | 1天 | 修改1-2个文件 | 命令行工具正常 | 低 |
| D.5 | 集成测试 | 1天 | 新增测试文件 | 所有编译场景通过 | 中 |

**Phase D 总计**：~6天

### 3.6 Phase E：验证和迁移

**目标**：所有现有功能迁移到新架构，移除旧路径依赖，全面验证。

**红线对照**：
- R1✓ 最终架构完全符合设计
- R2✓ 前后端完全解耦
- R3✓ 每个迁移步骤可独立验证
- R4✓ 全量测试回归
- R5✓ 无直接依赖具体实现
- R6✓ 前后端仅通过IR交互

**Task 分解**：

| Task | 内容 | 预估 | 文件变更 | 验收标准 | 风险 |
|------|------|------|---------|---------|------|
| E.1 | 全量测试回归(新管线) | 2天 | 无修改 | 所有现有测试通过 | 中 |
| E.2 | 性能基准测试 | 1天 | 新增基准 | 编译速度/代码质量不低于旧路径 | 中 |
| E.3 | 移除旧路径fallback | 1天 | 修改2-3个文件 | 编译通过，旧路径代码移除 | 低 |
| E.4 | CodeGen模块清理 | 2天 | 重构多个文件 | CodeGen仅被LLVM后端使用 | 中 |
| E.5 | 文档更新 | 1天 | 更新文档 | 架构文档与代码一致 | 低 |

**Phase E 总计**：~7天

---

## 四、风险评估

### 4.1 编译/测试回归

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| IR转换引入语义差异 | 中 | 高 | 每个Task完成后对比BTIR→LLVM IR与现有CodeGen输出 |
| C++特有语义丢失(异常/RTTI/dynamic_cast) | 中 | 高 | Phase B中C++特有Task逐一验证 |
| 测试覆盖不足 | 低 | 中 | 每个Phase新增IR层单元测试+端到端测试 |

### 4.2 性能影响

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| IR中间层增加编译时间 | 中 | 低 | BTIR设计轻量，转换开销<5%；Phase E基准验证 |
| IR→LLVM IR转换开销 | 低 | 低 | 转换器使用缓存映射，O(n)复杂度 |
| 优化效果退化 | 低 | 中 | LLVM优化在LLVM IR层面执行，不受影响 |

### 4.3 与 Phase 8 工作的兼容性

| Phase 8 工作 | 兼容方案 |
|-------------|---------|
| TargetInfo/ABIInfo | TargetLayout镜像TargetInfo布局信息；ABIInfo移入后端层 |
| Mangler substitution | IRMangler复用substitution逻辑，不依赖CodeGenModule |
| Builtins | Builtins属于前端语义，在ASTToIRConverter中处理 |
| 调用约定 | 调用约定作为IR函数属性，LLVM后端映射到LLVM CallingConv |
| 优化pipeline | LLVM优化pipeline在LLVMBackend中复用，不受影响 |
| 目标特定优化 | 目标特定优化在LLVMBackend中执行，不受影响 |

### 4.4 CGCXX 拆分风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| 拆分引入编译错误 | 低 | 低 | 先物理拆分（仅分文件），逻辑不变 |
| VTable生成迁移到后端 | 中 | 高 | 保留CGVTable.cpp在CodeGen中，Phase C迁移到LLVM后端 |
| 多重继承语义丢失 | 中 | 高 | 专项测试：多重继承、虚继承、thunk |

---

## 五、工作量估算

### 5.1 各 Phase 工作量

| Phase | 内容 | 工作量 | 风险系数 | 调整后 |
|-------|------|--------|---------|--------|
| A | IR基础设施 | 13天 | 1.0 | 13天 |
| B | 前端抽象层+C++前端适配 | 21天 | 1.3 | 27天 |
| C | 后端抽象层+LLVM后端适配 | 16天 | 1.2 | 19天 |
| D | 编译管线重构 | 6天 | 1.1 | 7天 |
| E | 验证和迁移 | 7天 | 1.2 | 8天 |
| **总计** | | **63天** | | **74天** |

### 5.2 执行顺序和并行策略

**严格串行**（因红线R3要求每阶段可编译可测试）：

```
Phase A (13天) → Phase B (27天) → Phase C (19天) → Phase D (7天) → Phase E (8天)
```

**Phase 内部并行**：
- Phase A：A.1-A.4可部分并行（类型/值/模块可同时开发）
- Phase B：B.1-B.2可与B.3并行；B.4-B.6必须串行
- Phase C：C.1可与C.2并行；C.3-C.8必须串行

### 5.3 关键里程碑

| 里程碑 | 时间 | 验收标准 |
|--------|------|---------|
| M1: IR库可用 | Phase A完成后 | libblocktype-ir编译通过，所有IR类型/指令/验证器测试通过 |
| M2: C++前端可产出IR | Phase B完成后 | CppFrontend可将C++源文件编译为BTIR |
| M3: LLVM后端可消费IR | Phase C完成后 | LLVMBackend可将BTIR编译为目标文件 |
| M4: 新管线可用 | Phase D完成后 | `--frontend=cpp --backend=llvm` 可完整编译 |
| M5: 迁移完成 | Phase E完成后 | 旧路径移除，全部功能通过新架构 |

---

## 附录 A：现有代码耦合分析

### A.1 LLVM 直接依赖统计

| 文件 | LLVM类型引用 | 耦合程度 |
|------|-------------|---------|
| CodeGenModule.h | llvm::Module, llvm::LLVMContext, llvm::DenseMap, llvm::GlobalValue | 高 |
| CodeGenFunction.h | llvm::IRBuilder, llvm::Function, llvm::Value, llvm::AllocaInst | 高 |
| CodeGenTypes.h | llvm::Type, llvm::FunctionType, llvm::StructType | 高 |
| CodeGenConstant.h | llvm::Constant, llvm::APSInt, llvm::APFloat | 高 |
| CGCXX.h | llvm::GlobalVariable, llvm::Function, llvm::Value | 高 |
| CGDebugInfo.h | llvm::DIBuilder, llvm::DIType, llvm::DISubprogram | 高 |
| Mangler.h | llvm::StringRef, llvm::SmallString (仅ADT) | 低 |
| TargetInfo.h | llvm::DataLayout, llvm::StringRef | 中 |
| ABIInfo.h | 通过CodeGenTypes间接依赖 | 中 |
| CompilerInstance.h | llvm::LLVMContext | 中 |
| Basic/LLVM.h | llvm ADT类型别名 | 低 |

### A.2 关键耦合点

1. **CodeGenModule→llvm::Module**：模块级引擎直接持有LLVM Module
2. **CodeGenFunction→llvm::IRBuilder**：函数级引擎直接使用LLVM IRBuilder
3. **CodeGenTypes→llvm::Type***：类型映射直接产出LLVM类型
4. **CompilerInstance→llvm::LLVMContext**：编译实例直接持有LLVM上下文
5. **CGDebugInfo→llvm::DIBuilder**：调试信息直接使用LLVM DIBuilder

### A.3 已有抽象可复用

1. **TargetInfo抽象基类+工厂方法**：设计模式可直接复用到FrontendBase/BackendBase
2. **ABIInfo抽象基类+工厂方法**：同上
3. **Basic/LLVM.h ADT类型别名**：IR层可复用这些ADT类型

---

## 附录 B：IR 文本格式完整示例

```
; BlockType IR (BTIR) - 文本表示
target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"

; 全局变量
@global_counter = global i32 0
@pi = constant double 3.14159265358979

; 外部函数声明
declare i32 @printf(i8*, ...)
declare void @exit(i32)

; 函数定义
define i32 @add(i32 %a, i32 %b) {
entry:
  %result = add i32 %a, %b
  ret i32 %result
}

define i32 @main() {
entry:
  %0 = call i32 @add(i32 1, i32 2)
  store i32 %0, i32* @global_counter
  ret i32 0
}

; C++ 类示例
%struct.Point = type { double, double }

define void @Point.constructor(%struct.Point* %this, double %x, double %y) {
entry:
  %x.ptr = getelementptr %struct.Point, %struct.Point* %this, i32 0, i32 0
  store double %x, double* %x.ptr
  %y.ptr = getelementptr %struct.Point, %struct.Point* %this, i32 0, i32 1
  store double %y, double* %y.ptr
  ret void
}
```
