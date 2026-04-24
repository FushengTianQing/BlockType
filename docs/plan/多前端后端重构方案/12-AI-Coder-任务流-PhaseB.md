# AI Coder 可执行任务流 — Phase B：前端适配

> 本文档是 AI coder 可直接执行的改造任务流。每个 Task 自包含所有必要信息：
> 接口签名、类型定义、参数约束、验收命令。AI coder 无需查阅其他文档即可编码。

---

## 执行规则

1. **严格按 Task 编号顺序执行**，每个 Task 完成并通过验收后再开始下一个
2. **接口签名不可修改**——本文档中的 class/struct/enum/函数签名是硬约束
3. **验收标准必须全部通过**——验收代码是可执行的断言
4. **命名空间**：前端层代码在 `namespace blocktype::frontend` 中
5. **头文件路径**：`include/blocktype/Frontend/`，源文件路径：`src/Frontend/`
6. **依赖限制**：`libblocktype-frontend` 依赖 `blocktype-sema` + `blocktype-ir` + `LLVM_ADT_LIBS`，不链接 `LLVM_IR_LIBS`
7. **Git 提交与推送**：每个 Task 完成并通过验收后，**必须立即**执行以下操作：
   ```bash
   git add -A
   git commit -m "feat(<phase>): 完成 <Task编号> — <Task标题>"
   git push origin HEAD
   ```
   - commit message 格式：`feat(B): 完成 B.1 — FrontendBase 抽象基类 + FrontendOptions`
   - **不得跳过此步骤**——确保每个 Task 的产出都有远端备份，防止工作丢失
   - 如果 push 失败，先 `git pull --rebase origin HEAD` 再重试

---

## 接口关联关系

### 持有关系

```
FrontendBase ──ref──→ FrontendOptions（配置，值语义）
FrontendBase ──ref──→ DiagnosticsEngine&（诊断，引用不拥有）

FrontendRegistry ──owns──→ StringMap<FrontendFactory>（工厂函数映射）
FrontendRegistry ──owns──→ StringMap<string> ExtToName（扩展名→前端名映射）

CppFrontend ──owns──→ unique_ptr<SourceManager> SM
CppFrontend ──owns──→ unique_ptr<Preprocessor> PP
CppFrontend ──owns──→ unique_ptr<ASTContext> ASTCtx
CppFrontend ──owns──→ unique_ptr<Sema> SemaPtr
CppFrontend ──owns──→ unique_ptr<Parser> ParserPtr
CppFrontend ──owns──→ unique_ptr<ASTToIRConverter> IRConverter
CppFrontend ──ref──→ FrontendOptions（继承自FrontendBase）
CppFrontend ──ref──→ DiagnosticsEngine&（继承自FrontendBase）

ASTToIRConverter ──ref──→ ir::IRContext&（IR内存管理，引用不拥有）
ASTToIRConverter ──ref──→ ir::IRTypeContext&（类型上下文，引用不拥有）
ASTToIRConverter ──ref──→ const TargetLayout&（布局信息，引用不拥有）
ASTToIRConverter ──ref──→ DiagnosticsEngine&（诊断，引用不拥有）
ASTToIRConverter ──owns──→ unique_ptr<ir::IRModule> TheModule
ASTToIRConverter ──owns──→ unique_ptr<ir::IRBuilder> Builder
ASTToIRConverter ──owns──→ unique_ptr<IRTypeMapper> TypeMapper
ASTToIRConverter ──owns──→ unique_ptr<IREmitExpr> ExprEmitter
ASTToIRConverter ──owns──→ unique_ptr<IREmitStmt> StmtEmitter
ASTToIRConverter ──owns──→ unique_ptr<IREmitCXX> CXXEmitter
ASTToIRConverter ──owns──→ unique_ptr<IRConstantEmitter> ConstEmitter
ASTToIRConverter ──owns──→ DenseMap<const Decl*, ir::IRValue*> DeclValues
ASTToIRConverter ──owns──→ DenseMap<const VarDecl*, ir::IRGlobalVariable*> GlobalVars
ASTToIRConverter ──owns──→ DenseMap<const FunctionDecl*, ir::IRFunction*> Functions
ASTToIRConverter ──owns──→ DenseMap<const VarDecl*, ir::IRValue*> LocalDecls

IRTypeMapper ──ref──→ ir::IRTypeContext&（类型工厂）
IRTypeMapper ──ref──→ const TargetLayout&（布局信息）
IRTypeMapper ──owns──→ DenseMap<QualType, ir::IRType*> Cache

IREmitExpr ──ref──→ ASTToIRConverter&（父转换器）
IREmitExpr ──ref──→ ir::IRBuilder&（IR构建器）
IREmitStmt ──ref──→ ASTToIRConverter&（父转换器）
IREmitStmt ──ref──→ ir::IRBuilder&（IR构建器）
IREmitCXX  ──ref──→ ASTToIRConverter&（父转换器）
IREmitCXX  ──ref──→ ir::IRBuilder&（IR构建器）
IRConstantEmitter ──ref──→ ir::IRTypeContext&（类型工厂）

IRMangler ──ref──→ ASTContext&（AST上下文）
IRMangler ──ref──→ const TargetLayout&（布局信息）
```

### 调用关系

