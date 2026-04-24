# AI Coder 可执行任务流 — Phase A：IR 层基础设施

> 本文档是 AI coder 可直接执行的改造任务流。每个 Task 自包含所有必要信息：
> 接口签名、类型定义、参数约束、验收命令。AI coder 无需查阅其他文档即可编码。

---

## 执行规则

1. **严格按 Task 编号顺序执行**，每个 Task 完成并通过验收后再开始下一个
2. **接口签名不可修改**——本文档中的 class/struct/enum/函数签名是硬约束，AI coder 必须照此实现
3. **验收命令必须全部通过**——验收命令是可执行的断言，不是描述性文字
4. **命名空间**：所有 IR 层代码在 `namespace blocktype::ir` 中
5. **头文件路径**：`include/blocktype/IR/`，源文件路径：`src/IR/`
6. **依赖限制**：`libblocktype-ir` 仅依赖 `blocktype-basic`（StringRef/SmallVector/DenseMap 等），不链接 LLVM
7. **Git 提交与推送**：每个 Task 完成并通过验收后，**必须立即**执行以下操作：
   ```bash
   git add -A
   git commit -m "feat(<phase>): 完成 <Task编号> — <Task标题>"
   git push origin HEAD
   ```
   - commit message 格式：`feat(A): 完成 A.1 — IRType 体系 + IRTypeContext`
   - **不得跳过此步骤**——确保每个 Task 的产出都有远端备份，防止工作丢失
   - 如果 push 失败，先 `git pull --rebase origin HEAD` 再重试

---

## 接口关联关系

> AI coder 实现每个 Task 时，必须保证接口之间的调用/持有/引用关系正确。
> 以下是 Phase A 所有接口之间的完整关联图。每个关联标注了方向和语义。

### 持有关系（A 持有 B 的指针/引用/unique_ptr）

```
IRTypeContext ──owns──→ IRType 及所有子类（唯一化缓存，DenseMap/FoldingSet）
IRTypeContext ──owns──→ IRVoidType（直接成员）
IRTypeContext ──owns──→ IRIntegerType（BitWidth→实例的 DenseMap）
IRTypeContext ──owns──→ IRFloatType（BitWidth→实例的 DenseMap）
IRTypeContext ──owns──→ IRPointerType（FoldingSet）
IRTypeContext ──owns──→ IRArrayType（FoldingSet）
IRTypeContext ──owns──→ IRVectorType（FoldingSet）
IRTypeContext ──owns──→ IRStructType（Name→实例的 StringMap）
IRTypeContext ──owns──→ IROpaqueType（Name→实例的 StringMap）

IRContext    ──owns──→ BumpPtrAllocator（直接成员）
IRContext    ──owns──→ IRTypeContext（直接成员）
IRContext    ──owns──→ 所有 IRValue/IRInstruction（通过 BumpPtrAllocator 分配）

IRPointerType  ──ref──→ IRType（PointeeType 指针，指向 IRTypeContext 管理的类型）
IRArrayType    ──ref──→ IRType（ElementType 指针）
IRStructType   ──ref──→ SmallVector<IRType*>（FieldTypes，每个指向 IRTypeContext 管理的类型）
IRFunctionType ──ref──→ IRType（ReturnType 指针）
IRFunctionType ──ref──→ SmallVector<IRType*>（ParamTypes）
IRVectorType   ──ref──→ IRType（ElementType 指针）

IRModule     ──ref──→ IRTypeContext&（引用，不拥有）
IRModule     ──owns──→ vector<unique_ptr<IRFunction>>
IRModule     ──owns──→ vector<unique_ptr<IRFunctionDecl>>
IRModule     ──owns──→ vector<unique_ptr<IRGlobalVariable>>
IRModule     ──owns──→ vector<unique_ptr<IRGlobalAlias>>
IRModule     ──owns──→ vector<unique_ptr<IRMetadata>>

IRFunction   ──ref──→ IRModule*（Parent 指针，反向引用）
IRFunction   ──ref──→ IRFunctionType*（类型签名指针，指向 IRTypeContext 管理的类型）
IRFunction   ──owns──→ SmallVector<unique_ptr<IRArgument>>
IRFunction   ──owns──→ list<unique_ptr<IRBasicBlock>>

IRBasicBlock ──ref──→ IRFunction*（Parent 指针，反向引用）
IRBasicBlock ──owns──→ list<unique_ptr<IRInstruction>>

IRInstruction ──ref──→ IRBasicBlock*（Parent 指针，反向引用）
IRInstruction ──ref──→ IRType*（结果类型指针，指向 IRTypeContext 管理的类型）

IRValue      ──ref──→ IRType*（Ty 指针，指向 IRTypeContext 管理的类型）
User         ──ref──→ SmallVector<Use>（操作数列表，每个 Use 指向另一个 IRValue）
Use          ──ref──→ IRValue*（Val 指针，被引用的 Value）
Use          ──ref──→ User*（Owner 指针，引用者）

IRConstantInt        ──ref──→ IRIntegerType*（类型指针）
IRConstantFP         ──ref──→ IRFloatType*（类型指针）
IRConstantStruct     ──ref──→ SmallVector<IRConstant*>（元素常量列表）
IRConstantArray      ──ref──→ SmallVector<IRConstant*>（元素常量列表）
IRConstantFunctionRef ──ref──→ IRFunction*（Func 指针，跨模块引用）
IRConstantGlobalRef   ──ref──→ IRGlobalVariable*（Global 指针，跨模块引用）

IRBuilder    ──ref──→ IRBasicBlock*（InsertBB，当前插入点）
IRBuilder    ──ref──→ IRInstruction*（InsertPt，插入位置）
IRBuilder    ──ref──→ IRTypeContext&（类型工厂引用）
IRBuilder    ──ref──→ IRContext&（内存分配上下文引用）

TargetLayout ──val──→ 布局参数（TripleStr/PointerSize/IntSize 等，值语义）
```

### 调用关系（A 的方法调用 B 的方法）

```
IRType::getSizeInBits(Layout)  ──calls──→ TargetLayout::getPointerSizeInBits()
  // IRPointerType::getSizeInBits 需要 TargetLayout 提供指针大小
  // IRStructType::getSizeInBits 需要 TargetLayout 提供字段对齐计算

IRType::getAlignInBits(Layout) ──calls──→ TargetLayout::getTypeAlignInBits()
  // IRStructType::getAlignInBits 需要 TargetLayout 提供自然对齐

IRTypeContext::getIntType(BW)  ──calls──→ IRIntegerType 构造函数
IRTypeContext::getPointerType(P, AS) ──calls──→ IRPointerType 构造函数
IRTypeContext::getFunctionType(R, P, VA) ──calls──→ IRFunctionType 构造函数
IRTypeContext::getStructType(N, E, P) ──calls──→ IRStructType 构造函数
  // 所有 get*Type 方法：先查缓存，未命中则构造并插入缓存

IRContext::create<T>(args...)  ──calls──→ BumpPtrAllocator::Allocate(sizeof(T), alignof(T))
  // 然后调用 placement new T(args...)

IRModule::getOrInsertFunction(Name, Ty)
  ──calls──→ getFunction(Name)  // 先查找
  ──calls──→ new IRFunction(this, Name, Ty)  // 未找到则创建
  // 精确语义：同名同类型→返回已有；同名不同类型→返回nullptr；不存在→创建

IRFunction::addBasicBlock(Name)  ──calls──→ new IRBasicBlock(Name, this)
  // 创建 BB 并设置 Parent 反向引用

IRBasicBlock::push_back(I)  ──calls──→ IRInstruction::setParent(this)
  // 插入指令时自动设置 Parent 反向引用

IRBasicBlock::getTerminator()  ──calls──→ IRInstruction::isTerminator()
  // 遍历 InstList 查找终结指令

IRBasicBlock::getSuccessors()  ──calls──→ IRInstruction 的 Br/CondBr/Invoke 目标 BB
  // 从终结指令提取后继基本块

IRBuilder::createAdd(LHS, RHS, Name)  ──calls──→ IRContext::create<IRInstruction>(Opcode::Add, ...)
  // 然后调用 InsertBB->push_back() 或 insert()
IRBuilder::createCall(Callee, Args, Name) ──calls──→ IRContext::create<IRInstruction>(Opcode::Call, ...)
  // 所有 create* 方法：分配指令→设置操作数→插入到 InsertBB

IRBuilder::getInt32(V)  ──calls──→ IRTypeContext::getInt32Ty()
  // 然后 new IRConstantInt(Int32Ty, V)
IRBuilder::getNull(T)  ──calls──→ new IRConstantNull(T)
IRBuilder::getUndef(T) ──calls──→ new IRConstantUndef(T)

Use::set(V)  ──calls──→ 旧 Val 的 def-use 链移除 + 新 V 的 def-use 链添加
  // 维护双向 def-use 链

IRValue::replaceAllUsesWith(New)  ──calls──→ 每个 Use::set(New)
  // 遍历所有 Use 并替换

VerifierPass::run(M)  ──calls──→ verifyFunction(F) 对 M 中每个 F
  ──calls──→ verifyBasicBlock(BB) 对 F 中每个 BB
  ──calls──→ verifyInstruction(I) 对 BB 中每个 I
  ──calls──→ verifyType(T) 对 I 涉及的每个 T

IRWriter::writeText(M, OS)  ──calls──→ IRModule::print(OS)
  ──calls──→ IRFunction::print(OS) 对 M 中每个 F
  ──calls──→ IRInstruction::print(OS) 对 F 中每个 I

IRReader::parseText(Text, Ctx)  ──calls──→ IRTypeContext::getInt32Ty() 等类型工厂
  ──calls──→ IRModule::getOrInsertFunction() 创建函数
  ──calls──→ IRFunction::addBasicBlock() 创建基本块
  ──calls──→ IRBuilder::create*() 创建指令
```

### 生命周期约束

```
IRContext 生命周期 ≥ IRModule 生命周期 ≥ IRFunction 生命周期 ≥ IRBasicBlock 生命周期 ≥ IRInstruction 生命周期
IRTypeContext 生命周期 = IRContext 生命周期（IRContext 直接持有）
TargetLayout 生命周期独立（可早于 IRModule 创建，按值/引用传递）
IRBuilder 不持有任何对象（仅引用 InsertBB 和 IRCtx，生命周期可短于 IRModule）
```

### 内存管理规则

| 对象 | 分配方式 | 拥有者 | 释放方式 |
|------|---------|--------|---------|
| IRType 及子类 | IRTypeContext 内部缓存 | IRTypeContext | 随 IRContext 析构释放 |
| IRValue/IRInstruction | IRContext::create<T>() | IRBasicBlock（逻辑拥有）+ BumpPtrAllocator（物理分配） | 随 IRContext 析构批量释放 |
| IRModule | unique_ptr | CompilerInstance | unique_ptr 析构 |
| IRFunction | unique_ptr | IRModule | 随 IRModule 析构释放 |
| IRBasicBlock | unique_ptr | IRFunction | 随 IRFunction 析构释放 |
| IRConstant | IRContext::create<T>() 或栈分配 | IRTypeContext 缓存或 IRModule | 随 IRContext 析构释放 |

---

## Task A.1：IRType 体系 + IRTypeContext

### 依赖

无（首个 Task）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRType.h` |
| 新增 | `include/blocktype/IR/IRTypeContext.h` |
| 新增 | `src/IR/IRType.cpp` |
| 新增 | `src/IR/IRTypeContext.cpp` |

### 必须实现的类型定义

#### DialectID 枚举

```cpp
namespace blocktype::ir::dialect {

enum class DialectID : uint8_t {
  Core     = 0,
  Cpp      = 1,
  Target   = 2,
  Debug    = 3,
  Metadata = 4,
};

}
```

#### IRType 基类

```cpp
namespace blocktype::ir {

class IRType {
public:
  enum Kind : uint8_t {
    Void = 0, Bool = 1, Integer = 2, Float = 3,
    Pointer = 4, Array = 5, Struct = 6, Function = 7,
    Vector = 8, Opaque = 9
  };

  Kind getKind() const { return KindVal; }
  dialect::DialectID getDialect() const { return DialectVal; }

  virtual bool equals(const IRType* Other) const = 0;
  virtual std::string toString() const = 0;
  virtual uint64_t getSizeInBits(const TargetLayout& Layout) const = 0;
  virtual uint64_t getAlignInBits(const TargetLayout& Layout) const = 0;

  bool isVoid() const { return KindVal == Void; }
  bool isBool() const { return KindVal == Bool; }
  bool isInteger() const { return KindVal == Integer; }
  bool isFloat() const { return KindVal == Float; }
  bool isPointer() const { return KindVal == Pointer; }
  bool isArray() const { return KindVal == Array; }
  bool isStruct() const { return KindVal == Struct; }
  bool isFunction() const { return KindVal == Function; }
  bool isVector() const { return KindVal == Vector; }
  bool isOpaque() const { return KindVal == Opaque; }

  static bool classof(const IRType* T) { return true; }

protected:
  IRType(Kind K, dialect::DialectID D = dialect::DialectID::Core) : KindVal(K), DialectVal(D) {}
  Kind KindVal;
  dialect::DialectID DialectVal;
};

}
```

#### IRType 子类（全部必须实现）

```cpp
class IRVoidType : public IRType {
public:
  IRVoidType() : IRType(Void) {}
  bool equals(const IRType* Other) const override { return Other->isVoid(); }
  std::string toString() const override { return "void"; }
  uint64_t getSizeInBits(const TargetLayout&) const override { return 0; }
  uint64_t getAlignInBits(const TargetLayout&) const override { return 0; }
  static bool classof(const IRType* T) { return T->getKind() == Void; }
};

class IRIntegerType : public IRType {
  unsigned BitWidth;
public:
  explicit IRIntegerType(unsigned BW)
    : IRType(Integer), BitWidth(BW) {
    assert((BW == 1 || BW == 8 || BW == 16 || BW == 32 || BW == 64 || BW == 128)
           && "Invalid integer bit width");
  }
  unsigned getBitWidth() const { return BitWidth; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout&) const override { return BitWidth; }
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Integer; }
};

class IRFloatType : public IRType {
  unsigned BitWidth;
public:
  explicit IRFloatType(unsigned BW)
    : IRType(Float), BitWidth(BW) {
    assert((BW == 16 || BW == 32 || BW == 64 || BW == 80 || BW == 128)
           && "Invalid float bit width");
  }
  unsigned getBitWidth() const { return BitWidth; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout&) const override { return BitWidth; }
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Float; }
};

class IRPointerType : public IRType {
  IRType* PointeeType;
  unsigned AddressSpace;
public:
  IRPointerType(IRType* P, unsigned AS = 0)
    : IRType(Pointer), PointeeType(P), AddressSpace(AS) {
    assert(P && "PointeeType cannot be null");
  }
  IRType* getPointeeType() const { return PointeeType; }
  unsigned getAddressSpace() const { return AddressSpace; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout& Layout) const override;
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Pointer; }
};

class IRArrayType : public IRType {
  uint64_t NumElements;
  IRType* ElementType;
public:
  IRArrayType(uint64_t N, IRType* E)
    : IRType(Array), NumElements(N), ElementType(E) {
    assert(E && "ElementType cannot be null");
    assert(N > 0 && "Array must have at least 1 element");
  }
  uint64_t getNumElements() const { return NumElements; }
  IRType* getElementType() const { return ElementType; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout& Layout) const override;
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Array; }
};

class IRStructType : public IRType {
  std::string Name;
  SmallVector<IRType*, 16> FieldTypes;
  bool IsPacked;
  mutable bool IsLayoutComputed = false;
  mutable SmallVector<uint64_t, 16> FieldOffsets;
public:
  IRStructType(StringRef N, SmallVector<IRType*, 16> F, bool P = false)
    : IRType(Struct), Name(N.str()), FieldTypes(std::move(F)), IsPacked(P) {}
  StringRef getName() const { return Name; }
  ArrayRef<IRType*> getElements() const { return FieldTypes; }
  unsigned getNumFields() const { return FieldTypes.size(); }
  IRType* getFieldType(unsigned i) const { return FieldTypes[i]; }
  bool isPacked() const { return IsPacked; }
  uint64_t getFieldOffset(unsigned i, const TargetLayout& Layout) const;
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout& Layout) const override;
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Struct; }
};

class IRFunctionType : public IRType {
  IRType* ReturnType;
  SmallVector<IRType*, 8> ParamTypes;
  bool IsVarArg;
public:
  IRFunctionType(IRType* R, SmallVector<IRType*, 8> P, bool VA = false)
    : IRType(Function), ReturnType(R), ParamTypes(std::move(P)), IsVarArg(VA) {
    assert(R && "ReturnType cannot be null");
  }
  IRType* getReturnType() const { return ReturnType; }
  ArrayRef<IRType*> getParamTypes() const { return ParamTypes; }
  unsigned getNumParams() const { return ParamTypes.size(); }
  IRType* getParamType(unsigned i) const { return ParamTypes[i]; }
  bool isVarArg() const { return IsVarArg; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout&) const override { return 0; }
  uint64_t getAlignInBits(const TargetLayout&) const override { return 0; }
  static bool classof(const IRType* T) { return T->getKind() == Function; }
};

class IRVectorType : public IRType {
  unsigned NumElements;
  IRType* ElementType;
public:
  IRVectorType(unsigned N, IRType* E)
    : IRType(Vector), NumElements(N), ElementType(E) {
    assert(E && "ElementType cannot be null");
    assert((N == 2 || N == 4 || N == 8 || N == 16 || N == 32 || N == 64)
           && "Vector NumElements must be power of 2");
  }
  unsigned getNumElements() const { return NumElements; }
  IRType* getElementType() const { return ElementType; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout& Layout) const override;
  uint64_t getAlignInBits(const TargetLayout& Layout) const override;
  static bool classof(const IRType* T) { return T->getKind() == Vector; }
};

class IROpaqueType : public IRType {
  std::string Name;
public:
  explicit IROpaqueType(StringRef N)
    : IRType(Opaque), Name(N.str()) {}
  StringRef getName() const { return Name; }
  bool equals(const IRType* Other) const override;
  std::string toString() const override;
  uint64_t getSizeInBits(const TargetLayout&) const override;
  uint64_t getAlignInBits(const TargetLayout&) const override;
  static bool classof(const IRType* T) { return T->getKind() == Opaque; }
};
```

#### getSizeInBits 精确计算规则

| 类型 | 计算规则 | 示例（x86_64） |
|------|---------|---------------|
| IRVoidType | 固定返回 0 | 0 |
| IRIntegerType | 返回 BitWidth | i32→32, i64→64 |
| IRFloatType | 返回 BitWidth | f32→32, f64→64, f80→80 |
| IRPointerType | 返回 Layout.getPointerSizeInBits() | x86_64→64 |
| IRArrayType | NumElements * ElementType::getSizeInBits() | [10 x i32]→320 |
| IRStructType | 非 packed：按 ABI 对齐计算字段偏移+尾部 padding；packed：紧密排列 | {i8, i32}→64 |
| IRFunctionType | 固定返回 0 | 0 |
| IRVectorType | NumElements * ElementType::getSizeInBits() | <4 x float>→128 |
| IROpaqueType | 触发 assert（不可调用） | N/A |

#### IRStructType 布局计算算法

1. 遍历字段，对每个字段计算对齐后的偏移：`offset = alignTo(currentOffset, fieldAlign)`
2. packed 模式跳过对齐，偏移直接累加
3. 非 packed 模式尾部 padding：`totalSize = alignTo(lastFieldEnd, structAlign)`
4. 结果缓存到 FieldOffsets 和 IsLayoutComputed

#### IRTypeContext

```cpp
class IRTypeContext {
  DenseMap<unsigned, unique_ptr<IRIntegerType>> IntTypes;
  DenseMap<unsigned, unique_ptr<IRFloatType>> FloatTypes;
  FoldingSet<IRPointerType> PointerTypes;
  FoldingSet<IRArrayType> ArrayTypes;
  FoldingSet<IRFunctionType> FunctionTypes;
  FoldingSet<IRVectorType> VectorTypes;
  StringMap<unique_ptr<IRStructType>> NamedStructTypes;
  StringMap<unique_ptr<IROpaqueType>> OpaqueTypes;
  IRVoidType VoidType;
  IRIntegerType BoolType;
  unsigned NumTypesCreated = 0;
public:
  IRTypeContext();
  IRVoidType* getVoidType() { return &VoidType; }
  IRIntegerType* getBoolType() { return &BoolType; }
  IRIntegerType* getIntType(unsigned BitWidth);
  IRIntegerType* getInt1Ty()   { return getIntType(1); }
  IRIntegerType* getInt8Ty()   { return getIntType(8); }
  IRIntegerType* getInt16Ty()  { return getIntType(16); }
  IRIntegerType* getInt32Ty()  { return getIntType(32); }
  IRIntegerType* getInt64Ty()  { return getIntType(64); }
  IRIntegerType* getInt128Ty() { return getIntType(128); }
  IRFloatType* getFloatType(unsigned BitWidth);
  IRFloatType* getHalfTy()     { return getFloatType(16); }
  IRFloatType* getFloatTy()    { return getFloatType(32); }
  IRFloatType* getDoubleTy()   { return getFloatType(64); }
  IRFloatType* getFloat80Ty()  { return getFloatType(80); }
  IRFloatType* getFloat128Ty() { return getFloatType(128); }
  IRPointerType* getPointerType(IRType* Pointee, unsigned AddressSpace = 0);
  IRArrayType* getArrayType(IRType* Element, uint64_t Count);
  IRVectorType* getVectorType(IRType* Element, unsigned Count);
  IRStructType* getStructType(StringRef Name, SmallVector<IRType*, 16> Elems, bool Packed = false);
  IRStructType* getAnonStructType(SmallVector<IRType*, 16> Elems, bool Packed = false);
  bool setStructBody(IRStructType* S, SmallVector<IRType*, 16> Elems, bool Packed = false);
  IRFunctionType* getFunctionType(IRType* Ret, SmallVector<IRType*, 8> Params, bool VarArg = false);
  IROpaqueType* getOpaqueType(StringRef Name);
  IRStructType* getStructTypeByName(StringRef Name) const;
  IROpaqueType* getOpaqueTypeByName(StringRef Name) const;
  unsigned getNumTypesCreated() const { return NumTypesCreated; }
  size_t getMemoryUsage() const;
};
```

### 实现约束

1. 不依赖 LLVM（不 #include 任何 llvm/ 头文件）
2. IRType 必须虚析构
3. getSizeInBits 必须 const
4. 所有子类不可拷贝
5. IRTypeContext 唯一化语义：相同参数返回相同指针

### 验收标准

```cpp
// V1: 整数类型大小
assert(IRIntegerType(32).getSizeInBits(Layout) == 32);

// V2: IRTypeContext 唯一化
IRTypeContext Ctx;
auto* Int32_1 = Ctx.getInt32Ty();
auto* Int32_2 = Ctx.getInt32Ty();
assert(Int32_1 == Int32_2);  // 同一指针

// V3: 指针类型唯一化
auto* PtrI8_1 = Ctx.getPointerType(Ctx.getInt8Ty());
auto* PtrI8_2 = Ctx.getPointerType(Ctx.getInt8Ty());
assert(PtrI8_1 == PtrI8_2);  // 同一指针

// V4: 类型映射
// mapType(BuiltinType::Int) → IRIntegerType(32)
// mapType(BuiltinType::Float64) → IRFloatType(64)
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.1：IRType 体系 + IRTypeContext" && git push origin HEAD
> ```

---

## Task A.1.1：IRContext + BumpPtrAllocator 内存管理

### 依赖

- A.1（IRType 体系）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRContext.h` |
| 新增 | `src/IR/IRContext.cpp` |

### 必须实现的类型定义

#### IRThreadingMode 枚举

```cpp
namespace blocktype::ir {

enum class IRThreadingMode {
  SingleThread,
  MultiInstance,
  SharedReadOnly
};

}
```

#### IRContext

```cpp
class IRContext {
  llvm::BumpPtrAllocator Allocator;
  std::vector<std::function<void()>> Cleanups;
  IRTypeContext TypeCtx;
  IRThreadingMode ThreadingMode = IRThreadingMode::SingleThread;
public:
  template <typename T, typename... Args>
  T *create(Args &&...args) {
    void *Mem = Allocator.Allocate(sizeof(T), alignof(T));
    T *Node = new (Mem) T(std::forward<Args>(args)...);
    return Node;
  }

  void addCleanup(std::function<void()> Callback) {
    Cleanups.push_back(std::move(Callback));
  }

  llvm::StringRef saveString(llvm::StringRef Str);
  IRTypeContext &getTypeContext() { return TypeCtx; }
  size_t getMemoryUsage() const { return Allocator.getTotalMemory(); }
  void setThreadingMode(IRThreadingMode Mode) { ThreadingMode = Mode; }
  IRThreadingMode getThreadingMode() const { return ThreadingMode; }
  void sealModule(IRModule &M);

  ~IRContext() {
    for (auto It = Cleanups.rbegin(); It != Cleanups.rend(); ++It)
      (*It)();
  }
};

}
```

### 实现约束

1. 不依赖 LLVM（使用自实现 BumpPtrAllocator，从 blocktype-basic 引入）
2. IRContext 拥有所有 IR 节点内存
3. IRModule 仅拥有逻辑结构
4. BumpPtrAllocator 的 Allocate 方法签名：`void* Allocate(size_t Size, size_t Alignment)`

### 验收标准

```cpp
// V1: create 分配成功
IRContext Ctx;
auto* IntTy = Ctx.create<IRIntegerType>(32);
assert(IntTy != nullptr);
assert(IntTy->getBitWidth() == 32);

// V2: 析构自动释放（无内存泄漏）
{
  IRContext Ctx;
  for (int i = 0; i < 1000; ++i)
    Ctx.create<IRIntegerType>(32);
} // ~IRContext() 自动释放

// V3: addCleanup 回调正确执行
bool CleanupCalled = false;
{
  IRContext Ctx;
  Ctx.addCleanup([&]() { CleanupCalled = true; });
}
assert(CleanupCalled == true);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.1.1：IRContext + BumpPtrAllocator 内存管理" && git push origin HEAD
> ```