```
CppFrontend::compile(Filename, TypeCtx, Layout)
  ──calls──→ SourceManager/Preprocessor/Parser/Sema 初始化
  ──calls──→ ASTToIRConverter::convert(TU)
  ──calls──→ contract::verifyAllContracts(IRModule)（Debug构建）
  ──returns──→ IRConversionResult

ASTToIRConverter::convert(TU)
  ──calls──→ IRTypeMapper::mapType() 对每个 QualType
  ──calls──→ emitFunction() 对每个 FunctionDecl
  ──calls──→ emitGlobalVar() 对每个 VarDecl（hasGlobalStorage）
  ──calls──→ IREmitCXX::EmitVTable() 对每个含虚函数的 CXXRecordDecl
  ──calls──→ IREmitCXX::EmitRTTI() 对每个含 RTTI 需求的 CXXRecordDecl
  ──returns──→ IRConversionResult（成功或isInvalid()）

ASTToIRConverter::emitFunction(FD)
  ──calls──→ IRTypeMapper::mapType(FD->getType())
  ──calls──→ IRModule::getOrInsertFunction(Name, FTy)
  ──calls──→ IRFunction::addBasicBlock("entry")
  ──calls──→ IREmitStmt::EmitCompoundStmt(Body) 对函数体
  ──calls──→ emitErrorPlaceholder() 失败时

ASTToIRConverter::emitGlobalVar(VD)
  ──calls──→ IRTypeMapper::mapType(VD->getType())
  ──calls──→ IRModule::getOrInsertGlobal(Name, Ty)
  ──calls──→ IRConstantEmitter::EmitConst*() 对初始值
  ──calls──→ IRGlobalVariable::setInitializer() 设置初始值

IRTypeMapper::mapType(QualType)
  ──calls──→ Cache.lookup(T) 先查缓存
  ──calls──→ IRTypeContext::getInt32Ty() 等类型工厂（按映射规则表）
  ──calls──→ IRTypeContext::getPointerType() 对指针/引用类型
  ──calls──→ IRTypeContext::getStructType() 对 RecordType
  ──calls──→ IRTypeContext::getFunctionType() 对 FunctionProtoType
  ──calls──→ mapType(Pointee) 递归映射子类型
  ──calls──→ emitErrorType() 失败时返回 IROpaqueType("error")

IREmitExpr::EmitBinaryExpr(BO)
  ──calls──→ Emit*Expr(BO->getLHS()) 递归处理左操作数
  ──calls──→ Emit*Expr(BO->getRHS()) 递归处理右操作数
  ──calls──→ IRBuilder::createAdd/Sub/Mul/... 根据操作码
  ──calls──→ emitErrorPlaceholder(T) 失败时

IREmitExpr::EmitCallExpr(CE)
  ──calls──→ emitFunction(CE->getCalleeDecl()) 确保被调函数已生成
  ──calls──→ IRBuilder::createCall(IRFn, Args)
  ──calls──→ emitErrorPlaceholder(T) 失败时

IREmitStmt::EmitIfStmt(IS)
  ──calls──→ IREmitExpr::Emit*Expr(IS->getCond()) 条件表达式
  ──calls──→ IRBuilder::createCondBr(Cond, ThenBB, ElseBB)
  ──calls──→ Emit*Stmt(IS->getThen()) Then 分支
  ──calls──→ Emit*Stmt(IS->getElse()) Else 分支（如有）

IREmitCXX::EmitVTable(RD)
  ──calls──→ IRMangler::mangleVTable(RD) 名称修饰
  ──calls──→ IRModule::getOrInsertGlobal(VTableName, IROpaqueType) 占位全局变量
  ──注意──→ VTable 具体布局在 LLVM 后端填充（Phase C Task C.8）

IREmitCXX::EmitRTTI(RD)
  ──calls──→ IRMangler::mangleTypeInfo(RD) 名称修饰
  ──calls──→ IRModule::getOrInsertGlobal(RTTIName, IROpaqueType) 占位全局变量
  ──注意──→ RTTI 具体布局在 LLVM 后端填充（Phase C Task C.8）

FrontendRegistry::autoSelect(Filename, Opts, Diags)
  ──calls──→ llvm::sys::path::extension(Filename) 提取扩展名
  ──calls──→ ExtToName.lookup(Ext) 查找扩展名映射
  ──calls──→ create(Name, Opts, Diags) 创建前端实例
```

### 生命周期约束

```
CompilerInstance 生命周期 ≥ CppFrontend 生命周期 ≥ ASTToIRConverter 生命周期
ASTToIRConverter 生命周期 ≥ IRModule 生命周期（convert() 返回的 IRModule 由调用者接管）
IRTypeMapper 生命周期 = ASTToIRConverter 生命周期
IRBuilder 生命周期 = ASTToIRConverter 生命周期
FrontendRegistry 全局单例（静态初始化，程序退出时销毁）
```

### 内存管理规则

| 对象 | 分配方式 | 拥有者 | 释放方式 |
|------|---------|--------|---------|
| FrontendBase 子类 | unique_ptr | CompilerInstance | unique_ptr 析构 |
| ASTToIRConverter | unique_ptr | CppFrontend | unique_ptr 析构 |
| IRModule | unique_ptr | convert() 返回值→CompilerInstance | unique_ptr 析构 |
| IRTypeMapper | unique_ptr | ASTToIRConverter | unique_ptr 析构 |
| DenseMap<Decl*, IRValue*> | ASTToIRConverter 直接持有 | ASTToIRConverter | 随 ASTToIRConverter 析构释放 |

---

## Task B.1：FrontendBase 抽象基类 + FrontendOptions

### 依赖

- Phase A 全部完成（IRType/IRModule/IRBuilder/IRVerifier/IRSerializer）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/FrontendBase.h` |
| 新增 | `include/blocktype/Frontend/FrontendOptions.h` |
| 新增 | `src/Frontend/FrontendBase.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

struct FrontendOptions {
  std::string InputFile;
  std::string OutputFile;
  std::string TargetTriple;
  std::string Language;
  bool EmitIR = false;
  bool EmitIRBitcode = false;
  bool BTIROnly = false;
  bool VerifyIR = false;
  unsigned OptimizationLevel = 0;
  SmallVector<std::string, 4> IncludePaths;
  SmallVector<std::string, 4> MacroDefinitions;
};

class FrontendBase {
protected:
  FrontendOptions Opts;
  DiagnosticsEngine& Diags;
public:
  FrontendBase(const FrontendOptions& Opts, DiagnosticsEngine& Diags);
  virtual ~FrontendBase() = default;
  virtual StringRef getName() const = 0;
  virtual StringRef getLanguage() const = 0;
  virtual unique_ptr<ir::IRModule> compile(
    StringRef Filename, ir::IRTypeContext& TypeCtx, const ir::TargetLayout& Layout) = 0;
  virtual bool canHandle(StringRef Filename) const = 0;
  const FrontendOptions& getOptions() const { return Opts; }
  DiagnosticsEngine& getDiagnostics() const { return Diags; }
};

using FrontendFactory = std::function<unique_ptr<FrontendBase>(
  const FrontendOptions&, DiagnosticsEngine&)>;

}
```

### 实现约束

1. FrontendBase 不可拷贝
2. compile() 返回的 IRModule 所有权转移给调用者
3. compile() 失败时返回 nullptr
4. FrontendOptions 的 TargetTriple 必须非空（否则 compile 触发 assert）

### 验收标准

```cpp
// V1: FrontendBase 不可实例化（纯虚类）
// FrontendBase F(Opts, Diags); → 编译错误

// V2: 子类可实例化
class TestFrontend : public FrontendBase {
public:
  using FrontendBase::FrontendBase;
  StringRef getName() const override { return "test"; }
  StringRef getLanguage() const override { return "test"; }
  unique_ptr<ir::IRModule> compile(StringRef, ir::IRTypeContext&, const ir::TargetLayout&) override { return nullptr; }
  bool canHandle(StringRef) const override { return true; }
};
TestFrontend TF(Opts, Diags);
assert(TF.getName() == "test");

// V3: FrontendOptions 默认值
FrontendOptions Opts;
assert(Opts.EmitIR == false);
assert(Opts.BTIROnly == false);
assert(Opts.OptimizationLevel == 0);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.1：FrontendBase 抽象基类 + FrontendOptions" && git push origin HEAD
> ```

---

## Task B.2：FrontendRegistry + 自动选择

### 依赖

- B.1（FrontendBase）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/FrontendRegistry.h` |
| 新增 | `src/Frontend/FrontendRegistry.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class FrontendRegistry {
  llvm::StringMap<FrontendFactory> Registry;
  llvm::StringMap<std::string> ExtToName;
  FrontendRegistry() = default;
public:
  static FrontendRegistry& instance();
  void registerFrontend(StringRef Name, FrontendFactory Factory);
  unique_ptr<FrontendBase> create(StringRef Name, const FrontendOptions& Opts, DiagnosticsEngine& Diags);
  unique_ptr<FrontendBase> autoSelect(StringRef Filename, const FrontendOptions& Opts, DiagnosticsEngine& Diags);
  void addExtensionMapping(StringRef Ext, StringRef FrontendName);
  bool hasFrontend(StringRef Name) const;
  SmallVector<StringRef, 4> getRegisteredNames() const;
};

}
```