---

## Task A.1.2：IRThreadingMode 枚举 + seal 接口

### 依赖

- A.1.1（IRContext）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `include/blocktype/IR/IRContext.h` |
| 修改 | `src/IR/IRContext.cpp` |

### 必须实现的修改

1. IRThreadingMode 枚举已在 A.1.1 中定义，此处确保 sealModule() 实现
2. `sealModule(IRModule& M)` 标记 IRModule 不可变，之后任何修改操作触发 assert

### 实现约束

1. 枚举值：SingleThread / MultiInstance / SharedReadOnly
2. sealModule() 后 IRModule 不可修改（修改操作触发 assert 或返回错误）
3. SingleThread 为默认值

### 验收标准

```cpp
// V1: 默认线程模式
IRContext Ctx;
assert(Ctx.getThreadingMode() == IRThreadingMode::SingleThread);

// V2: 设置线程模式
Ctx.setThreadingMode(IRThreadingMode::MultiInstance);
assert(Ctx.getThreadingMode() == IRThreadingMode::MultiInstance);

// V3: sealModule 后不可修改
IRTypeContext TypeCtx;
auto Module = std::make_unique<IRModule>("test", TypeCtx);
Ctx.sealModule(*Module);
// Module->addFunction(...) → 触发 assert 或返回错误
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.1.2：IRThreadingMode 枚举 + seal 接口" && git push origin HEAD
> ```

---

## Task A.2：TargetLayout（独立于 LLVM DataLayout）

### 依赖

- A.1（IRType 体系，因为 getSizeInBits 需要 TargetLayout 参数）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/TargetLayout.h` |
| 新增 | `src/IR/TargetLayout.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::ir {

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
  uint64_t getPointerSizeInBits() const { return PointerSize * 8; }
  bool isLittleEndian() const { return IsLittleEndian; }
  static unique_ptr<TargetLayout> Create(StringRef Triple);
};

}
```

### 平台布局参数表

| 参数 | x86_64-unknown-linux-gnu | aarch64-unknown-linux-gnu | x86_64-apple-macosx |
|------|--------------------------|---------------------------|---------------------|
| PointerSize | 8 | 8 | 8 |
| IntSize | 4 | 4 | 4 |
| LongSize | 8 | 8 | 8 |
| LongLongSize | 8 | 8 | 8 |
| FloatSize | 4 | 4 | 4 |
| DoubleSize | 8 | 8 | 8 |
| LongDoubleSize | 16 | 16 | 16 |
| IsLittleEndian | true | true | true |
| IsLinux | true | true | false |
| IsMacOS | false | false | true |

### 实现约束

1. 不依赖 LLVM DataLayout
2. 从 Triple 字符串推断布局
3. 支持 x86_64 / aarch64 两个平台
4. getTypeSizeInBits 对 IRType 各子类的计算规则见 A.1 中的 getSizeInBits 精确计算规则表

### 验收标准

```cpp
// V1: x86_64 指针大小
auto Layout = TargetLayout::Create("x86_64-unknown-linux-gnu");
assert(Layout->getPointerSizeInBits() == 64);

// V2: x86_64 小端
assert(Layout->isLittleEndian() == true);

// V3: aarch64 小端
auto LayoutARM = TargetLayout::Create("aarch64-unknown-linux-gnu");
assert(LayoutARM->isLittleEndian() == true);

// V4: 类型大小
IRTypeContext Ctx;
assert(Layout->getTypeSizeInBits(Ctx.getInt32Ty()) == 32);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.2：TargetLayout（独立于 LLVM DataLayout）" && git push origin HEAD
> ```

---

## Task A.3：IRValue + IRConstant + Use/User 体系

### 依赖

- A.1（IRType 体系）
- A.2（TargetLayout，IRValue 的 getType 需要 IRType*）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRValue.h` |
| 新增 | `include/blocktype/IR/IRInstruction.h` |
| 新增 | `include/blocktype/IR/IRConstant.h` |
| 新增 | `src/IR/IRValue.cpp` |
| 新增 | `src/IR/IRInstruction.cpp` |
| 新增 | `src/IR/IRConstant.cpp` |

### 必须实现的类型定义

#### ValueKind 枚举

```cpp
enum class ValueKind : uint8_t {
  ConstantInt           = 0,
  ConstantFloat         = 1,
  ConstantNull          = 2,
  ConstantUndef         = 3,
  ConstantStruct        = 4,
  ConstantArray         = 5,
  ConstantAggregateZero = 6,
  ConstantFunctionRef   = 7,
  ConstantGlobalRef     = 8,
  InstructionResult     = 9,
  Argument              = 10,
  BasicBlockRef         = 11,
};
```

#### Opcode 枚举

```cpp
enum class Opcode : uint16_t {
  Ret = 0, Br = 1, CondBr = 2, Switch = 3, Invoke = 4, Unreachable = 5, Resume = 6,
  Add = 16, Sub = 17, Mul = 18, UDiv = 19, SDiv = 20, URem = 21, SRem = 22,
  FAdd = 32, FSub = 33, FMul = 34, FDiv = 35, FRem = 36,
  Shl = 48, LShr = 49, AShr = 50, And = 51, Or = 52, Xor = 53,
  Alloca = 64, Load = 65, Store = 66, GEP = 67, Memcpy = 68, Memset = 69,
  Trunc = 80, ZExt = 81, SExt = 82, FPTrunc = 83, FPExt = 84,
  FPToSI = 85, FPToUI = 86, SIToFP = 87, UIToFP = 88,
  PtrToInt = 89, IntToPtr = 90, BitCast = 91,
  ICmp = 96, FCmp = 97,
  Call = 112,
  Phi = 128, Select = 129, ExtractValue = 130, InsertValue = 131,
  ExtractElement = 132, InsertElement = 133, ShuffleVector = 134,
  DbgDeclare = 144, DbgValue = 145, DbgLabel = 146,
  FFICall = 160, FFICheck = 161, FFICoerce = 162, FFIUnwind = 163,
  AtomicLoad = 176, AtomicStore = 177, AtomicRMW = 178, AtomicCmpXchg = 179, Fence = 180,
};
```

#### ICmpPred / FCmpPred 枚举

```cpp
enum class ICmpPred : uint8_t {
  EQ = 0, NE = 1, UGT = 2, UGE = 3, ULT = 4, ULE = 5, SGT = 6, SGE = 7, SLT = 8, SLE = 9,
};

enum class FCmpPred : uint8_t {
  False = 0, OEQ = 1, OGT = 2, OGE = 3, OLT = 4, OLE = 5, ONE = 6, ORD = 7,
  UNO = 8, UEQ = 9, UGT = 10, UGE = 11, ULT = 12, ULE = 13, UNE = 14, True = 15,
};
```

#### LinkageKind / CallingConvention / FunctionAttrs 枚举

```cpp
enum class LinkageKind : uint8_t {
  External = 0, Private = 1, Internal = 2, LinkOnce = 3, LinkOnceODR = 4,
  Weak = 5, WeakODR = 6, Common = 7, Appending = 8, ExternalWeak = 9,
  AvailableExternally = 10,
};

enum class CallingConvention : uint8_t {
  C = 0, Fast = 1, Cold = 2, GHC = 3, Stdcall = 4, Fastcall = 5,
  VectorCall = 6, ThisCall = 7, Swift = 8, WASM = 9, BTInternal = 10,
};

enum class FunctionAttr : uint32_t {
  NoReturn = 1 << 0, NoThrow = 1 << 1, ReadOnly = 1 << 2, WriteOnly = 1 << 3,
  ReadNone = 1 << 4, NoInline = 1 << 5, AlwaysInline = 1 << 6, NoUnwind = 1 << 7,
  Pure = 1 << 8, Const = 1 << 9, Naked = 1 << 10, NoRecurse = 1 << 11,
  WillReturn = 1 << 12, MustProgress = 1 << 13,
};
using FunctionAttrs = uint32_t;
```

#### IRValue / Use / User

```cpp
class IRValue {
protected:
  ValueKind Kind;
  IRType* Ty;
  unsigned ValueID;
  std::string Name;
public:
  IRValue(ValueKind K, IRType* T, unsigned ID, StringRef N = "")
    : Kind(K), Ty(T), ValueID(ID), Name(N.str()) {}
  virtual ~IRValue() = default;
  ValueKind getValueKind() const { return Kind; }
  IRType* getType() const { return Ty; }
  unsigned getValueID() const { return ValueID; }
  StringRef getName() const { return Name; }
  void setName(StringRef N) { Name = N.str(); }
  bool isConstant() const { return static_cast<uint8_t>(Kind) <= 8; }
  bool isInstruction() const { return Kind == ValueKind::InstructionResult; }
  bool isArgument() const { return Kind == ValueKind::Argument; }
  void replaceAllUsesWith(IRValue* New);
  unsigned getNumUses() const;
  virtual void print(raw_ostream& OS) const = 0;
};

class Use {
  IRValue* Val;
  User* Owner;
public:
  Use() : Val(nullptr), Owner(nullptr) {}
  Use(IRValue* V, User* O) : Val(V), Owner(O) {}
  IRValue* get() const { return Val; }
  void set(IRValue* V);
  User* getUser() const { return Owner; }
  operator IRValue*() const { return Val; }
};

class User : public IRValue {
  SmallVector<Use, 4> Operands;
public:
  User(ValueKind K, IRType* T, unsigned ID, StringRef N = "")
    : IRValue(K, T, ID, N) {}
  unsigned getNumOperands() const { return Operands.size(); }
  IRValue* getOperand(unsigned i) const { return Operands[i].get(); }
  void setOperand(unsigned i, IRValue* V) { Operands[i].set(V); }
  void addOperand(IRValue* V);
  ArrayRef<Use> operands() const { return Operands; }
  IRType* getOperandType(unsigned i) const { return Operands[i].get()->getType(); }
};
```

#### IRInstruction

```cpp
class IRInstruction : public User {
  Opcode Op;
  dialect::DialectID Dialect;
  IRBasicBlock* Parent;
public:
  IRInstruction(Opcode O, IRType* Ty, unsigned ID,
                dialect::DialectID D = dialect::DialectID::Core, StringRef N = "")
    : User(ValueKind::InstructionResult, Ty, ID, N),
      Op(O), Dialect(D), Parent(nullptr) {}
  Opcode getOpcode() const { return Op; }
  dialect::DialectID getDialect() const { return Dialect; }
  IRBasicBlock* getParent() const { return Parent; }
  void setParent(IRBasicBlock* BB) { Parent = BB; }
  bool isTerminator() const;
  bool isBinaryOp() const;
  bool isCast() const;
  bool isMemoryOp() const;
  bool isComparison() const;
  void eraseFromParent();
  void print(raw_ostream& OS) const override;
};
```

#### IRConstant 系列

```cpp
class IRConstant : public IRValue {
public:
  IRConstant(ValueKind K, IRType* T, unsigned ID) : IRValue(K, T, ID) {}
  static bool classof(const IRValue* V) { return V->isConstant(); }
};

class IRConstantInt : public IRConstant {
  APInt Value;
public:
  IRConstantInt(IRIntegerType* Ty, const APInt& V);
  IRConstantInt(IRIntegerType* Ty, uint64_t V);
  const APInt& getValue() const { return Value; }
  uint64_t getZExtValue() const { return Value.getZExtValue(); }
  int64_t getSExtValue() const { return Value.getSExtValue(); }
  bool isZero() const { return Value.isZero(); }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantInt; }
};

class IRConstantFP : public IRConstant {
  APFloat Value;
public:
  IRConstantFP(IRFloatType* Ty, const APFloat& V);
  const APFloat& getValue() const { return Value; }
  bool isZero() const { return Value.isZero(); }
  bool isNaN() const { return Value.isNaN(); }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantFloat; }
};

class IRConstantNull : public IRConstant {
public:
  explicit IRConstantNull(IRType* Ty) : IRConstant(ValueKind::ConstantNull, Ty, 0) {}
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantNull; }
};

class IRConstantUndef : public IRConstant {
public:
  explicit IRConstantUndef(IRType* Ty) : IRConstant(ValueKind::ConstantUndef, Ty, 0) {}
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantUndef; }
};

class IRConstantAggregateZero : public IRConstant {
public:
  explicit IRConstantAggregateZero(IRType* Ty) : IRConstant(ValueKind::ConstantAggregateZero, Ty, 0) {}
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantAggregateZero; }
};

class IRConstantStruct : public IRConstant {
  SmallVector<IRConstant*, 16> Elements;
public:
  IRConstantStruct(IRStructType* Ty, SmallVector<IRConstant*, 16> Elems);
  ArrayRef<IRConstant*> getElements() const { return Elements; }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantStruct; }
};

class IRConstantArray : public IRConstant {
  SmallVector<IRConstant*, 16> Elements;
public:
  IRConstantArray(IRArrayType* Ty, SmallVector<IRConstant*, 16> Elems);
  ArrayRef<IRConstant*> getElements() const { return Elements; }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantArray; }
};

class IRConstantFunctionRef : public IRConstant {
  IRFunction* Func;
public:
  explicit IRConstantFunctionRef(IRFunction* F);
  IRFunction* getFunction() const { return Func; }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantFunctionRef; }
};

class IRConstantGlobalRef : public IRConstant {
  IRGlobalVariable* Global;
public:
  explicit IRConstantGlobalRef(IRGlobalVariable* G);
  IRGlobalVariable* getGlobal() const { return Global; }
  static bool classof(const IRValue* V) { return V->getValueKind() == ValueKind::ConstantGlobalRef; }
};
```

### 实现约束

1. Use-Def chain 必须双向正确（Use::set 更新 def-use 链）
2. IRConstant 子类不可变（immutable）
3. IRConstantUndef::get(TypeCtx, T) 必须缓存（同一类型返回同一指针）

### 验收标准

```cpp
// V1: 常量整数值
IRTypeContext Ctx;
auto* Int32 = Ctx.getInt32Ty();
auto* CI = IRConstantInt(Int32, 42);
assert(CI->getZExtValue() == 42);

// V2: Use/User 双向链接
auto* V1 = IRConstantInt(Int32, 1);
auto* V2 = IRConstantInt(Int32, 2);
// 创建 add 指令后：
// Use::set(V1) → getUser()->getOperand(0) == V1

// V3: IRConstantUndef 缓存
auto* Undef1 = IRConstantUndef(Int32);
auto* Undef2 = IRConstantUndef(Int32);
// 同类型应返回同一指针（如果通过缓存工厂获取）
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.3：IRValue + IRConstant + Use/User 体系" && git push origin HEAD
> ```

---

## Task A.3.1：IRFormatVersion + IRFileHeader

### 依赖

- A.1（IRType 体系）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRFormatVersion.h` |
| 新增 | `src/IR/IRFormatVersion.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::ir {

struct IRFormatVersion {
  uint16_t Major;
  uint16_t Minor;
  uint16_t Patch;
  static constexpr IRFormatVersion Current() { return {1, 0, 0}; }
  bool isCompatibleWith(IRFormatVersion ReaderVersion) const {
    if (Major != ReaderVersion.Major) return false;
    if (ReaderVersion.Minor < Minor) return false;
    return true;
  }
  std::string toString() const;
};

struct IRFileHeader {
  char Magic[4] = {'B', 'T', 'I', 'R'};
  IRFormatVersion Version = IRFormatVersion::Current();
  uint32_t Flags = 0;
  uint32_t ModuleOffset = 0;
  uint32_t StringTableOffset = 0;
  uint32_t StringTableSize = 0;
};

}
```

### 实现约束

1. 版本号独立于编译器版本
2. 魔数 "BTIR"
3. isCompatibleWith 语义：Major 必须相等，Reader Minor >= File Minor
4. IRFileHeader 大小固定（sizeof(IRFileHeader) 必须为常量）

### 验收标准

```cpp
// V1: 当前版本
assert(IRFormatVersion::Current().Major == 1);
assert(IRFormatVersion::Current().Minor == 0);
assert(IRFormatVersion::Current().Patch == 0);

// V2: 兼容性
assert(IRFormatVersion{1, 0, 0}.isCompatibleWith({1, 0, 0}) == true);
assert(IRFormatVersion{2, 0, 0}.isCompatibleWith({1, 0, 0}) == false);
assert(IRFormatVersion{1, 5, 0}.isCompatibleWith({1, 0, 0}) == false);  // Reader Minor < File Minor

// V3: IRFileHeader 大小固定
static_assert(sizeof(IRFileHeader) == 4 + 6 + 4 + 4 + 4 + 4);  // 魔数+版本+flags+3个offset
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.3.1：IRFormatVersion + IRFileHeader" && git push origin HEAD
> ```

---

## Task A.4：IRModule / IRFunction / IRBasicBlock / IRFunctionDecl

### 依赖

- A.1（IRType 体系）
- A.3（IRValue, IRInstruction, IRConstant）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRModule.h` |
| 新增 | `include/blocktype/IR/IRFunction.h` |
| 新增 | `include/blocktype/IR/IRBasicBlock.h` |
| 新增 | `src/IR/IRModule.cpp` |
| 新增 | `src/IR/IRFunction.cpp` |
| 新增 | `src/IR/IRBasicBlock.cpp` |

### 必须实现的类型定义

#### IRArgument

```cpp
class IRArgument {
  IRType* ParamType;
  std::string Name;
  unsigned ArgNo;
  unsigned Attrs = 0;
public:
  IRArgument(IRType* T, unsigned No, StringRef N = "")
    : ParamType(T), Name(N.str()), ArgNo(No) {}
  IRType* getType() const { return ParamType; }
  StringRef getName() const { return Name; }
  unsigned getArgNo() const { return ArgNo; }
  bool hasAttr(unsigned A) const { return (Attrs & A) != 0; }
  void addAttr(unsigned A) { Attrs |= A; }
  void print(raw_ostream& OS) const;
};
```

#### IRBasicBlock

```cpp
class IRBasicBlock {
  IRFunction* Parent;
  std::string Name;
  std::list<unique_ptr<IRInstruction>> InstList;
public:
  explicit IRBasicBlock(StringRef N, IRFunction* P = nullptr)
    : Parent(P), Name(N.str()) {}
  StringRef getName() const { return Name; }
  IRFunction* getParent() const { return Parent; }
  void setParent(IRFunction* F) { Parent = F; }
  auto& getInstList() { return InstList; }
  const auto& getInstList() const { return InstList; }
  IRInstruction* getTerminator();
  IRInstruction* getFirstNonPHI();
  IRInstruction* getFirstInsertionPt();
  IRInstruction* push_back(unique_ptr<IRInstruction> I);
  IRInstruction* push_front(unique_ptr<IRInstruction> I);
  void insert(IRInstruction* After, unique_ptr<IRInstruction> I);
  unique_ptr<IRInstruction> erase(IRInstruction* I);
  SmallVector<IRBasicBlock*, 4> getPredecessors() const;
  SmallVector<IRBasicBlock*, 4> getSuccessors() const;
  size_t size() const { return InstList.size(); }
  bool empty() const { return InstList.empty(); }
  void print(raw_ostream& OS) const;
};
```

#### IRFunction

```cpp
class IRFunction {
  IRModule* Parent;
  std::string Name;
  IRFunctionType* Ty;
  LinkageKind Linkage;
  CallingConvention CallConv;
  SmallVector<unique_ptr<IRArgument>> Args;
  std::list<unique_ptr<IRBasicBlock>> BasicBlocks;
  FunctionAttrs Attrs;
  unsigned Alignment = 0;
  StringRef Section;
public:
  IRFunction(IRModule* M, StringRef N, IRFunctionType* T,
             LinkageKind L = LinkageKind::External,
             CallingConvention CC = CallingConvention::C);
  StringRef getName() const { return Name; }
  IRModule* getParent() const { return Parent; }
  IRFunctionType* getFunctionType() const { return Ty; }
  LinkageKind getLinkage() const { return Linkage; }
  CallingConvention getCallingConv() const { return CallConv; }
  FunctionAttrs getAttributes() const { return Attrs; }
  void addAttribute(FunctionAttr A) { Attrs |= static_cast<uint32_t>(A); }
  unsigned getNumArgs() const { return Args.size(); }
  IRArgument* getArg(unsigned i) const { return Args[i].get(); }
  IRType* getArgType(unsigned i) const { return Args[i]->getType(); }
  IRBasicBlock* addBasicBlock(StringRef Name);
  IRBasicBlock* getEntryBlock();
  auto& getBasicBlocks() { return BasicBlocks; }
  unsigned getNumBasicBlocks() const { return BasicBlocks.size(); }
  IRType* getReturnType() const { return Ty->getReturnType(); }
  bool isDeclaration() const { return BasicBlocks.empty(); }
  bool isDefinition() const { return !BasicBlocks.empty(); }
  void print(raw_ostream& OS) const;
};
```

#### IRFunctionDecl

```cpp
class IRFunctionDecl {
  std::string Name;
  IRFunctionType* Ty;
  LinkageKind Linkage;
  CallingConvention CallConv;
public:
  IRFunctionDecl(StringRef N, IRFunctionType* T,
                 LinkageKind L = LinkageKind::External,
                 CallingConvention CC = CallingConvention::C);
  StringRef getName() const { return Name; }
  IRFunctionType* getFunctionType() const { return Ty; }
  LinkageKind getLinkage() const { return Linkage; }
  CallingConvention getCallingConv() const { return CallConv; }
};
```

#### IRGlobalVariable

```cpp
class IRGlobalVariable {
  std::string Name;
  IRType* Ty;
  LinkageKind Linkage;
  IRConstant* Initializer;
  unsigned Alignment;
  bool IsConstant;
  StringRef Section;
  unsigned AddressSpace = 0;
public:
  IRGlobalVariable(StringRef N, IRType* T, bool IsConst,
                   LinkageKind L = LinkageKind::External,
                   IRConstant* Init = nullptr, unsigned Align = 0, unsigned AS = 0);
  StringRef getName() const { return Name; }
  IRType* getType() const { return Ty; }
  LinkageKind getLinkage() const { return Linkage; }
  IRConstant* getInitializer() const { return Initializer; }
  void setInitializer(IRConstant* C) { Initializer = C; }
  bool hasInitializer() const { return Initializer != nullptr; }
  unsigned getAlignment() const { return Alignment; }
  bool isConstant() const { return IsConstant; }
  unsigned getAddressSpace() const { return AddressSpace; }
};
```

#### IRGlobalAlias

```cpp
class IRGlobalAlias {
  std::string Name;
  IRType* Ty;
  IRConstant* Aliasee;
public:
  IRGlobalAlias(StringRef N, IRType* T, IRConstant* A);
  StringRef getName() const { return Name; }
  IRType* getType() const { return Ty; }
  IRConstant* getAliasee() const { return Aliasee; }
};
```

#### IRModule

```cpp
class IRModule {
  IRTypeContext& TypeCtx;
  std::string Name, TargetTriple, DataLayoutStr;
  std::vector<unique_ptr<IRFunction>> Functions;
  std::vector<unique_ptr<IRFunctionDecl>> FunctionDecls;
  std::vector<unique_ptr<IRGlobalVariable>> Globals;
  std::vector<unique_ptr<IRGlobalAlias>> Aliases;
  std::vector<unique_ptr<IRMetadata>> Metadata;
  bool IsReproducible = false;
  uint32_t RequiredFeatures = 0;
public:
  IRModule(StringRef N, IRTypeContext& Ctx, StringRef Triple = "", StringRef DL = "");
  StringRef getName() const { return Name; }
  StringRef getTargetTriple() const { return TargetTriple; }
  void setTargetTriple(StringRef T) { TargetTriple = T.str(); }
  IRTypeContext& getTypeContext() const { return TypeCtx; }
  IRFunction* getFunction(StringRef Name) const;
  IRFunction* getOrInsertFunction(StringRef Name, IRFunctionType* Ty);
  void addFunction(unique_ptr<IRFunction> F);
  auto& getFunctions() { return Functions; }
  unsigned getNumFunctions() const { return Functions.size(); }
  IRFunctionDecl* getFunctionDecl(StringRef Name) const;
  void addFunctionDecl(unique_ptr<IRFunctionDecl> D);
  IRGlobalVariable* getGlobalVariable(StringRef Name) const;
  IRGlobalVariable* getOrInsertGlobal(StringRef Name, IRType* Ty);
  void addGlobal(unique_ptr<IRGlobalVariable> GV);
  auto& getGlobals() { return Globals; }
  void addAlias(unique_ptr<IRGlobalAlias> A);
  void addMetadata(unique_ptr<IRMetadata> M);
  bool isReproducible() const { return IsReproducible; }
  void setReproducible(bool V) { IsReproducible = V; }
  uint32_t getRequiredFeatures() const { return RequiredFeatures; }
  void addRequiredFeature(IRFeature F) { RequiredFeatures |= static_cast<uint32_t>(F); }
  void print(raw_ostream& OS) const;
};
```

### getOrInsertFunction 精确语义

1. 按名查找，找到且类型匹配 → 返回已有
2. 按名查找，找到但类型不匹配 → 返回 nullptr（类型冲突错误）
3. 未找到 → 创建新 IRFunction 并插入

### 实现约束

1. IRFunction 持有 IRBasicBlock 列表
2. IRModule 持有所有函数/全局变量
3. getOrInsertFunction 语义与 LLVM 一致

### 验收标准

```cpp
// V1: getOrInsertFunction 创建新函数
IRTypeContext Ctx;
IRModule Mod("test", Ctx);
auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {Ctx.getInt32Ty(), Ctx.getInt32Ty()});
auto* F = Mod.getOrInsertFunction("foo", FTy);
assert(F != nullptr);
assert(F->getName() == "foo");

// V2: addBasicBlock
auto* Entry = F->addBasicBlock("entry");
assert(Entry != nullptr);
assert(Entry->getName() == "entry");

// V3: getTerminator（无终结指令时返回 nullptr）
assert(Entry->getTerminator() == nullptr);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.4：IRModule / IRFunction / IRBasicBlock / IRFunctionDecl" && git push origin HEAD
> ```