### 扩展名→前端映射规则

| 扩展名 | 前端名 |
|--------|--------|
| `.cpp` / `.cc` / `.cxx` / `.C` | `"cpp"` |
| `.c` | `"cpp"`（C 作为 C++ 子集处理） |
| `.h` / `.hpp` / `.hxx` | `"cpp"` |
| `.bc` / `.btir` | 无前端（直接加载 IR） |

### 实现约束

1. FrontendRegistry 是全局单例（`instance()` 返回静态局部变量引用）
2. `registerFrontend` 重复注册同名前端触发 assert
3. `create` 找不到前端返回 nullptr
4. `autoSelect` 找不到匹配扩展名返回 nullptr
5. 线程安全：注册阶段（编译前）无需加锁；查找阶段假设注册已完成

### 验收标准

```cpp
// V1: 注册和创建
auto& Reg = FrontendRegistry::instance();
Reg.registerFrontend("cpp", createCppFrontend);
auto FE = Reg.create("cpp", Opts, Diags);
assert(FE != nullptr);
assert(FE->getName() == "cpp");

// V2: 自动选择
Reg.addExtensionMapping(".cpp", "cpp");
auto FE2 = Reg.autoSelect("test.cpp", Opts, Diags);
assert(FE2 != nullptr);

// V3: 未知前端返回 nullptr
auto FE3 = Reg.create("unknown", Opts, Diags);
assert(FE3 == nullptr);

// V4: 重复注册触发 assert
// Reg.registerFrontend("cpp", createCppFrontend); → assert 失败
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.2：FrontendRegistry + 自动选择" && git push origin HEAD
> ```

---

## Task B.3：IRTypeMapper（QualType → IRType 映射）

### 依赖

- Phase A（IRType 体系、IRTypeContext）
- B.1（FrontendBase，IRTypeMapper 被 ASTToIRConverter 持有）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/IRTypeMapper.h` |
| 新增 | `src/Frontend/IRTypeMapper.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class IRTypeMapper {
  ir::IRTypeContext& TypeCtx;
  const ir::TargetLayout& Layout;
  DenseMap<QualType, ir::IRType*> Cache;
public:
  explicit IRTypeMapper(ir::IRTypeContext& C, const ir::TargetLayout& L);
  ir::IRType* mapType(QualType T);
private:
  ir::IRType* mapBuiltinType(const BuiltinType* BT);
  ir::IRType* mapPointerType(const PointerType* PT);
  ir::IRType* mapReferenceType(const ReferenceType* RT);
  ir::IRType* mapArrayType(const ArrayType* AT);
  ir::IRType* mapRecordType(const RecordType* RT);
  ir::IRType* mapFunctionType(const FunctionProtoType* FT);
  ir::IRType* mapEnumType(const EnumType* ET);
  ir::IRType* mapTypedefType(const TypedefType* TT);
  ir::IRType* mapTemplateSpecializationType(const TemplateSpecializationType* TST);
  ir::IRType* mapVectorType(const VectorType* VT);
  ir::IRType* mapComplexType(const ComplexType* CT);
  ir::IRType* mapBlockType(const BlockType* BT);
  ir::IRType* emitErrorType();
};

}
```

### QualType → IRType 映射规则表

| QualType Kind | IRType | 映射规则 |
|---------------|--------|---------|
| BuiltinType::Void | IRVoidType | TypeCtx.getVoidType() |
| BuiltinType::Bool | IRIntegerType(1) | TypeCtx.getBoolType() |
| BuiltinType::Char_U/S | IRIntegerType(8) | TypeCtx.getInt8Ty() |
| BuiltinType::UChar/SChar | IRIntegerType(8) | TypeCtx.getInt8Ty() |
| BuiltinType::Short/UShort | IRIntegerType(16) | TypeCtx.getIntType(16) |
| BuiltinType::Int/UInt | IRIntegerType(32) | TypeCtx.getInt32Ty() |
| BuiltinType::Long/ULong | IRIntegerType(Layout.LongSize*8) | 平台相关：x86_64=64 |
| BuiltinType::LongLong/ULongLong | IRIntegerType(64) | TypeCtx.getInt64Ty() |
| BuiltinType::Float | IRFloatType(32) | TypeCtx.getFloatTy() |
| BuiltinType::Double | IRFloatType(64) | TypeCtx.getDoubleTy() |
| BuiltinType::LongDouble | IRFloatType(Layout.LongDoubleSize*8) | 平台相关：x86_64=80 |
| PointerType | IRPointerType(Pointee) | mapType(Pointee) → getPointerType() |
| ReferenceType | IRPointerType(Pointee) | 引用→指针（语义等价） |
| ArrayType(Constant) | IRArrayType(Element, N) | mapType(Element) → getArrayType() |
| RecordType(Struct/Class) | IRStructType(Name, Fields) | 逐字段mapType → getStructType() |
| FunctionProtoType | IRFunctionType(Ret, Params) | mapType(Ret) + mapTypes(Params) |
| EnumType | IRIntegerType(BitWidth) | BitWidth = Layout.getTypeSizeInBits(EnumDecl) |
| TypedefType | 递归mapType(UnderlyingType) | 透明映射 |
| TemplateSpecializationType | 递归mapType(Desugar) | 透明映射 |
| DependentType | IROpaqueType(Name) | 未决类型→不透明类型占位 |
| VectorType | IRVectorType(N, Element) | mapType(Element) → getVectorType() |
| ComplexType | IRStructType("_complex", [T,T]) | 复数→结构体 |
| BlockType | IRPointerType(IRFunctionType(...)) | Block→函数指针 |

### 实现约束

1. mapType 必须缓存（相同 QualType 返回相同 IRType*）
2. 失败时返回 IROpaqueType("error") 并发出 diag::err_ir_type_mapping_failed
3. 递归类型（如 struct S { S* next; }）必须处理：先用 IROpaqueType 占位，再 setStructBody
4. Long/ULong 和 LongDouble 的位宽依赖 TargetLayout

### 验收标准

```cpp
// V1: 基础类型映射
IRTypeContext Ctx;
auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
IRTypeMapper Mapper(Ctx, *Layout);
auto* Int32Ty = Mapper.mapType(Context.IntTy);
assert(isa<ir::IRIntegerType>(Int32Ty));
assert(cast<ir::IRIntegerType>(Int32Ty)->getBitWidth() == 32);

// V2: 指针类型映射
auto* PtrIntTy = Mapper.mapType(Context.getPointerType(Context.IntTy));
assert(isa<ir::IRPointerType>(PtrIntTy));

// V3: 缓存一致性
auto* Int32Ty2 = Mapper.mapType(Context.IntTy);
assert(Int32Ty == Int32Ty2);  // 同一指针