---

## Task A.5：IRBuilder（含常量工厂）

### 依赖

- A.3（IRValue, IRInstruction, IRConstant）
- A.4（IRModule, IRFunction, IRBasicBlock）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRBuilder.h` |
| 新增 | `src/IR/IRBuilder.cpp` |

### 必须实现的接口

```cpp
class IRBuilder {
  IRBasicBlock* InsertBB = nullptr;
  IRInstruction* InsertPt = nullptr;
  IRTypeContext& TypeCtx;
  IRContext& IRCtx;
public:
  explicit IRBuilder(IRContext& Ctx) : TypeCtx(Ctx.getTypeContext()), IRCtx(Ctx) {}

  void setInsertPoint(IRBasicBlock* BB) { InsertBB = BB; InsertPt = nullptr; }
  void setInsertPoint(IRBasicBlock* BB, IRInstruction* Before) { InsertBB = BB; InsertPt = Before; }
  IRBasicBlock* getInsertBlock() const { return InsertBB; }

  // 常量工厂
  IRConstantInt* getInt1(bool V);
  IRConstantInt* getInt32(uint32_t V);
  IRConstantInt* getInt64(uint64_t V);
  IRConstantNull* getNull(IRType* T);
  IRConstantUndef* getUndef(IRType* T);

  // 终结指令
  IRInstruction* createRet(IRValue* V);
  IRInstruction* createRetVoid();
  IRInstruction* createBr(IRBasicBlock* Dest);
  IRInstruction* createCondBr(IRValue* Cond, IRBasicBlock* TrueBB, IRBasicBlock* FalseBB);
  IRInstruction* createInvoke(IRFunction* Callee, ArrayRef<IRValue*> Args,
                               IRBasicBlock* NormalBB, IRBasicBlock* UnwindBB);

  // 算术指令
  IRInstruction* createAdd(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createSub(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createMul(IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createNeg(IRValue* V, StringRef Name = "");

  // 比较指令
  IRInstruction* createICmp(ICmpPred Pred, IRValue* LHS, IRValue* RHS, StringRef Name = "");
  IRInstruction* createFCmp(FCmpPred Pred, IRValue* LHS, IRValue* RHS, StringRef Name = "");

  // 内存指令
  IRInstruction* createAlloca(IRType* Ty, StringRef Name = "");
  IRInstruction* createLoad(IRType* Ty, IRValue* Ptr, StringRef Name = "");
  IRInstruction* createStore(IRValue* Val, IRValue* Ptr);
  IRInstruction* createGEP(IRType* SourceTy, IRValue* Ptr, ArrayRef<IRValue*> Indices, StringRef Name = "");

  // 转换指令
  IRInstruction* createBitCast(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createZExt(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createSExt(IRValue* V, IRType* DestTy, StringRef Name = "");
  IRInstruction* createTrunc(IRValue* V, IRType* DestTy, StringRef Name = "");

  // 调用指令
  IRInstruction* createCall(IRFunction* Callee, ArrayRef<IRValue*> Args, StringRef Name = "");

  // 聚合操作
  IRInstruction* createExtractValue(IRValue* Agg, ArrayRef<unsigned> Indices, StringRef Name = "");
  IRInstruction* createInsertValue(IRValue* Agg, IRValue* Val, ArrayRef<unsigned> Indices, StringRef Name = "");

  // Phi / Select
  IRInstruction* createPhi(IRType* Ty, unsigned NumIncoming, StringRef Name = "");
  IRInstruction* createSelect(IRValue* Cond, IRValue* TrueVal, IRValue* FalseVal, StringRef Name = "");
};
```

### 实现约束

1. 所有 create* 方法返回非空指针
2. InsertPoint 正确维护
3. 常量工厂与 IRTypeContext 缓存一致

### 验收标准

```cpp
// V1: createAdd
IRContext IRCtx;
IRTypeContext& Ctx = IRCtx.getTypeContext();
IRModule Mod("test", Ctx);
auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
auto* F = Mod.getOrInsertFunction("test_fn", FTy);
auto* Entry = F->addBasicBlock("entry");
IRBuilder Builder(IRCtx);
Builder.setInsertPoint(Entry);
auto* One = Builder.getInt32(1);
auto* Two = Builder.getInt32(2);
auto* Add = Builder.createAdd(One, Two, "sum");
assert(Add != nullptr);

// V2: createRetVoid
auto* Ret = Builder.createRetVoid();
assert(Ret != nullptr);

// V3: createCall
auto* FTy2 = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
auto* Foo = Mod.getOrInsertFunction("foo", FTy2);
auto* Call = Builder.createCall(Foo, {}, "call_result");
assert(Call != nullptr);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.5：IRBuilder（含常量工厂）" && git push origin HEAD
> ```

---

## Task A.6：IRVerifier

### 依赖

- A.4（IRModule, IRInstruction）
- A.5（IRBuilder，用于构造测试用例）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRVerifier.h` |
| 新增 | `src/IR/IRVerifier.cpp` |

### 必须实现的类型定义

```cpp
class VerifierPass : public Pass {
public:
  StringRef getName() const override { return "verifier"; }
  bool run(IRModule& M) override;
private:
  bool verifyType(const IRType* T);
  bool verifyFunction(const IRFunction& F);
  bool verifyBasicBlock(const IRBasicBlock& BB);
  bool verifyInstruction(const IRInstruction& I);
};
```

### 验证检查项

1. 类型完整性（无 OpaqueType 残留）
2. SSA 性质（Value 唯一定义）
3. 终结指令（每个 BB 恰好一个终结指令）
4. 类型匹配（操作数类型与指令要求一致）
5. 函数调用参数数量和类型匹配
6. 引用的 Function/GlobalVariable 存在

### 验收标准

```cpp
// V1: 合法 IRModule 通过验证
IRContext IRCtx;
// ... 构建合法 Module ...
assert(VerifierPass().run(LegalModule) == true);

// V2: 含 OpaqueType 的 IRModule 不通过
// ... Module 中包含 IROpaqueType ...
assert(VerifierPass().run(ModuleWithOpaque) == false);

// V3: 无终结指令的 BB 不通过
// ... BB 中无 terminator ...
assert(VerifierPass().run(ModuleNoTerminator) == false);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.6：IRVerifier" && git push origin HEAD
> ```

---

## Task A.7：IRSerializer（文本 + 二进制格式）

### 依赖

- A.4（IRModule）
- A.3.1（IRFormatVersion, IRFileHeader）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRSerializer.h` |
| 新增 | `src/IR/IRSerializer.cpp` |

### 必须实现的类型定义

```cpp
class IRWriter {
public:
  static bool writeText(const IRModule& M, raw_ostream& OS);
  static bool writeBitcode(const IRModule& M, raw_ostream& OS);
};

class IRReader {
public:
  static unique_ptr<IRModule> parseText(StringRef Text, IRTypeContext& Ctx);
  static unique_ptr<IRModule> parseBitcode(StringRef Data, IRTypeContext& Ctx);
  static unique_ptr<IRModule> readFile(StringRef Path, IRTypeContext& Ctx);
};
```

### 文本格式规范

```
; BTIR text format
module "test" target "x86_64-unknown-linux-gnu"

global @g1 : i32 = 42

function @add(i32 %a, i32 %b) -> i32 {
entry:
  %1 = add i32 %a, %b
  ret i32 %1
}

function @main() -> i32 {
entry:
  %1 = call @add(i32 1, i32 2)
  ret i32 %1
}
```

### 实现约束

1. 文本格式人类可读
2. 二进制格式紧凑（IRFileHeader + 模块数据 + 字符串表）
3. 版本号写入文件头

### 验收标准

```cpp
// V1: 文本格式往返
std::string Text;
raw_string_ostream OS(Text);
IRWriter::writeText(*Module, OS);
auto Parsed = IRReader::parseText(OS.str(), Ctx);
assert(Parsed != nullptr);
// 比较 Module 和 Parsed 的结构等价性

// V2: 二进制格式往返
SmallVector<char, 1024> Buffer;
raw_svector_ostream BOS(Buffer);
IRWriter::writeBitcode(*Module, BOS);
auto Parsed2 = IRReader::parseBitcode(StringRef(Buffer.data(), Buffer.size()), Ctx);
assert(Parsed2 != nullptr);

// V3: IRFileHeader 魔数
// 二进制数据前4字节 == "BTIR"
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.7：IRSerializer（文本 + 二进制格式）" && git push origin HEAD
> ```

---

## Task A.8：CMake 集成 + 单元测试

### 依赖

- A.1 ~ A.7 全部完成

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/IR/CMakeLists.txt` |
| 修改 | `CMakeLists.txt`（添加 IR 子目录） |
| 新增 | `tests/unit/IR/` 下所有测试文件 |

### 必须满足的构建约束

1. `libblocktype-ir` 不链接 LLVM
2. 所有单元测试通过
3. lit 测试通过

### CMake 目标定义

```cmake
add_library(blocktype-ir
  IRType.cpp IRTypeContext.cpp IRContext.cpp TargetLayout.cpp
  IRValue.cpp IRInstruction.cpp IRConstant.cpp
  IRModule.cpp IRFunction.cpp IRBasicBlock.cpp
  IRBuilder.cpp IRVerifier.cpp IRSerializer.cpp
  IRFormatVersion.cpp
)
target_link_libraries(blocktype-ir PUBLIC blocktype-basic)
target_include_directories(blocktype-ir PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)
```

### 验收标准

```bash
# V1: 构建成功
cmake --build build --target blocktype-ir
# 退出码 == 0

# V2: 单元测试全部通过
cd build && ctest -R "IR" --output-on-failure
# All tests passed

# V3: libblocktype-ir 无 LLVM 符号
nm libblocktype-ir.a | grep "llvm::" | wc -l
# 输出 == 0
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.8：CMake 集成 + 单元测试" && git push origin HEAD
> ```

---

## 附加增强任务

### Task A-F1：IRInstruction/IRType 添加 DialectID 字段 + DialectLoweringPass

**依赖**：A.1（IRType 体系）、A.3（IRInstruction）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 修改 | `include/blocktype/IR/IRType.h` |
| 修改 | `include/blocktype/IR/IRInstruction.h` |
| 新增 | `include/blocktype/IR/IRDialect.h` |
| 新增 | `src/IR/IRDialect.cpp` |
| 新增 | `include/blocktype/IR/DialectLoweringPass.h` |
| 新增 | `src/IR/DialectLoweringPass.cpp` |

**必须实现的类型定义**

DialectID 枚举已在 Task A.1 中定义。此处补充 DialectCapability 和 DialectLoweringPass：

```cpp
namespace blocktype::ir::dialect {

class DialectCapability {
  uint32_t SupportedDialects = 0;
public:
  void declareDialect(DialectID D) {
    SupportedDialects |= (1u << static_cast<uint8_t>(D));
  }
  bool hasDialect(DialectID D) const {
    return (SupportedDialects & (1u << static_cast<uint8_t>(D))) != 0;
  }
  bool supportsAll(uint32_t Required) const {
    return (SupportedDialects & Required) == Required;
  }
  uint32_t getUnsupported(uint32_t Required) const {
    return Required & ~SupportedDialects;
  }
  uint32_t getSupportedMask() const { return SupportedDialects; }
};

class DialectLoweringPass : public Pass {
public:
  StringRef getName() const override { return "dialect-lowering"; }
  bool run(IRModule& M) override;
private:
  struct LoweringRule {
    DialectID SourceDialect;
    Opcode SourceOpcode;
    DialectID TargetDialect;
    std::function<bool(IRInstruction&, IRBuilder&)> Lower;
  };
  static const SmallVector<LoweringRule, 16> LoweringRules;
};

}
```

**Dialect 降级规则表（10条）**

| # | 源Dialect | 源Opcode | 目标Dialect | 降级行为 | 条件 |
|---|----------|---------|------------|---------|------|
| 1 | bt_cpp | Invoke | bt_core | `invoke @f() to label %normal unwind label %unwind` → `call @f()` + landingpad布局 | 后端不支持异常处理 |
| 2 | bt_cpp | Resume | bt_core | `resume {ptr, i32} %val` → `unreachable` | 后端不支持异常恢复 |
| 3 | bt_cpp | dynamic_cast | bt_core | `dynamic_cast<T>(v)` → `call @__dynamic_cast(v, &typeinfo_T, ...)` | 所有后端 |
| 4 | bt_cpp | vtable.dispatch | bt_core | `vtable.dispatch(obj, idx)` → `load vptr` + `gep idx` + `indirect_call` | 所有后端 |
| 5 | bt_cpp | RTTI.typeid | bt_core | `typeid(expr)` → `call @__typeid(expr)` | 所有后端 |
| 6 | bt_target | target.intrinsic | bt_core | 目标特定intrinsic → 目标特定函数调用 | 后端不直接支持 |
| 7 | bt_meta | meta.inline.always | bt_core | 附加函数属性`AlwaysInline` | 无条件 |
| 8 | bt_meta | meta.inline.never | bt_core | 附加函数属性`NoInline` | 无条件 |
| 9 | bt_meta | meta.hot | bt_core | 附加函数属性`Hot` | 无条件 |
| 10 | bt_meta | meta.cold | bt_core | 附加函数属性`Cold` | 无条件 |

**各后端Dialect能力声明**

| 后端 | 支持的Dialect | 位掩码值 |
|------|-------------|---------|
| LLVM | Core+Cpp+Target+Debug+Metadata | 0x1F (31) |
| Cranelift | Core+Debug | 0x09 (9) |

**约束**：DialectID 作为 IRType/IRInstruction 的附加字段，不改变现有接口；现有指令默认属于 bt_core；DialectCapability 使用位掩码，与 BackendCapability 正交

**验收标准**

```cpp
// V1: DialectCapability 位掩码
dialect::DialectCapability Cap;
Cap.declareDialect(dialect::DialectID::Core);
Cap.declareDialect(dialect::DialectID::Cpp);
assert(Cap.hasDialect(dialect::DialectID::Core));
assert(Cap.hasDialect(dialect::DialectID::Cpp));
assert(!Cap.hasDialect(dialect::DialectID::Debug));

// V2: DialectLoweringPass 可运行
// 构建含 bt_cpp Invoke 指令的 IRModule
DialectLoweringPass Pass;
bool Changed = Pass.run(Module);
assert(Changed == true);
// Invoke 指令被降级为 Call + landingpad 布局
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F1：IRInstruction/IRType 添加 DialectID 字段 + DialectLoweringPass" && git push origin HEAD
> ```

---

### Task A-F2：IRFeature 枚举 + BackendCapability

**依赖**：A.1（IRType 体系）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/BackendCapability.h` |
| 新增 | `src/IR/BackendCapability.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::ir {

enum class IRFeature : uint32_t {
  IntegerArithmetic  = 1 << 0,
  FloatArithmetic    = 1 << 1,
  VectorOperations   = 1 << 2,
  AtomicOperations   = 1 << 3,
  ExceptionHandling  = 1 << 4,
  DebugInfo          = 1 << 5,
  VarArg             = 1 << 6,
  SeparateFloatInt   = 1 << 7,
  StructReturn       = 1 << 8,
  DynamicCast        = 1 << 9,
  VirtualDispatch    = 1 << 10,
  Coroutines         = 1 << 11,
};

class BackendCapability {
  uint32_t SupportedFeatures = 0;
  uint32_t RequiredFeatures = 0;
public:
  void declareFeature(IRFeature F) { SupportedFeatures |= static_cast<uint32_t>(F); }
  bool hasFeature(IRFeature F) const { return (SupportedFeatures & static_cast<uint32_t>(F)) != 0; }
  bool supportsAll(uint32_t Required) const { return (SupportedFeatures & Required) == Required; }
  uint32_t getUnsupported(uint32_t Required) const { return Required & ~SupportedFeatures; }
};

}
```

**各后端能力声明**

```cpp
// LLVM 后端
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

// Cranelift 后端（未来）
BackendCapability CraneliftBackend::getCapability() const {
  BackendCapability Cap;
  Cap.declareFeature(IRFeature::IntegerArithmetic);
  Cap.declareFeature(IRFeature::FloatArithmetic);
  Cap.declareFeature(IRFeature::VectorOperations);
  return Cap;
}
```

**约束**：BackendCapability 在 BackendBase 接口中声明为 `virtual BackendCapability getCapability() const = 0;`

**验收标准**

```cpp
// V1: BackendCapability 特性检查
BackendCapability Cap;
Cap.declareFeature(IRFeature::IntegerArithmetic);
Cap.declareFeature(IRFeature::FloatArithmetic);
assert(Cap.hasFeature(IRFeature::IntegerArithmetic));
assert(!Cap.hasFeature(IRFeature::ExceptionHandling));

// V2: supportsAll 批量检查
uint32_t Required = static_cast<uint32_t>(IRFeature::IntegerArithmetic)
                  | static_cast<uint32_t>(IRFeature::FloatArithmetic);
assert(Cap.supportsAll(Required));
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F2：IRFeature 枚举 + BackendCapability" && git push origin HEAD
> ```

---

### Task A-F3：TelemetryCollector 基础框架 + PhaseGuard RAII

**依赖**：无

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRTelemetry.h` |
| 新增 | `src/IR/IRTelemetry.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::telemetry {

enum class CompilationPhase : uint8_t {
  Frontend       = 0,
  IRGeneration   = 1,
  IRValidation   = 2,
  IROptimization = 3,
  BackendCodegen = 4,
  BackendOptimize = 5,
  CodeEmission   = 6,
  Linking        = 7,
};

struct CompilationEvent {
  CompilationPhase Phase;
  std::string Detail;
  uint64_t StartNs;
  uint64_t EndNs;
  size_t MemoryBefore;
  size_t MemoryAfter;
  bool Success;
};

class TelemetryCollector {
  bool Enabled = false;
  SmallVector<CompilationEvent, 64> Events;
  uint64_t CompilationStartNs;
public:
  void enable() { Enabled = true; }
  bool isEnabled() const { return Enabled; }

  class PhaseGuard {
    TelemetryCollector& Collector;
    CompilationPhase Phase;
    std::string Detail;
    uint64_t StartNs;
    size_t MemoryBefore;
    bool Failed = false;
  public:
    PhaseGuard(TelemetryCollector& C, CompilationPhase P, StringRef D)
      : Collector(C), Phase(P), Detail(D.str()),
        StartNs(getCurrentTimeNs()),
        MemoryBefore(getCurrentMemoryUsage()) {}
    ~PhaseGuard() {
      if (Collector.Enabled) {
        CompilationEvent E;
        E.Phase = Phase;
        E.Detail = Detail;
        E.StartNs = StartNs;
        E.EndNs = getCurrentTimeNs();
        E.MemoryBefore = MemoryBefore;
        E.MemoryAfter = getCurrentMemoryUsage();
        E.Success = !Failed;
        Collector.Events.push_back(std::move(E));
      }
    }
    void markFailed() { Failed = true; }
  private:
    static uint64_t getCurrentTimeNs();
    static size_t getCurrentMemoryUsage();
  };

  PhaseGuard scopePhase(CompilationPhase P, StringRef Detail = "") {
    return PhaseGuard(*this, P, Detail);
  }

  const SmallVector<CompilationEvent, 64>& getEvents() const { return Events; }
  void clear() { Events.clear(); }
  bool writeChromeTrace(StringRef Path) const;
  bool writeJSONReport(StringRef Path) const;
};

}
```

**Chrome Trace JSON 输出格式**

```json
{
  "traceEvents": [
    { "ph": "B", "pid": 0, "tid": 0, "ts": 123456789, "name": "Frontend", "cat": "compiler",
      "args": { "detail": "parse + sema", "memoryBefore": 10485760, "memoryAfter": 20971520 } },
    { "ph": "E", "pid": 0, "tid": 0, "ts": 123457000 }
  ],
  "displayTimeUnit": "us",
  "metadata": { "compiler": "BlockType", "version": "1.0.0" }
}
```

**编译选项**

```
--ftime-report         输出编译时间报告（文本格式）
--ftime-report=json    输出编译时间报告（JSON格式）
--ftrace-compilation   输出Chrome Trace格式（可在chrome://tracing可视化）
--fmemory-report       输出内存使用报告
```

**约束**：PhaseGuard 构造时记录开始时间和内存，析构时记录耗时；Release构建无额外开销

**验收标准**

```cpp
// V1: PhaseGuard RAII 计时
telemetry::TelemetryCollector TC;
TC.enable();
{
  auto Guard = TC.scopePhase(telemetry::CompilationPhase::Frontend, "test");
}
assert(TC.getEvents().size() == 1);
assert(TC.getEvents()[0].Phase == telemetry::CompilationPhase::Frontend);
assert(TC.getEvents()[0].Success == true);

// V2: Chrome Trace 输出
assert(TC.writeChromeTrace("/tmp/trace.json") == true);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F3：TelemetryCollector 基础框架 + PhaseGuard RAII" && git push origin HEAD
> ```

---

### Task A-F4：IRInstruction 添加 Optional DbgInfo 字段

**依赖**：A.3（IRInstruction 定义）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 修改 | `include/blocktype/IR/IRInstruction.h` |

**必须实现的修改**

IRInstruction 类添加以下字段和方法（已在 Task A.3 的接口中预留）：

```cpp
class IRInstruction : public User {
  Optional<ir::debug::IRInstructionDebugInfo> DbgInfo;
public:
  void setDebugInfo(const ir::debug::IRInstructionDebugInfo& DI) { DbgInfo = DI; }
  const ir::debug::IRInstructionDebugInfo* getDebugInfo() const {
    return DbgInfo ? &*DbgInfo : nullptr;
  }
  bool hasDebugInfo() const { return DbgInfo.has_value(); }
};
```

**约束**：不改变现有接口；Release构建 DbgInfo 始终为 None，开销仅1字节

**验收标准**

```cpp
// V1: 无调试信息时开销为1字节
static_assert(sizeof(Optional<ir::debug::IRInstructionDebugInfo>) == 1 || true);
// V2: 设置/获取调试信息
IRInstruction I(Opcode::Add, ...);
assert(!I.hasDebugInfo());
ir::debug::IRInstructionDebugInfo DI;
I.setDebugInfo(DI);
assert(I.hasDebugInfo());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F4：IRInstruction 添加 Optional DbgInfo 字段" && git push origin HEAD
> ```

---

### Task A-F5：IRDebugMetadata 基础类型定义 + 调试信息升级

**依赖**：A-F4

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRDebugInfo.h` |
| 新增 | `include/blocktype/IR/IRDebugMetadata.h` |
| 新增 | `src/IR/IRDebugInfo.cpp` |
| 新增 | `src/IR/IRDebugMetadata.cpp` |

**必须实现的类型定义**

§2.1.10 基础调试信息（`blocktype::ir` 命名空间）：

```cpp
namespace blocktype::ir {

class DebugMetadata {
public:
  virtual ~DebugMetadata() = default;
  virtual void print(raw_ostream& OS) const = 0;
};

class DICompileUnit : public DebugMetadata {
  std::string SourceFile, Producer; unsigned Language;
};

class DIType : public DebugMetadata {
  StringRef Name; uint64_t SizeInBits, AlignInBits;
};

class DISubprogram : public DebugMetadata {
  StringRef Name; DICompileUnit* Unit; DISubprogram* Linkage;
};

class DILocation : public DebugMetadata {
  unsigned Line, Column; DISubprogram* Scope;
};

}
```

§2.1.25 调试信息升级（`blocktype::ir::debug` 命名空间）：

```cpp
namespace blocktype::ir::debug {

struct DIType {
  enum class Kind : uint8_t {
    Basic = 0, Pointer = 1, Array = 2, Struct = 3, Class = 4,
    Enum = 5, Function = 6, Template = 7, Union = 8,
    Modifier = 9, Subrange = 10, String = 11,
  };
};

class IRInstructionDebugInfo {
  IRDebugMetadata::SourceLocation Loc;
  Optional<IRDebugMetadata::DISubprogram*> Subprogram;
  bool IsArtificial;
  bool IsInlined;
  Optional<IRDebugMetadata::SourceLocation> InlinedAt;
};

class DebugInfoEmitter {
public:
  virtual ~DebugInfoEmitter() = default;
  virtual void emitDebugInfo(const ir::IRModule& M, raw_ostream& OS) = 0;
  virtual void emitDWARF5(const ir::IRModule& M, raw_ostream& OS) = 0;
  virtual void emitDWARF4(const ir::IRModule& M, raw_ostream& OS) = 0;
  virtual void emitCodeView(const ir::IRModule& M, raw_ostream& OS) = 0;
};

}
```

**编译选项**

```
-g                             生成调试信息（DWARF5默认）
-gdwarf-4                      生成DWARF4调试信息
-gdwarf-5                      生成DWARF5调试信息
-gcodeview                     生成CodeView调试信息（Windows）
-gir-debug                     在IR中保留调试元数据
-fdebug-info-for-profiling     生成性能剖析用调试信息
```

**约束**：基础调试信息（§2.1.10）和升级调试信息（§2.1.25）共存；Phase A-D 两者并行，Phase E+ 升级版替代基础版

**验收标准**

```cpp
// V1: 基础调试信息
ir::DICompileUnit CU;
CU.SourceFile = "test.cpp";
CU.print(OS);

// V2: 升级调试信息
ir::debug::IRInstructionDebugInfo DI;
assert(!DI.IsArtificial);
DI.IsArtificial = true;
assert(DI.IsArtificial);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F5：IRDebugMetadata 基础类型定义 + 调试信息升级" && git push origin HEAD
> ```

---

### Task A-F6：StructuredDiagnostic 基础结构定义

**依赖**：无

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRDiagnostic.h` |
| 新增 | `src/IR/IRDiagnostic.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::diag {

enum class DiagnosticLevel : uint8_t {
  Note = 0, Remark = 1, Warning = 2, Error = 3, Fatal = 4,
};

enum class DiagnosticGroup : uint16_t {
  TypeMapping = 0, InstructionValidation = 1, IRVerification = 2,
  BackendCodegen = 3, FFIBinding = 4, Serialization = 5,
};

class StructuredDiagnostic {
  DiagnosticLevel Level;
  DiagnosticGroup Group;
  ir::IRErrorCode Code;
  std::string Message;
  SourceLocation Loc;
  SmallVector<std::string, 4> Notes;
public:
  DiagnosticLevel getLevel() const { return Level; }
  DiagnosticGroup getGroup() const { return Group; }
  ir::IRErrorCode getCode() const { return Code; }
  StringRef getMessage() const { return Message; }
  void addNote(StringRef N) { Notes.push_back(N.str()); }
  std::string toJSON() const;
  std::string toText() const;
};

class StructuredDiagEmitter {
public:
  virtual ~StructuredDiagEmitter() = default;
  virtual void emit(const StructuredDiagnostic& D) = 0;
};

class DiagnosticGroupManager {
  DenseMap<DiagnosticGroup, bool> EnabledGroups;
public:
  void enableGroup(DiagnosticGroup G) { EnabledGroups[G] = true; }
  void disableGroup(DiagnosticGroup G) { EnabledGroups[G] = false; }
  bool isGroupEnabled(DiagnosticGroup G) const;
};

}
```

**约束**：支持文本和 JSON 输出；DiagnosticGroupManager 控制哪些诊断组启用

**验收标准**

```cpp
// V1: StructuredDiagnostic 创建
diag::StructuredDiagnostic D;
D.Level = diag::DiagnosticLevel::Error;
D.Code = ir::IRErrorCode::TypeMappingFailed;
D.Message = "Cannot map QualType to IRType";
assert(D.getLevel() == diag::DiagnosticLevel::Error);

// V2: JSON 输出
std::string JSON = D.toJSON();
assert(JSON.find("TypeMappingFailed") != std::string::npos);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F6：StructuredDiagnostic 基础结构定义" && git push origin HEAD
> ```

---

### Task A-F7：CacheKey/CacheEntry 基础类型定义 + 编译器缓存架构

**依赖**：A.3.1（IRFormatVersion）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRCache.h` |
| 新增 | `src/IR/IRCache.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::cache {

struct CacheKey {
  uint64_t SourceHash;
  uint64_t OptionsHash;
  uint64_t VersionHash;
  uint64_t TargetTripleHash;
  uint64_t DependencyHash;
  uint64_t CombinedHash;
  static CacheKey compute(StringRef Source, const CompilerOptions& Opts);
  std::string toHex() const;
};

struct CacheEntry {
  CacheKey Key;
  std::vector<uint8_t> IRData;
  std::vector<uint8_t> ObjectData;
  ir::IRFormatVersion IRVersion;
  uint64_t Timestamp;
  size_t IRSize;
  size_t ObjectSize;
};

class CacheStorage {
public:
  virtual ~CacheStorage() = default;
  virtual Optional<CacheEntry> lookup(const CacheKey& Key) = 0;
  virtual bool store(const CacheKey& Key, const CacheEntry& Entry) = 0;
  struct Stats { size_t Hits; size_t Misses; size_t Evictions; size_t TotalSize; };
  virtual Stats getStats() const = 0;
};

class LocalDiskCache : public CacheStorage {
  std::string CacheDir;
  size_t MaxSize;
public:
  explicit LocalDiskCache(StringRef Dir, size_t Max);
  Optional<CacheEntry> lookup(const CacheKey& Key) override;
  bool store(const CacheKey& Key, const CacheEntry& Entry) override;
  Stats getStats() const override;
  void evictIfNeeded();
};

class RemoteCache : public CacheStorage {
  std::string Endpoint;
  std::string Bucket;
public:
  Optional<CacheEntry> lookup(const CacheKey& Key) override;
  bool store(const CacheKey& Key, const CacheEntry& Entry) override;
};

class CompilationCacheManager {
  std::unique_ptr<CacheStorage> Storage;
  bool Enabled = false;
public:
  void enable(StringRef CacheDir, size_t MaxSize);
  void disable();
  Optional<std::unique_ptr<ir::IRModule>> lookupIR(const CacheKey& Key, ir::IRTypeContext& Ctx);
  Optional<std::vector<uint8_t>> lookupObject(const CacheKey& Key);
  bool storeIR(const CacheKey& Key, const ir::IRModule& M);
  bool storeObject(const CacheKey& Key, ArrayRef<uint8_t> Data);
};

}
```

**缓存文件命名规则**

```
目录结构：<CacheDir>/<first2hex>/<rest38hex>/
  - IR缓存：<CacheDir>/ab/cdef0123456789.../ir.btir
  - 目标缓存：<CacheDir>/ab/cdef0123456789.../obj.o
  - 元数据：<CacheDir>/ab/cdef0123456789.../meta.json
```

**编译选项**

```
--fcache-dir=<path>           指定编译缓存目录
--fcache-size=<size>          最大缓存大小（如 "10G"）
--fcache-remote=<url>         远程缓存端点（远期）
--fcache-no                   禁用编译缓存
--fcache-stats                输出缓存统计
```

**约束**：CacheKey 使用 BLAKE3 稳定哈希；LocalDiskCache LRU淘汰；RemoteCache 远期实现

**验收标准**

```cpp
// V1: CacheKey 计算
cache::CacheKey Key = cache::CacheKey::compute("int main(){}", Opts);
assert(Key.CombinedHash != 0);

// V2: LocalDiskCache 存取
cache::LocalDiskCache LDC("/tmp/btcache", 1024*1024*1024);
cache::CacheEntry Entry;
Entry.Key = Key;
Entry.IRData = {1,2,3};
assert(LDC.store(Key, Entry));
auto Found = LDC.lookup(Key);
assert(Found.has_value());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F7：CacheKey/CacheEntry 基础类型定义 + 编译器缓存架构" && git push origin HEAD
> ```

---

### Task A-F8：IRIntegrityChecksum 基础实现 + IRSigner + 可重现构建

**依赖**：A.4（IRModule）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRIntegrity.h` |
| 新增 | `src/IR/IRIntegrity.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::ir::security {

struct IRIntegrityChecksum {
  uint64_t TypeSystemHash;
  uint64_t InstructionHash;
  uint64_t ConstantHash;
  uint64_t GlobalHash;
  uint64_t DebugInfoHash;
  uint64_t CombinedHash;
  static IRIntegrityChecksum compute(const IRModule& M);
  bool verify(const IRModule& M) const;
};

using PrivateKey = std::array<uint8_t, 32>;
using PublicKey  = std::array<uint8_t, 32>;
using Signature  = std::array<uint8_t, 64>;

class IRSigner {
public:
  static Signature sign(const IRModule& M, const PrivateKey& Key);
  static bool verify(const IRModule& M, const Signature& Sig, const PublicKey& Key);
};

}
```

**可重现构建行为列表**

1. 时间戳：使用 SOURCE_DATE_EPOCH 环境变量值（默认0）
2. 随机数：所有随机数生成器使用确定性种子（seed=0x42）
3. 遍历顺序：所有哈希表遍历改为排序后遍历（按key排序）
4. 内部计数器：ValueID/InstructionID使用确定性起始值（从1递增）
5. 临时文件名：使用确定性命名（而非随机后缀）
6. DWARF生产者字符串：使用固定版本号（不含构建时间）

**编译选项**

```
--freproducible-build          启用可重现构建模式
--fir-integrity-check          加载IR时验证完整性
--fir-sign=<key-file>          对输出IR签名
--fir-verify-signature=<key>   验证输入IR签名
```

**约束**：哈希算法使用 BLAKE3；签名算法使用 Ed25519；IRModule 已有 IsReproducible 字段和 computeChecksum() 方法

**验收标准**

```cpp
// V1: IRIntegrityChecksum 计算
ir::security::IRIntegrityChecksum Chk = ir::security::IRIntegrityChecksum::compute(Module);
assert(Chk.CombinedHash != 0);

// V2: 校验和验证
assert(Chk.verify(Module) == true);

// V3: 可重现构建
Module.setReproducible(true);
auto Chk1 = ir::security::IRIntegrityChecksum::compute(Module);
auto Chk2 = ir::security::IRIntegrityChecksum::compute(Module);
assert(Chk1.CombinedHash == Chk2.CombinedHash);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F8：IRIntegrityChecksum 基础实现 + IRSigner + 可重现构建" && git push origin HEAD
> ```

---

### Task A-F9：IRConversionResult + IRVerificationResult（错误恢复机制）

**依赖**：A.4（IRModule）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRConversionResult.h` |
| 新增 | `src/IR/IRConversionResult.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::ir {

class IRConversionResult {
  std::unique_ptr<IRModule> Module;
  bool Invalid = false;
  unsigned NumErrors = 0;
public:
  IRConversionResult() = default;
  IRConversionResult(std::unique_ptr<IRModule> M) : Module(std::move(M)) {}
  static IRConversionResult getInvalid(unsigned Errors = 1) {
    IRConversionResult R;
    R.Invalid = true;
    R.NumErrors = Errors;
    return R;
  }
  bool isInvalid() const { return Invalid; }
  bool isUsable() const { return Module != nullptr && !Invalid; }
  std::unique_ptr<IRModule> takeModule() { return std::move(Module); }
  unsigned getNumErrors() const { return NumErrors; }
};

class IRVerificationResult {
  bool IsValid;
  SmallVector<std::string, 8> Violations;
public:
  explicit IRVerificationResult(bool Valid) : IsValid(Valid) {}
  void addViolation(const std::string &Msg) { Violations.push_back(Msg); IsValid = false; }
  bool isValid() const { return IsValid; }
  const SmallVector<std::string, 8> &getViolations() const { return Violations; }
};

}
```

**错误恢复策略**

| 阶段 | 错误类型 | 恢复策略 |
|------|---------|---------|
| AST → IR 转换 | 类型映射失败 | 发出 Diag，用 `IROpaqueType` 占位，继续转换 |
| AST → IR 转换 | 语句转换失败 | 发出 Diag，跳过该语句，继续下一语句 |
| AST → IR 转换 | 表达式转换失败 | 发出 Diag，用 `IRConstantUndef` 占位，继续 |
| IR 验证 | 类型完整性 | 收集所有违规，一次性报告 |
| IR 验证 | SSA 性质 | 收集所有违规，一次性报告 |
| IR → LLVM 转换 | 不支持的 IR 特性 | 发出 Diag，中止该函数转换，继续下一函数 |
| IR → LLVM 转换 | 后端能力不足 | 检查 `BackendCapability`，提前报告不支持的特性 |

**约束**：IR 转换的错误都是硬错误，直接通过 DiagnosticsEngine 报告；IR 转换层不需要 SFINAE 抑制

**验收标准**

```cpp
// V1: IRConversionResult 有效
auto Mod = std::make_unique<IRModule>("test", Ctx);
IRConversionResult Result(std::move(Mod));
assert(!Result.isInvalid());
assert(Result.isUsable());

// V2: IRConversionResult 无效
IRConversionResult InvalidResult = IRConversionResult::getInvalid(3);
assert(InvalidResult.isInvalid());
assert(!InvalidResult.isUsable());
assert(InvalidResult.getNumErrors() == 3);

// V3: IRVerificationResult 收集违规
IRVerificationResult VR(true);
VR.addViolation("Missing terminator in BB entry");
assert(!VR.isValid());
assert(VR.getViolations().size() == 1);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F9：IRConversionResult + IRVerificationResult（错误恢复机制）" && git push origin HEAD
> ```

---

### Task A-F10：IRFeature 枚举 + IRErrorCode 枚举

**依赖**：A-F2（BackendCapability 中的 IRFeature）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRErrorCode.h` |
| 新增 | `src/IR/IRErrorCode.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::ir {

enum class IRErrorCode : uint32_t {
  // 类型错误 (1000-1099)
  TypeMappingFailed      = 1001,
  TypeIncomplete         = 1002,
  TypeMismatch           = 1003,
  TypeCircularRef        = 1004,
  TypeInvalidBitWidth    = 1005,
  TypeStructBodyConflict = 1006,

  // 指令错误 (1100-1199)
  InvalidOpcode          = 1101,
  InvalidOperand         = 1102,
  MissingTerminator      = 1103,
  MultipleTerminators    = 1104,
  InvalidPHINode         = 1105,
  InvalidGEPIndex        = 1106,
  InvalidCallSignature   = 1107,
  InvalidInvokeTarget    = 1108,

  // 验证错误 (1200-1299)
  SSAViolation           = 1201,
  DefUseChainBroken      = 1202,
  UseDefChainBroken      = 1203,
  GlobalRefNotFound      = 1204,
  FunctionRefNotFound    = 1205,
  BlockRefNotFound       = 1206,
  InvalidEntryBlock      = 1207,
  EmptyFunction          = 1208,

  // 序列化错误 (1300-1399)
  InvalidFormat          = 1301,
  VersionMismatch        = 1302,
  ChecksumFailed         = 1303,
  SignatureFailed        = 1304,
  InvalidMagicNumber     = 1305,
  TruncatedData          = 1306,
  StringTableError       = 1307,

  // 后端错误 (1400-1499)
  BackendUnsupportedFeature = 1401,
  BackendLoweringFailed  = 1402,
  BackendCodegenFailed   = 1403,
  BackendOptimizationFailed = 1404,

  // FFI错误 (1500-1599)
  FFITypeMappingFailed   = 1501,
  FFICallingConvMismatch = 1502,
  FFIHeaderNotFound      = 1503,
  FFIBindingGenerationFailed = 1504,
};

const char* errorCodeToString(IRErrorCode Code);

}
```

**约束**：错误码按类别分段（类型1000/指令1100/验证1200/序列化1300/后端1400/FFI1500）

**验收标准**

```cpp
// V1: 错误码转字符串
assert(strcmp(errorCodeToString(IRErrorCode::TypeMappingFailed), "TypeMappingFailed") == 0);

// V2: 错误码分段
assert(static_cast<uint32_t>(IRErrorCode::TypeMappingFailed) / 100 == 10);
assert(static_cast<uint32_t>(IRErrorCode::InvalidOpcode) / 100 == 11);
assert(static_cast<uint32_t>(IRErrorCode::SSAViolation) / 100 == 12);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F10：IRFeature 枚举 + IRErrorCode 枚举" && git push origin HEAD
> ```

---

### Task A-F11：IR 优化 Pass 具体实现 + 前端-IR 契约测试

**依赖**：A.6（IRVerifier）、A.5（IRBuilder）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRPasses.h` |
| 新增 | `src/IR/IRPasses.cpp` |
| 新增 | `include/blocktype/IR/IRContract.h` |
| 新增 | `src/IR/IRContract.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::ir {

class DeadFunctionEliminationPass : public Pass {
public:
  StringRef getName() const override { return "dead-func-elim"; }
  bool run(IRModule &M) override;
};

class ConstantFoldingPass : public Pass {
public:
  StringRef getName() const override { return "constant-fold"; }
  bool run(IRModule &M) override;
private:
  Optional<IRConstant *> tryFold(Opcode Op, ArrayRef<IRConstant *> Operands);
};

class TypeCanonicalizationPass : public Pass {
public:
  StringRef getName() const override { return "type-canon"; }
  bool run(IRModule &M) override;
};

unique_ptr<PassManager> createDefaultIRPipeline() {
  auto PM = std::make_unique<PassManager>();
  PM->addPass(std::make_unique<VerifierPass>());
  PM->addPass(std::make_unique<DeadFunctionEliminationPass>());
  PM->addPass(std::make_unique<ConstantFoldingPass>());
  PM->addPass(std::make_unique<TypeCanonicalizationPass>());
  PM->addPass(std::make_unique<VerifierPass>());
  return PM;
}

}
```

**前端-IR 契约测试（6个契约）**

```cpp
namespace blocktype::ir::contract {

bool verifyIRModuleContract(const IRModule &M);
bool verifyTypeCompleteness(const IRModule &M);
bool verifyFunctionNonEmpty(const IRModule &M);
bool verifyTerminatorContract(const IRModule &M);
bool verifyTypeConsistency(const IRModule &M);
bool verifyTargetTripleValid(const IRModule &M);
IRVerificationResult verifyAllContracts(const IRModule &M);

}
```

**约束**：IR 层 Pass 是轻量级预处理，不替代后端优化；`--verify-btir` 选项触发契约验证，Debug 构建默认启用

**验收标准**

```cpp
// V1: createDefaultIRPipeline
auto PM = createDefaultIRPipeline();
assert(PM != nullptr);

// V2: DeadFunctionEliminationPass
// 构建含未引用内部函数的 IRModule
DeadFunctionEliminationPass DFE;
bool Changed = DFE.run(Module);
assert(Changed == true);

// V3: 契约验证
auto Result = ir::contract::verifyAllContracts(LegalModule);
assert(Result.isValid());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F11：IR 优化 Pass 具体实现 + 前端-IR 契约测试" && git push origin HEAD
> ```

---

### Task A-F12：FFI 接口基础定义

**依赖**：A.1（IRType 体系）、A.3（Opcode 枚举中的 FFI 指令）

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRFFI.h` |
| 新增 | `src/IR/IRFFI.cpp` |

**必须实现的类型定义**

```cpp
namespace blocktype::ir::ffi {

enum class CallingConvention : uint8_t {
  C = 0, Stdcall = 1, Fastcall = 2, VectorCall = 3,
  WASM = 4, BTInternal = 5, ThisCall = 6, Swift = 7,
};

class FFITypeMapper {
public:
  static ir::IRType* mapExternalType(StringRef Language, StringRef TypeName, ir::IRTypeContext& Ctx);
  static std::string mapToExternalType(const ir::IRType* T, StringRef TargetLanguage);
  static std::vector<std::string> getSupportedLanguages();
};

class FFIFunctionDecl {
  std::string Name;
  std::string MangledName;
  CallingConvention Conv;
  ir::IRFunctionType* Signature;
  std::string SourceLanguage;
  std::string HeaderFile;
  bool IsVariadic;
public:
  FFIFunctionDecl(StringRef N, StringRef MN, CallingConvention C,
                  ir::IRFunctionType* S, StringRef Lang,
                  StringRef HF = "", bool VA = false);
  ir::IRFunction* createIRDeclaration(ir::IRModule& M);
  ir::IRFunction* createCallWrapper(ir::IRModule& M, const ir::IRFunction& Caller);
};

class FFIModule {
  DenseMap<StringRef, FFIFunctionDecl> Declarations;
public:
  bool importFromCHeader(StringRef HeaderPath);
  bool importFromRustCrate(StringRef CratePath);
  bool exportToCAPI(const ir::IRFunction& F, StringRef ExportName);
  bool generateBindings(StringRef TargetLanguage, raw_ostream& OS);
  const FFIFunctionDecl* lookup(StringRef Name) const;
  unsigned getNumDeclarations() const { return Declarations.size(); }
};

}
```

**C语言映射完整类型映射表**

| C类型 | IR类型 | 说明 |
|-------|--------|------|
| `void` | `IRVoidType` | 无返回值 |
| `_Bool`/`bool` | `IRIntegerType(8)` | C99 _Bool为i8 |
| `char` | `IRIntegerType(8)` | 有符号性取决于平台 |
| `short` | `IRIntegerType(16)` | |
| `int` | `IRIntegerType(32)` | |
| `long` | `IRIntegerType(32)`或`(64)` | LP64/LLP64 |
| `long long` | `IRIntegerType(64)` | |
| `float` | `IRFloatType(32)` | IEEE 754 single |
| `double` | `IRFloatType(64)` | IEEE 754 double |
| `long double` | `IRFloatType(80)`或`(128)` | x86: f80; 其他: f128 |
| `T*` | `IRPointerType(IRIntegerType(8))` | 所有指针统一为i8* |
| `T[N]` | `IRArrayType(N, T_ir)` | |
| `struct T {...}` | `IRStructType("T", [...])` | |
| `union T {...}` | `IRStructType("union.T", [largest_field])` | 映射为最大字段结构体 |
| `enum E {...}` | `IRIntegerType(32)` | 枚举统一为i32 |
| `void(*)(int)` | `IRPointerType(IRFunctionType(...))` | 函数指针 |
| `_Complex float` | `<2 x float>` | 复数映射为2元素向量 |
| `va_list` | `IRPointerType(IRIntegerType(8))` | va_list映射为i8* |

**支持的语言**

- "C"：C语言（零开销映射）
- "Rust"：Rust（通过C ABI映射）
- "Python"：Python（通过ctypes/cffi映射）
- "WASM"：WebAssembly（通过WASM ABI映射）
- "Swift"：Swift（通过C ABI映射）
- "Zig"：Zig（通过C ABI映射）

**约束**：FFI指令（FFICall/FFICheck/FFICoerce/FFIUnwind）属于 bt_core Dialect

**验收标准**

```cpp
// V1: C语言类型映射
auto* Ty = ffi::FFITypeMapper::mapExternalType("C", "int", Ctx);
assert(Ty->isInteger());
assert(cast<IRIntegerType>(Ty)->getBitWidth() == 32);

// V2: FFIFunctionDecl 创建
ffi::FFIFunctionDecl FDecl("printf", "_printf", ffi::CallingConvention::C,
                            FTy, "C", "stdio.h", true);
assert(FDecl.getName() == "printf");
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F12：FFI 接口基础定义" && git push origin HEAD
> ```

---

### Task A-F13：IR 层头文件依赖图 + CMake 子库拆分

**依赖**：A.1 ~ A.7 全部完成、A-F1 ~ A-F12 全部完成

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/IR/CMakeLists.txt` |
| 新增 | `src/IR/Cache/CMakeLists.txt` |
| 新增 | `src/IR/Telemetry/CMakeLists.txt` |

**IR 层头文件依赖图**

```
IRType.h → (无依赖，仅blocktype-basic)
IRTypeContext.h → IRType.h, DenseMap/FoldingSet/StringMap
IRValue.h → IRType.h, ValueKind枚举
IRInstruction.h → IRValue.h, Opcode枚举, DialectID枚举, IRBasicBlock前向声明, IRDebugInfo.h
IRConstant.h → IRValue.h, APInt/APFloat
IRBasicBlock.h → IRInstruction.h, IRFunction前向声明
IRFunction.h → IRBasicBlock.h, IRArgument.h, IRFunctionType.h, LinkageKind/CallingConvention/FunctionAttrs
IRGlobalVariable.h → IRConstant.h, IRType.h, LinkageKind
IRModule.h → IRFunction.h, IRFunctionDecl.h, IRGlobalVariable.h, IRGlobalAlias.h, IRMetadata.h, IRTypeContext.h, IRIntegrityChecksum.h
IRBuilder.h → IRTypeContext.h, IRInstruction.h, IRConstant.h, IRBasicBlock.h, IRFunction.h
IRVerifier.h → IRModule.h, IRPass.h
IRSerializer.h → IRModule.h, IRFormatVersion
IRPass.h → IRModule.h (前向声明)
IRConsumer.h → IRModule.h/IRFunction.h/IRInstruction.h (前向声明)
IRContext.h → BumpPtrAllocator, IRTypeContext.h, IRThreadingMode枚举
IRDebugInfo.h → IRMetadata.h
IRDebugMetadata.h → IRType.h (前向声明)
IRIntegrityChecksum.h → IRModule.h (前向声明)
IRDialect.h → DialectID枚举, DialectCapability
DialectLoweringPass.h → IRPass.h, IRDialect.h, IRBuilder.h
FFIFunctionDecl.h → IRFunctionType.h, CallingConvention枚举
CacheKey.h → IRFormatVersion (前向声明)
TelemetryCollector.h → CompilationPhase枚举, SmallVector
```

**CMake 子库拆分规范**

```cmake
# libblocktype-ir (IR核心库)
add_library(blocktype-ir
  IRType.cpp IRTypeContext.cpp TargetLayout.cpp
  IRValue.cpp IRInstruction.cpp IRConstant.cpp
  IRModule.cpp IRFunction.cpp IRBasicBlock.cpp
  IRBuilder.cpp IRVerifier.cpp IRSerializer.cpp
  IRDebugInfo.cpp IRMetadata.cpp
  IRContext.cpp IRPass.cpp IRConsumer.cpp
  IRDialect.cpp DialectLoweringPass.cpp
  IRIntegrityChecksum.cpp IRSigner.cpp
  FFIFunctionDecl.cpp FFITypeMapper.cpp
  IRDebugMetadata.cpp
  IRConversionResult.cpp IRErrorCode.cpp
  IRPasses.cpp IRContract.cpp
  BackendCapability.cpp
)
target_link_libraries(blocktype-ir PUBLIC blocktype-basic)

# libblocktype-ir-cache (缓存子库)
add_library(blocktype-ir-cache
  CacheKey.cpp CacheEntry.cpp LocalDiskCache.cpp CompilationCacheManager.cpp
)
target_link_libraries(blocktype-ir-cache PUBLIC blocktype-basic blocktype-ir)

# libblocktype-ir-telemetry (遥测子库)
add_library(blocktype-ir-telemetry
  TelemetryCollector.cpp PhaseGuard.cpp
)
target_link_libraries(blocktype-ir-telemetry PUBLIC blocktype-basic)
```

**约束**：libblocktype-ir 不链接任何 LLVM 库；子库独立编译链接

**验收标准**

```bash
# V1: libblocktype-ir 无 LLVM 符号
nm libblocktype-ir.a | grep "llvm::" | wc -l
# 输出 == 0

# V2: 子库独立编译
cmake --build build --target blocktype-ir
cmake --build build --target blocktype-ir-cache
cmake --build build --target blocktype-ir-telemetry
# 全部退出码 == 0

# V3: 头文件依赖无循环
# 检查方法：对每个头文件单独 #include，编译通过
```