// V4: 递归类型处理
// struct Node { Node* next; int val; };
// mapType → IRStructType("Node", [IRPointerType, IRIntegerType(32)])
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.3：IRTypeMapper（QualType → IRType 映射）" && git push origin HEAD
> ```

---

## Task B.4：ASTToIRConverter 主框架

### 依赖

- B.3（IRTypeMapper）
- Phase A（IRModule/IRBuilder/IRVerifier）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/ASTToIRConverter.h` |
| 新增 | `src/Frontend/ASTToIRConverter.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class ASTToIRConverter {
  ir::IRContext& IRCtx;
  ir::IRTypeContext& TypeCtx;
  const ir::TargetLayout& Layout;
  DiagnosticsEngine& Diags;
  unique_ptr<ir::IRModule> TheModule;
  unique_ptr<ir::IRBuilder> Builder;
  unique_ptr<IRTypeMapper> TypeMapper;
  DenseMap<const Decl*, ir::IRValue*> DeclValues;
  DenseMap<const VarDecl*, ir::IRGlobalVariable*> GlobalVars;
  DenseMap<const FunctionDecl*, ir::IRFunction*> Functions;
  DenseMap<const VarDecl*, ir::IRValue*> LocalDecls;
  unique_ptr<IREmitExpr> ExprEmitter;
  unique_ptr<IREmitStmt> StmtEmitter;
  unique_ptr<IREmitCXX> CXXEmitter;
  unique_ptr<IRConstantEmitter> ConstEmitter;
public:
  ASTToIRConverter(ir::IRContext& IRCtx,
                   ir::IRTypeContext& Ctx,
                   const ir::TargetLayout& Layout,
                   DiagnosticsEngine& Diags);
  ir::IRConversionResult convert(TranslationUnitDecl* TU);
  ir::IRType* mapType(QualType T);
  ir::IRFunction* emitFunction(FunctionDecl* FD);
  ir::IRGlobalVariable* emitGlobalVar(VarDecl* VD);
  void convertCXXConstructor(CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);
  void convertCXXDestructor(CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);
  ir::IRValue* convertCXXConstructExpr(CXXConstructExpr* CCE);
  ir::IRValue* convertVirtualCall(CXXMemberCallExpr* MCE);
  ir::IRValue* emitErrorPlaceholder(ir::IRType* T);
  ir::IRType* emitErrorType();
  ir::IRBuilder& getBuilder() { return *Builder; }
  ir::IRModule* getModule() const { return TheModule.get(); }
  IRTypeMapper& getTypeMapper() { return *TypeMapper; }
  ir::IRValue* getDeclValue(const Decl* D) const;
  void setDeclValue(const Decl* D, ir::IRValue* V);
  ir::IRFunction* getFunction(const FunctionDecl* FD) const;
  ir::IRGlobalVariable* getGlobalVar(const VarDecl* VD) const;
};

}
```

### IRConversionResult 定义

```cpp
namespace blocktype::ir {

class IRConversionResult {
  unique_ptr<IRModule> Module;
  unsigned NumErrors = 0;
  bool Invalid = false;
public:
  IRConversionResult() = default;
  explicit IRConversionResult(unique_ptr<IRModule> M) : Module(std::move(M)) {}
  static IRConversionResult getInvalid() {
    IRConversionResult R;
    R.Invalid = true;
    return R;
  }
  bool isInvalid() const { return Invalid; }
  bool isUsable() const { return !Invalid && Module != nullptr; }
  unique_ptr<IRModule> takeModule() { return std::move(Module); }
  IRModule* getModule() const { return Module.get(); }
  unsigned getNumErrors() const { return NumErrors; }
  void addError() { ++NumErrors; Invalid = true; }
};

}
```

### convert() 精确语义

1. 创建 IRModule（名称=源文件名，TargetTriple=Layout.getTriple()）
2. 遍历 TU->decls()，对每个 Decl：
   - FunctionDecl → emitFunction()
   - VarDecl（hasGlobalStorage）→ emitGlobalVar()
   - CXXRecordDecl（hasDefinition && hasVTable）→ IREmitCXX::EmitVTable()
3. 失败时采用占位+继续策略（见错误恢复表）
4. 返回 IRConversionResult（成功时 isUsable()==true）

### 错误恢复策略表

| 错误场景 | 恢复策略 | 占位值 | DiagID |
|---------|---------|--------|--------|
| QualType 无法映射 | 发出 Diag，用 IROpaqueType 占位 | IROpaqueType("error") | diag::err_ir_type_mapping_failed |
| 表达式转换失败 | 发出 Diag，用 IRConstantUndef 占位 | IRConstantUndef::get(T) | diag::err_ir_expr_conversion_failed |
| 语句转换失败 | 发出 Diag，跳过该语句 | — | diag::err_ir_stmt_conversion_failed |
| 函数体转换失败 | 发出 Diag，生成空函数体 | createRetVoid() | diag::err_ir_function_conversion_failed |
| 全局变量初始化失败 | 发出 Diag，用 IRConstantUndef 作为初始值 | IRConstantUndef::get(T) | diag::err_ir_globalvar_init_failed |
| C++特有语义转换失败 | 发出 Diag，用 IRConstantUndef 占位 | IRConstantUndef::get(T) | diag::err_ir_cxx_semantic_failed |
| VTable生成失败 | 发出 Diag，用空全局变量占位 | IRGlobalVariable(Name, IROpaqueType) | diag::err_ir_vtable_generation_failed |
| RTTI生成失败 | 发出 Diag，用空全局变量占位 | IRGlobalVariable(Name, IROpaqueType) | diag::err_ir_rtti_generation_failed |

### 实现约束

1. IRCtx 必须已初始化，Layout 必须与目标平台一致
2. Diags 必须已绑定 SourceManager
3. convert() 后 TheModule 所有权转移给 IRConversionResult
4. 所有 IR 节点通过 IRContext::create<T>() 分配

### 验收标准

```cpp
// V1: 空 TU 转换
auto Result = Converter.convert(EmptyTU);
assert(Result.isUsable());
assert(Result.getModule()->getNumFunctions() == 0);

// V2: 含函数的 TU 转换
auto Result2 = Converter.convert(TUWithMain);
assert(Result2.isUsable());
auto* MainFn = Result2.getModule()->getFunction("main");
assert(MainFn != nullptr);

// V3: 错误恢复
auto Result3 = Converter.convert(TUWithError);
// 不崩溃，错误被记录
assert(Result3.getNumErrors() > 0 || Result3.isUsable());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.4：ASTToIRConverter 主框架" && git push origin HEAD
> ```

---

## Task B.5：IREmitExpr（表达式发射器）

### 依赖

- B.4（ASTToIRConverter）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/IREmitExpr.h` |
| 新增 | `src/Frontend/IREmitExpr.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class IREmitExpr {
  ASTToIRConverter& Converter;
  ir::IRBuilder& Builder;
public:
  explicit IREmitExpr(ASTToIRConverter& C, ir::IRBuilder& B);
  ir::IRValue* EmitBinaryExpr(const BinaryOperator* BO);
  ir::IRValue* EmitUnaryExpr(const UnaryOperator* UO);
  ir::IRValue* EmitCallExpr(const CallExpr* CE);
  ir::IRValue* EmitMemberExpr(const MemberExpr* ME);
  ir::IRValue* EmitDeclRefExpr(const DeclRefExpr* DRE);
  ir::IRValue* EmitCastExpr(const CastExpr* CE);
  ir::IRValue* EmitCXXConstructExpr(const CXXConstructExpr* CCE);
  ir::IRValue* EmitCXXMemberCallExpr(const CXXMemberCallExpr* MCE);
  ir::IRValue* EmitCXXNewExpr(const CXXNewExpr* NE);
  ir::IRValue* EmitCXXDeleteExpr(const CXXDeleteExpr* DE);
  ir::IRValue* EmitCXXThisExpr(const CXXThisExpr* TE);
  ir::IRValue* EmitConditionalOperator(const ConditionalOperator* CO);
  ir::IRValue* EmitInitListExpr(const InitListExpr* ILE);
  ir::IRValue* EmitStringLiteral(const StringLiteral* SL);
  ir::IRValue* EmitIntegerLiteral(const IntegerLiteral* IL);
  ir::IRValue* EmitFloatingLiteral(const FloatingLiteral* FL);
  ir::IRValue* EmitCharacterLiteral(const CharacterLiteral* CL);
  ir::IRValue* EmitBoolLiteral(const CXXBoolLiteralExpr* BLE);
};

}
```

### BinaryOperator → IR 指令映射表

| BinaryOperator::Opcode | IR Opcode | 备注 |
|------------------------|-----------|------|
| BO_Add | Opcode::Add | 整数加法 |
| BO_Sub | Opcode::Sub | 整数减法 |
| BO_Mul | Opcode::Mul | 整数乘法 |
| BO_Div | Opcode::SDiv/UDiv | 有符号/无符号取决于类型 |
| BO_Rem | Opcode::SRem/URem | 同上 |
| BO_Shl | Opcode::Shl | 左移 |
| BO_Shr | Opcode::AShr/LShr | 算术/逻辑右移取决于类型 |
| BO_And | Opcode::And | 按位与 |
| BO_Or | Opcode::Or | 按位或 |
| BO_Xor | Opcode::Xor | 按位异或 |
| BO_LT/BO_GT/BO_LE/BO_GE/BO_EQ/BO_NE | Opcode::ICmp | 比较指令 |
| BO_LAnd/BO_LOr | Opcode::ICmp + Select | 逻辑与/或（短路求值） |
| BO_Assign | — | 赋值是 Store 指令 |
| BO_Comma | — | 逗号表达式，返回右操作数 |

### 实现约束

1. 所有 Emit*Expr 方法失败时返回 emitErrorPlaceholder(T)
2. EmitCallExpr 必须先确保被调函数已通过 emitFunction() 生成
3. EmitDeclRefExpr 通过 DeclValues 查找值映射
4. 短路求值（&&/||）必须生成 CondBr 分支

### 验收标准

```cpp
// V1: 整数加法
// int x = a + b; → %sum = add i32 %a, %b
auto* Result = ExprEmitter.EmitBinaryExpr(AddBO);
assert(Result != nullptr);

// V2: 函数调用
// foo(1, 2) → call @foo(i32 1, i32 2)
auto* CallResult = ExprEmitter.EmitCallExpr(CallCE);
assert(CallResult != nullptr);

// V3: 成员访问
// obj.field → GEP + load
auto* MemberResult = ExprEmitter.EmitMemberExpr(MemberME);
assert(MemberResult != nullptr);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.5：IREmitExpr（表达式发射器）" && git push origin HEAD
> ```

---

## Task B.6：IREmitStmt（语句发射器）

### 依赖

- B.5（IREmitExpr）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/IREmitStmt.h` |
| 新增 | `src/Frontend/IREmitStmt.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class IREmitStmt {
  ASTToIRConverter& Converter;
  ir::IRBuilder& Builder;
public:
  explicit IREmitStmt(ASTToIRConverter& C, ir::IRBuilder& B);
  void EmitIfStmt(const IfStmt* IS);
  void EmitForStmt(const ForStmt* FS);
  void EmitWhileStmt(const WhileStmt* WS);
  void EmitDoStmt(const DoStmt* DS);
  void EmitReturnStmt(const ReturnStmt* RS);
  void EmitSwitchStmt(const SwitchStmt* SS);
  void EmitCompoundStmt(const CompoundStmt* CS);
  void EmitDeclStmt(const DeclStmt* DS);
  void EmitNullStmt(const NullStmt* NS);
  void EmitGotoStmt(const GotoStmt* GS);
  void EmitLabelStmt(const LabelStmt* LS);
  void EmitBreakStmt(const BreakStmt* BS);
  void EmitContinueStmt(const ContinueStmt* CS);
};

}
```

### 控制流 IR 生成模式

**IfStmt**:
```
  %cond = <evaluate condition>
  condBr %cond, %then, %else
then:
  <then body>
  br %end
else:
  <else body>  (if exists)
  br %end
end:
```

**ForStmt**:
```
  <init>
  br %cond
cond:
  %c = <evaluate condition>
  condBr %c, %body, %end
body:
  <body>
  br %inc
inc:
  <increment>
  br %cond
end:
```

### 实现约束

1. 所有 Emit*Stmt 方法失败时跳过该语句并发出 diag
2. 每个 BB 必须恰好一个终结指令
3. Break/Continue 需要维护 break/continue 目标栈

### 验收标准

```cpp
// V1: if 语句生成正确的 BB 结构
StmtEmitter.EmitIfStmt(IfS);
// 验证：entry BB → condBr → then BB + else BB → end BB
// 每个 BB 恰好一个终结指令

// V2: for 循环生成正确的 BB 结构
StmtEmitter.EmitForStmt(ForS);
// 验证：init → cond → body → inc → cond → end

// V3: return 语句
StmtEmitter.EmitReturnStmt(ReturnS);
// 验证：当前 BB 以 ret 或 retVoid 终结
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.6：IREmitStmt（语句发射器）" && git push origin HEAD
> ```

---

## Task B.7：IREmitCXX（C++ 特有发射器，4 子文件）

### 依赖

- B.5（IREmitExpr）
- B.6（IREmitStmt）
- B.8（IRMangler，VTable/RTTI 需要名称修饰）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/IREmitCXX.h` |
| 新增 | `src/Frontend/IREmitCXXLayout.cpp` |
| 新增 | `src/Frontend/IREmitCXXCtorDtor.cpp` |
| 新增 | `src/Frontend/IREmitCXXVTable.cpp` |
| 新增 | `src/Frontend/IREmitCXXInherit.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class IREmitCXX {
  ASTToIRConverter& Converter;
  ir::IRBuilder& Builder;
public:
  explicit IREmitCXX(ASTToIRConverter& C, ir::IRBuilder& B);
  void EmitCXXConstructor(CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);
  void EmitCXXDestructor(CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);
  void EmitVTable(const CXXRecordDecl* RD);
  void EmitRTTI(const CXXRecordDecl* RD);
  void EmitThunk(const CXXMethodDecl* MD);
};

class IREmitCXXLayout {
  ASTToIRConverter& Converter;
public:
  explicit IREmitCXXLayout(ASTToIRConverter& C);
  ir::IRStructType* ComputeClassLayout(const CXXRecordDecl* RD);
  uint64_t GetFieldOffset(const FieldDecl* FD);
  uint64_t GetClassSize(const CXXRecordDecl* RD);
  uint64_t GetBaseOffset(const CXXRecordDecl* Derived, const CXXBaseSpecifier* Base);
  uint64_t GetVirtualBaseOffset(const CXXRecordDecl* Derived, const CXXRecordDecl* VBase);
};

class IREmitCXXCtorDtor {
  ASTToIRConverter& Converter;
  ir::IRBuilder& Builder;
public:
  explicit IREmitCXXCtorDtor(ASTToIRConverter& C, ir::IRBuilder& B);
  void EmitConstructor(const CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);
  void EmitBaseInitializer(const CXXCtorInitializer* Init);
  void EmitMemberInitializer(const CXXCtorInitializer* Init);
  void EmitDelegatingConstructor(const CXXCtorInitializer* Init);
  void EmitDestructor(const CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);
  void EmitDestructorBody(const CXXDestructorDecl* Dtor);
  void EmitDestructorCall(const CXXDestructorDecl* Dtor, ir::IRValue* Object);
};

class IREmitCXXVTable {
  ASTToIRConverter& Converter;
  ir::IRBuilder& Builder;
public:
  explicit IREmitCXXVTable(ASTToIRConverter& C, ir::IRBuilder& B);
  void EmitVTable(const CXXRecordDecl* RD);
  ir::IRType* GetVTableType(const CXXRecordDecl* RD);
  uint64_t GetVTableIndex(const CXXMethodDecl* MD);
  void InitializeVTablePtr(ir::IRValue* Object, const CXXRecordDecl* RD);
  void EmitVTableInitialization(const CXXRecordDecl* RD);
  void EmitRTTI(const CXXRecordDecl* RD);
  void EmitCatchTypeInfo(const CXXCatchStmt* CS);
};

class IREmitCXXInherit {
  ASTToIRConverter& Converter;
  ir::IRBuilder& Builder;
public:
  explicit IREmitCXXInherit(ASTToIRConverter& C, ir::IRBuilder& B);
  ir::IRValue* EmitCastToBase(ir::IRValue* Object, const CXXBaseSpecifier* Base);
  ir::IRValue* EmitCastToDerived(ir::IRValue* Object, const CXXRecordDecl* Derived);
  uint64_t EmitBaseOffset(const CXXRecordDecl* Derived, const CXXBaseSpecifier* Base);
  ir::IRValue* EmitDynamicCast(ir::IRValue* Object, const CXXDynamicCastExpr* DCE);
  void EmitThunk(const CXXMethodDecl* MD);
  void EmitVTT(const CXXRecordDecl* RD);
};

}
```

### CGCXX 拆分策略（OPT-02 修订）

原方案 7 子文件 → 4 子文件（实际 CGCXX.cpp 仅 2,229 行）：

| 子文件 | 行数估计 | 包含的函数 |
|--------|---------|-----------|
| IREmitCXXLayout.cpp | ~400 | ComputeClassLayout, GetFieldOffset, GetClassSize, GetBaseOffset, GetVirtualBaseOffset |
| IREmitCXXCtorDtor.cpp | ~600 | EmitConstructor, EmitBaseInitializer, EmitMemberInitializer, EmitDelegatingConstructor, EmitDestructor, EmitDestructorBody, EmitDestructorCall |
| IREmitCXXVTable.cpp | ~700 | EmitVTable, GetVTableType, GetVTableIndex, InitializeVTablePtr, EmitVTableInitialization, EmitRTTI, EmitCatchTypeInfo |
| IREmitCXXInherit.cpp | ~500 | EmitCastToBase, EmitCastToDerived, EmitBaseOffset, EmitDynamicCast, EmitThunk, EmitVTT |

### VTable/RTTI 归属

- **布局计算** → 移入 IREmitCXXLayout，输出 IR 结构体类型
- **构造/析构** → 移入 IREmitCXXCtorDtor，输出 IR 指令序列
- **VTable/RTTI** → IR 层仅创建占位全局变量（IROpaqueType），LLVM 后端填充具体布局（Phase C Task C.8）
- **Mangler** → 独立为 IRMangler（Task B.8）

### 实现约束

1. 所有子文件不 `#include "CodeGenModule.h"`，改为 `#include "ASTToIRConverter.h"`
2. 所有 `llvm::GlobalVariable*` → `ir::IRGlobalVariable*`
3. 所有 `llvm::Function*` → `ir::IRFunction*`
4. 所有 `llvm::Value*` → `ir::IRValue*`
5. VTable 在 IR 中为占位全局变量（IROpaqueType），具体布局由后端填充

### 验收标准

```cpp
// V1: 类布局计算
auto* Layout = CXXLayout.ComputeClassLayout(RD);
assert(Layout != nullptr);
assert(isa<ir::IRStructType>(Layout));

// V2: 构造函数生成
CtorDtor.EmitConstructor(Ctor, IRFn);
assert(IRFn->isDefinition());
assert(IRFn->getEntryBlock() != nullptr);

// V3: VTable 占位全局变量
VTableEmitter.EmitVTable(RD);
auto* VTableGV = Module->getGlobalVariable(VTableName);
assert(VTableGV != nullptr);
assert(isa<ir::IROpaqueType>(VTableGV->getType()));  // 占位类型

// V4: 继承偏移计算
uint64_t Offset = Inherit.EmitBaseOffset(Derived, Base);
assert(Offset >= 0);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.7：IREmitCXX（C++ 特有发射器，4 子文件）" && git push origin HEAD
> ```

---

## Task B.8：IRMangler（后端无关化）

### 依赖

- B.4（ASTToIRConverter，Mangler 被 ASTToIRConverter 使用）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/IRMangler.h` |
| 新增 | `src/Frontend/IRMangler.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class IRMangler {
  ASTContext& Context;
  const ir::TargetLayout& Layout;
public:
  IRMangler(ASTContext& C, const ir::TargetLayout& L);
  std::string mangleFunctionName(const NamedDecl* ND);
  std::string mangleVTable(const CXXRecordDecl* RD);
  std::string mangleTypeInfo(const CXXRecordDecl* RD);
  std::string mangleThunk(const CXXMethodDecl* MD);
  std::string mangleGuardVariable(const VarDecl* VD);
  std::string mangleStringLiteral(const StringLiteral* SL);
};

}
```

### 实现约束

1. 不依赖 CodeGenModule（核心改动：`Mangler(CodeGenModule&)` → `IRMangler(ASTContext&, const TargetLayout&)`）
2. 核心逻辑不变（只生成字符串，不涉及 IR 类型）
3. 支持 Itanium C++ ABI 名称修饰
4. TargetLayout 用于确定 size_t/ptrdiff_t 等平台相关类型

### 验收标准

```cpp
// V1: 函数名称修饰
IRMangler Mangler(ASTCtx, Layout);
auto Name = Mangler.mangleFunctionName(FuncDecl);
assert(!Name.empty());
assert(Name.starts_with("_Z"));  // Itanium ABI 前缀

// V2: VTable 名称修饰
auto VTableName = Mangler.mangleVTable(RD);
assert(VTableName.starts_with("_ZTV"));  // VTable 前缀

// V3: RTTI 名称修饰
auto RTTIName = Mangler.mangleTypeInfo(RD);
assert(RTTIName.starts_with("_ZTI"));  // TypeInfo 前缀
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.8：IRMangler（后端无关化）" && git push origin HEAD
> ```

---

## Task B.9：IRConstantEmitter

### 依赖

- Phase A（IRConstant 体系）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/IRConstantEmitter.h` |
| 新增 | `src/Frontend/IRConstantEmitter.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class IRConstantEmitter {
  ir::IRTypeContext& TypeCtx;
public:
  explicit IRConstantEmitter(ir::IRTypeContext& C);
  ir::IRConstantInt* EmitConstInt(const APSInt& Val);
  ir::IRConstantInt* EmitConstInt(uint64_t Val, unsigned BitWidth, bool IsSigned);
  ir::IRConstantFP* EmitConstFloat(const APFloat& Val);
  ir::IRConstantNull* EmitConstNull(ir::IRType* T);
  ir::IRConstantUndef* EmitConstUndef(ir::IRType* T);
  ir::IRConstantStruct* EmitConstStruct(ArrayRef<ir::IRConstant*> Vals);
  ir::IRConstantArray* EmitConstArray(ArrayRef<ir::IRConstant*> Vals);
  ir::IRConstantAggregateZero* EmitConstAggregateZero(ir::IRType* T);
};

}
```

### 实现约束

1. EmitConstInt 的 BitWidth 必须与 IRIntegerType 匹配
2. EmitConstStruct 的 Vals 数量和类型必须与 IRStructType 字段匹配
3. 常量通过 IRContext::create<T>() 分配

### 验收标准

```cpp
// V1: 整数常量
auto* CI = ConstEmitter.EmitConstInt(42, 32, false);
assert(CI->getZExtValue() == 42);

// V2: 浮点常量
auto* CF = ConstEmitter.EmitConstFloat(APFloat(3.14));
assert(CF != nullptr);

// V3: 结构体常量
auto* CS = ConstEmitter.EmitConstStruct({IntConst1, IntConst2});
assert(CS != nullptr);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.9：IRConstantEmitter" && git push origin HEAD
> ```

---

## Task B.10：CppFrontend 集成 + 契约验证

### 依赖

- B.1 ~ B.9 全部完成

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/CppFrontend.h` |
| 新增 | `src/Frontend/CppFrontend.cpp` |
| 新增 | `src/Frontend/CppFrontendRegistration.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::frontend {

class CppFrontend : public FrontendBase {
  unique_ptr<SourceManager> SM;
  unique_ptr<Preprocessor> PP;
  unique_ptr<ASTContext> ASTCtx;
  unique_ptr<Sema> SemaPtr;
  unique_ptr<Parser> ParserPtr;
  unique_ptr<ASTToIRConverter> IRConverter;
public:
  CppFrontend(const FrontendOptions& Opts, DiagnosticsEngine& Diags);
  StringRef getName() const override { return "cpp"; }
  StringRef getLanguage() const override { return "c++"; }
  unique_ptr<ir::IRModule> compile(
    StringRef Filename, ir::IRTypeContext& TypeCtx, const ir::TargetLayout& Layout) override;
  bool canHandle(StringRef Filename) const override;
};

static struct CppFrontendRegistrator {
  CppFrontendRegistrator() {
    FrontendRegistry::instance().registerFrontend("cpp", createCppFrontend);
    auto& Reg = FrontendRegistry::instance();
    Reg.addExtensionMapping(".cpp", "cpp");
    Reg.addExtensionMapping(".cc", "cpp");
    Reg.addExtensionMapping(".cxx", "cpp");
    Reg.addExtensionMapping(".C", "cpp");
    Reg.addExtensionMapping(".c", "cpp");
    Reg.addExtensionMapping(".h", "cpp");
    Reg.addExtensionMapping(".hpp", "cpp");
    Reg.addExtensionMapping(".hxx", "cpp");
  }
} CppFrontendRegistratorInstance;

}
```

### 前端-IR 契约验证

| 契约 | 说明 | 验证函数 |
|------|------|---------|
| C1: VerifierPass 通过 | IRModule 通过 VerifierPass 验证 | contract::verifyIRModuleContract() |
| C2: 类型完整性 | 所有 IRType 非 Opaque | contract::verifyTypeCompleteness() |
| C3: 函数非空 | 每个 IRFunction 至少有一个 IRBasicBlock | contract::verifyFunctionNonEmpty() |
| C4: 终结指令 | 每个 IRBasicBlock 恰好一个终结指令 | contract::verifyTerminatorContract() |
| C5: 类型一致性 | 所有 IRValue 的类型与使用点类型一致 | contract::verifyTypeConsistency() |
| C6: TargetTriple 有效 | IRModule 的 TargetTriple 非空 | contract::verifyTargetTripleValid() |

```cpp
namespace blocktype::frontend::contract {

bool verifyAllContracts(ir::IRModule& M) {
  bool OK = true;
  OK &= verifyIRModuleContract(M);
  OK &= verifyTypeCompleteness(M);
  OK &= verifyFunctionNonEmpty(M);
  OK &= verifyTerminatorContract(M);
  OK &= verifyTypeConsistency(M);
  OK &= verifyTargetTripleValid(M);
  return OK;
}

}
```

### compile() 精确流程

```
1. 初始化 SourceManager/Preprocessor/Parser/Sema
2. 解析源文件 → TranslationUnitDecl
3. 创建 ASTToIRConverter(IRCtx, TypeCtx, Layout, Diags)
4. IRConversionResult Result = Converter.convert(TU)
5. if (!Result.isUsable()) return nullptr
6. if (DebugMode || Opts.VerifyIR) contract::verifyAllContracts(*Result.getModule())
7. return Result.takeModule()
```

### 实现约束

1. CppFrontend 不直接持有任何 LLVM IR 类型（llvm::Module/llvm::Function 等）
2. 静态注册在 CppFrontendRegistration.cpp 中
3. canHandle() 通过扩展名判断

### 验收标准

```bash
# V1: CppFrontend 编译 C++ 源文件
# blocktype --emit-btir test.cpp -o test.btir
# 退出码 == 0

# V2: 输出 BTIR 文本格式
# blocktype --emit-btir test.cpp -o -
# 输出包含 "module" 和 "function" 关键字

# V3: 契约验证
# blocktype --verify-btir test.cpp
# 退出码 == 0

# V4: 差分测试（OPT-04）
# 旧路径: AST → LLVM IR → 目标文件
# 新路径: AST → BTIR → LLVM IR → 目标文件
# diff <(objdump -d old_output) <(objdump -d new_output)
# 输出为空（无差异）
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B.10：CppFrontend 集成 + 契约验证" && git push origin HEAD
> ```

---

## 附加增强任务

### Task B-F1：PluginManager 插件管理器

**依赖**：B.1（FrontendBase）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/PluginManager.h` |
| 新增 | `src/Frontend/PluginManager.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::plugin {

enum class PluginType : uint8_t {
  IRPass      = 0,
  Frontend    = 1,
  Backend     = 2,
  Diagnostic  = 3,
  Analysis    = 4,
};

struct PluginInfo {
  std::string Name;
  std::string Version;
  std::string Description;
  PluginType Type;
  std::vector<std::string> ProvidedPasses;
  std::vector<std::string> RequiredDialects;
};

class CompilerPlugin {
public:
  virtual ~CompilerPlugin() = default;
  virtual PluginInfo getInfo() const = 0;
  virtual bool initialize(CompilerInstance& CI) = 0;
  virtual void finalize() = 0;
};

class PluginManager {
  DenseMap<StringRef, std::unique_ptr<CompilerPlugin>> LoadedPlugins;
  std::vector<void*> LoadedLibraries;
public:
  bool loadPlugin(StringRef Path);
  bool unloadPlugin(StringRef Name);
  CompilerPlugin* getPlugin(StringRef Name) const;
  void registerPluginPasses(CompilerInstance& CI);
  void listPlugins(raw_ostream& OS) const;
};

}
```

**编译选项**

```
--fplugin=<path>                加载编译器插件
--fplugin-arg-<name>-<arg>      传递参数给插件
--fplugin-list                  列出已加载插件
```

**约束**：FrontendRegistry 保持不变，插件系统是上层管理器；前端插件不替换 FrontendRegistry 中的前端，而是扩展前端行为

**验收标准**

```cpp
// V1: PluginManager 加载/卸载
plugin::PluginManager PM;
assert(!PM.loadPlugin("/nonexistent.so"));
assert(PM.getPlugin("nonexistent") == nullptr);

// V2: PluginInfo 创建
plugin::PluginInfo Info;
Info.Name = "test-pass";
Info.Type = plugin::PluginType::IRPass;
Info.ProvidedPasses = {"my-pass"};
assert(Info.Name == "test-pass");
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B-F1：PluginManager 插件管理器" && git push origin HEAD
> ```

---

### Task B-F2：StructuredDiagnostic 完整结构定义（含 FixIt/Notes/SARIF）

**依赖**：A-F10（IRErrorCode）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/StructuredDiagnostic.h` |
| 新增 | `src/Frontend/StructuredDiagnostic.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::diag {

struct StructuredDiagnostic {
  diag::Level Level;
  std::string Message;
  SourceLocation PrimaryLoc;
  SmallVector<SourceRange, 4> Ranges;
  SmallVector<SourceRange, 2> RelatedLocs;
  std::string Category;
  std::string FlagName;

  struct FixItHint {
    SourceRange Range;
    std::string Replacement;
    std::string Description;
  };
  SmallVector<FixItHint, 2> FixIts;

  struct DiagnosticNote {
    SourceLocation Loc;
    std::string Message;
  };
  SmallVector<DiagnosticNote, 4> Notes;

  Optional<ir::DialectID> IRRelatedDialect;
  Optional<ir::Opcode> IRRelatedOpcode;
};

class StructuredDiagEmitter {
public:
  virtual ~StructuredDiagEmitter() = default;
  void emit(const StructuredDiagnostic& Diag);
  void emitSARIF(raw_ostream& OS) const;
  void emitJSON(raw_ostream& OS) const;
  void emitText(raw_ostream& OS) const;
};

class DiagnosticGroupManager {
  DenseMap<StringRef, SmallVector<unsigned, 8>> GroupToDiagIDs;
  DenseMap<unsigned, StringRef> DiagIDToGroup;
public:
  void enableGroup(StringRef Group);
  void disableGroup(StringRef Group);
  void listGroups(raw_ostream& OS) const;
};

}
```

**约束**：扩展 DiagnosticsEngine，不替换；支持 -Wall/-Wextra/-Werror/-Wir/-Wdialect

**验收标准**

```cpp
// V1: StructuredDiagnostic 创建
diag::StructuredDiagnostic D;
D.Level = diag::Level::Error;
D.Message = "Cannot map QualType to IRType";
D.Category = "type-error";
D.FlagName = "-Wir";
D.IRRelatedDialect = ir::DialectID::Cpp;
assert(D.Level == diag::Level::Error);

// V2: FixIt Hint
diag::StructuredDiagnostic::FixItHint FI;
FI.Range = SourceRange();
FI.Replacement = "int";
FI.Description = "Replace with int";
D.FixIts.push_back(FI);
assert(D.FixIts.size() == 1);

// V3: JSON 输出
diag::StructuredDiagEmitter Emitter;
Emitter.emit(D);
std::string JSON;
raw_string_ostream OS(JSON);
Emitter.emitJSON(OS);
assert(JSON.find("type-error") != std::string::npos);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B-F2：StructuredDiagnostic 完整结构定义（含 FixIt/Notes/SARIF）" && git push origin HEAD
> ```

---

### Task B-F3：FrontendFuzzer 前端模糊测试器

**依赖**：B.10（CppFrontend）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Testing/FrontendFuzzer.h` |
| 新增 | `tests/fuzz/FrontendFuzzer.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::testing {

class FrontendFuzzer {
public:
  std::string generateRandomSource(uint64_t Seed);
  std::string mutateSource(StringRef Seed, uint64_t Seed);

  struct FrontendFuzzResult {
    bool Crashed;
    bool AssertionFailed;
    bool ProducedInvalidAST;
    std::string TriggerSource;
    std::string ErrorOutput;
    uint64_t IterationsRun;
  };

  FrontendFuzzResult fuzz(uint64_t MaxIterations, uint64_t Seed);
  FrontendFuzzResult fuzzFromSeed(StringRef SeedSource, uint64_t MaxIterations);
  std::string minimizeTrigger(StringRef TriggerSource);
};

}
```

**编译选项**

```
--ftest-frontend-fuzz=<iterations>  运行前端模糊测试
--ftest-frontend-fuzz-seed=<seed>   模糊测试种子
```

**约束**：生成策略优先覆盖C++核心语法子集（类/模板/继承/异常）；与现有 lit 测试框架集成

**验收标准**

```cpp
// V1: 生成随机源码
testing::FrontendFuzzer Fuzzer;
std::string Src = Fuzzer.generateRandomSource(42);
assert(!Src.empty());

// V2: 变异源码
std::string Mutated = Fuzzer.mutateSource(Src, 123);
assert(!Mutated.empty());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(B): 完成 Task B-F3：FrontendFuzzer 前端模糊测试器" && git push origin HEAD
> ```

---

### Task B-F4：IREquivalenceChecker 接口定义

**依赖**：Phase A（IRModule/IRVerifier）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IREquivalenceChecker.h` |

**必须实现的类型定义**

```cpp
namespace blocktype::ir {

class IREquivalenceChecker {
public:
  struct EquivalenceResult {
    bool IsEquivalent;
    SmallVector<std::string, 8> Differences;
  };
  static EquivalenceResult check(const IRModule& A, const IRModule& B);
  static bool isStructurallyEquivalent(const IRFunction& A, const IRFunction& B);
  static bool isTypeEquivalent(const IRType* A, const IRType* B);
};

}
```

**约束**：仅定义接口，实现为远期（Phase E E-F3）

**验收标准**

```cpp
// V1: 接口存在
auto Result = ir::IREquivalenceChecker::check(ModuleA, ModuleB);
assert(Result.IsEquivalent || !Result.Differences.empty());
```
