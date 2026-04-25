# Task A.6（优化版）：IRVerifier + Pass 基类

> 本文档是 Task A.6 的优化版本，替代 `11-AI-Coder-任务流-PhaseA.md` 中第 1502~1561 行的 A.6 定义。
> 本文档自包含，dev-runner 无需查阅其他文档即可编码。

---

## 依赖

- A.1（IRType 体系、IRTypeContext）
- A.2（IRValue、User、Use、Opcode 枚举、ValueKind 枚举）
- A.3（IRInstruction、IRConstant 全系列）
- A.4（IRModule、IRFunction、IRBasicBlock）
- A.5（IRBuilder，用于构造验收测试用例）

---

## 产出文件

| 操作 | 文件路径 | 说明 |
|------|----------|------|
| 新增 | `include/blocktype/IR/IRPass.h` | Pass 抽象基类 |
| 新增 | `include/blocktype/IR/IRVerifier.h` | VerificationDiagnostic + VerifierPass |
| 新增 | `src/IR/IRPass.cpp` | Pass 基类实现（仅析构函数） |
| 新增 | `src/IR/IRVerifier.cpp` | VerifierPass 完整实现 |
| 修改 | `src/IR/CMakeLists.txt` | 添加 IRPass.cpp、IRVerifier.cpp |
| 新增 | `tests/unit/IR/IRVerifierTest.cpp` | VerifierPass 单元测试（V1~V7 验收场景） |
| 修改 | `tests/unit/IR/CMakeLists.txt` | 添加 IRVerifierTest.cpp 测试目标 |

---

## 设计决策说明（4 个问题的解决方案）

### 问题 1：缺少 Pass 基类

**决策：在 A.6 中首次引入 Pass 抽象基类。**

理由：
- A-F1（DialectLoweringPass）、A-F11（优化 Pass 管线）均继承 Pass，A.6 是最早的消费者
- Pass 基类极简（仅 virtual 析构 + getName + run），不引入不必要的基础设施
- 独立文件 `IRPass.h`，符合依赖图规划：`IRPass.h → IRModule.h (前向声明)`

### 问题 2：run() 返回值语义

**决策：`VerifierPass::run()` 返回 `bool`，语义为 `true = 验证通过，false = 验证失败`。**

理由：
- VerifierPass 是只读 Pass，不修改 IR，返回值仅表示"是否通过验证"
- 后续优化 Pass 的 `run()` 语义为 `true = IR 被修改，false = 未修改`，两者不同但兼容
- Pass 基类不约束 `run()` 的布尔语义，由子类自行定义

### 问题 3：错误报告机制

**决策：定义 `VerificationDiagnostic` 结构体，通过 `SmallVector` 收集错误，VerifierPass 构造时可选传入收集器。**

- 支持两种使用模式：
  - **断言模式**：不传 Diag，验证失败直接 `assert(false)`（Debug 构建默认行为）
  - **收集模式**：传入 `SmallVector<VerificationDiagnostic>&`，收集所有错误后统一报告
- `VerificationDiagnostic` 包含：检查类别、错误消息

### 问题 4：SSA 性质验证的具体定义

**决策：A.6 阶段实现以下 SSA 相关检查：**

1. **操作数非空**：每条指令的所有 Use 指向的 IRValue 指针非 nullptr
2. **定义域检查**：指令的操作数必须满足以下可见性规则：
   - 常量值（IRConstant 子类）全局可见
   - 函数参数（IRArgument）在所属函数内可见
   - 指令结果（IRInstruction）必须与当前指令在同一函数内，且定义在该指令之前或属于 Phi 的 incoming 值
3. **Phi 节点位置约束**：Phi 指令必须出现在 BB 的最前面（Phi 之前不得有非 Phi 指令）

**注意**：当前 IR 的 `createPhi()` 实现仅存储 `NumIncoming` 常量，尚无 `addIncoming(Value, BB)` 机制。因此 A.6 对 Phi 的验证仅限于位置约束和类型检查，不检查 incoming 值与前置 BB 的一致性（待后续增强）。

---

## 必须实现的类型定义

### IRPass.h — Pass 抽象基类

```cpp
#ifndef BLOCKTYPE_IR_IRPASS_H
#define BLOCKTYPE_IR_IRPASS_H

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRModule;

/// Pass 抽象基类 — 所有 IR 层 Pass 的公共接口。
/// run() 的 bool 返回值语义由子类自行定义。
class Pass {
public:
  virtual ~Pass() = default;

  /// 返回 Pass 的标识名称，用于日志和调试。
  virtual StringRef getName() const = 0;

  /// 在 Module 上执行该 Pass。
  /// 返回值的语义由具体 Pass 定义（VerifierPass: true=通过; 优化Pass: true=修改了IR）。
  virtual bool run(IRModule& M) = 0;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRPASS_H
```

### IRVerifier.h — 验证诊断 + VerifierPass

```cpp
#ifndef BLOCKTYPE_IR_IRVERIFIER_H
#define BLOCKTYPE_IR_IRVERIFIER_H

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRPass.h"

namespace blocktype {
namespace ir {

class IRModule;
class IRFunction;
class IRBasicBlock;
class IRInstruction;
class IRType;

// ============================================================================
// 验证诊断
// ============================================================================

/// 验证检查的类别
enum class VerificationCategory : uint8_t {
  ModuleLevel    = 0,  // 模块级检查
  FunctionLevel  = 1,  // 函数级检查
  BasicBlockLevel = 2, // 基本块级检查
  InstructionLevel = 3, // 指令级检查
  TypeLevel      = 4,  // 类型级检查
};

/// 单条验证诊断信息
struct VerificationDiagnostic {
  VerificationCategory Category;
  std::string Message;

  VerificationDiagnostic(VerificationCategory Cat, const std::string& Msg)
    : Category(Cat), Message(Msg) {}
};

// ============================================================================
// VerifierPass
// ============================================================================

/// IR 验证 Pass — 检查 IRModule 的结构正确性。
///
/// 使用方式：
///   // 收集模式：收集所有错误
///   SmallVector<VerificationDiagnostic, 32> Errors;
///   VerifierPass VP(&Errors);
///   bool OK = VP.run(Module);
///
///   // 断言模式：验证失败直接 assert（Debug 构建）
///   VerifierPass VP;
///   bool OK = VP.run(Module);
///
/// run() 返回值：true = 验证通过，false = 发现错误。
class VerifierPass : public Pass {
public:
  /// 构造函数。
  /// @param Diag 可选的诊断收集器。传入 nullptr 则使用断言模式。
  explicit VerifierPass(SmallVector<VerificationDiagnostic, 32>* Diag = nullptr);

  StringRef getName() const override { return "verifier"; }

  /// 执行验证。true = 通过，false = 失败。
  bool run(IRModule& M) override;

  /// 独立验证函数 — 可不通过 Pass 框架直接调用。
  static bool verify(IRModule& M, SmallVector<VerificationDiagnostic, 32>* Diag = nullptr);

private:
  SmallVector<VerificationDiagnostic, 32>* Diagnostics;
  bool HasErrors = false;

  // ---- 内部验证方法（均返回 true=通过，false=失败）----

  /// 模块级验证
  bool verifyModule(const IRModule& M);

  /// 类型级验证
  bool verifyType(const IRType* T);
  bool verifyTypeComplete(const IRType* T);

  /// 函数级验证
  bool verifyFunction(const IRFunction& F);

  /// 基本块级验证
  bool verifyBasicBlock(const IRBasicBlock& BB);

  /// 指令级验证
  bool verifyInstruction(const IRInstruction& I);

  // ---- 按操作码分类的指令验证 ----
  bool verifyTerminator(const IRInstruction& I);
  bool verifyBinaryOp(const IRInstruction& I);
  bool verifyFloatBinaryOp(const IRInstruction& I);
  bool verifyBitwiseOp(const IRInstruction& I);
  bool verifyMemoryOp(const IRInstruction& I);
  bool verifyCastOp(const IRInstruction& I);
  bool verifyCmpOp(const IRInstruction& I);
  bool verifyCallOp(const IRInstruction& I);
  bool verifyPhiOp(const IRInstruction& I);
  bool verifySelectOp(const IRInstruction& I);
  bool verifyOtherOp(const IRInstruction& I);

  // ---- 辅助方法 ----

  /// 记录一条验证错误
  void reportError(VerificationCategory Cat, const std::string& Msg);

  /// 获取指令所在函数（通过 Parent BB 追溯）
  const IRFunction* getContainingFunction(const IRInstruction& I) const;

  /// 检查操作数索引是否有效
  bool hasOperand(const IRInstruction& I, unsigned Idx) const;

  /// 获取操作数的类型（带空指针检查）
  IRType* getOperandType(const IRInstruction& I, unsigned Idx) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRVERIFIER_H
```

---

## 验证检查项（完整枚举）

### V-MOD：模块级检查

| 编号 | 检查项 | 失败条件 |
|------|--------|----------|
| V-MOD-1 | 模块名非空 | `M.getName().empty()` |
| V-MOD-2 | 函数无重名定义 | 存在两个 `IRFunction` 具有相同 `getName()` |
| V-MOD-3 | 全局变量无重名 | 存在两个 `IRGlobalVariable` 具有相同 `getName()` |
| V-MOD-4 | 所有函数均通过验证 | 任一 `verifyFunction()` 返回 false |
| V-MOD-5 | 所有全局变量类型合法 | `verifyType()` 返回 false |
| V-MOD-6 | 所有函数声明类型合法 | `verifyType()` 对声明返回 false |

### V-TYPE：类型级检查

| 编号 | 检查项 | 失败条件 |
|------|--------|----------|
| V-TYPE-1 | 类型非空 | `IRType*` 指针为 nullptr |
| V-TYPE-2 | 类型完整性（无 OpaqueType 残留） | 在函数体/指令/全局初始化器中发现 `IROpaqueType` |
| V-TYPE-3 | StructType 字段非空 | `IRStructType` 的某个字段类型为 nullptr |
| V-TYPE-4 | ArrayType 元素类型非空 | `IRArrayType::getElementType()` 返回 nullptr |
| V-TYPE-5 | VectorType 元素类型非空 | `IRVectorType::getElementType()` 返回 nullptr |
| V-TYPE-6 | PointerType 指向类型非空 | `IRPointerType::getPointeeType()` 返回 nullptr |
| V-TYPE-7 | FunctionType 返回类型非空 | `IRFunctionType::getReturnType()` 返回 nullptr |
| V-TYPE-8 | FunctionType 参数类型非空 | `IRFunctionType` 的某个参数类型为 nullptr |

### V-FUNC：函数级检查

| 编号 | 检查项 | 失败条件 |
|------|--------|----------|
| V-FUNC-1 | 函数类型非空 | `F.getFunctionType()` 返回 nullptr |
| V-FUNC-2 | 函数名非空 | `F.getName().empty()` |
| V-FUNC-3 | 参数数量匹配 | `F.getNumArgs() != F.getFunctionType()->getNumParams()` |
| V-FUNC-4 | 参数类型逐个匹配 | `F.getArgType(i) != F.getFunctionType()->getParamType(i)` |
| V-FUNC-5 | 定义函数至少有一个 BB | `F.isDefinition() && F.getNumBasicBlocks() == 0` |
| V-FUNC-6 | 定义函数的入口 BB 存在 | `F.isDefinition() && F.getEntryBlock() == nullptr` |
| V-FUNC-7 | 所有 BB 均通过验证 | 任一 `verifyBasicBlock()` 返回 false |

### V-BB：基本块级检查

| 编号 | 检查项 | 失败条件 |
|------|--------|----------|
| V-BB-1 | BB 有所属函数 | `BB.getParent() == nullptr` |
| V-BB-2 | 非空 BB 恰好有一个终结指令 | `!BB.empty() && BB.getTerminator() == nullptr` |
| V-BB-3 | 终结指令位于指令列表末尾 | 终结指令不是最后一条指令 |
| V-BB-4 | 无中间终结指令 | 在终结指令之后仍有非终结指令 |
| V-BB-5 | Phi 指令位于 BB 开头 | 在 Phi 指令之前出现非 Phi 指令 |
| V-BB-6 | 所有指令均通过验证 | 任一 `verifyInstruction()` 返回 false |

### V-INST：指令级检查（按操作码分类）

#### 终结指令

| 编号 | 操作码 | 检查项 |
|------|--------|--------|
| V-INST-RET-1 | Ret (void) | 操作数数量 == 0，结果类型 == void |
| V-INST-RET-2 | Ret (value) | 操作数数量 == 1，操作数类型 == 所在函数返回类型 |
| V-INST-BR-1 | Br | 操作数数量 == 1，操作数为 `IRBasicBlockRef` |
| V-INST-CONDBR-1 | CondBr | 操作数数量 == 3，operand[0] 为 i1，operand[1]/[2] 为 `IRBasicBlockRef` |
| V-INST-UNREACHABLE-1 | Unreachable | 操作数数量 == 0 |
| V-INST-INVOKE-1 | Invoke | operand[0] 为 `IRConstantFunctionRef`，最后两个操作数为 `IRBasicBlockRef`（Normal/Unwind），中间操作数为实参，实参数量和类型与被调用函数签名匹配 |

#### 整数二元运算（Add, Sub, Mul, UDiv, SDiv, URem, SRem）

| 编号 | 检查项 |
|------|--------|
| V-INST-BININT-1 | 操作数数量 == 2 |
| V-INST-BININT-2 | 两个操作数类型相同，且均为 `IRIntegerType` |
| V-INST-BININT-3 | 结果类型 == 操作数类型 |

#### 浮点二元运算（FAdd, FSub, FMul, FDiv, FRem）

| 编号 | 检查项 |
|------|--------|
| V-INST-BINFLT-1 | 操作数数量 == 2 |
| V-INST-BINFLT-2 | 两个操作数类型相同，且均为 `IRFloatType` |
| V-INST-BINFLT-3 | 结果类型 == 操作数类型 |

#### 位运算（Shl, LShr, AShr, And, Or, Xor）

| 编号 | 检查项 |
|------|--------|
| V-INST-BITWISE-1 | 操作数数量 == 2 |
| V-INST-BITWISE-2 | 两个操作数类型相同，且均为 `IRIntegerType` |
| V-INST-BITWISE-3 | 结果类型 == 操作数类型 |

#### 内存操作

| 编号 | 操作码 | 检查项 |
|------|--------|--------|
| V-INST-ALLOCA-1 | Alloca | 结果类型为 `IRPointerType` |
| V-INST-LOAD-1 | Load | 操作数数量 == 1，操作数类型为 `IRPointerType`，结果类型 == `getPointeeType()` |
| V-INST-STORE-1 | Store | 操作数数量 == 2，operand[1] 为 `IRPointerType`，operand[0] 类型 == `getPointeeType()`，结果类型 == void |
| V-INST-GEP-1 | GEP | 操作数数量 >= 2，operand[0] 为 `IRPointerType`，所有 index 操作数为 `IRIntegerType` |

#### 类型转换

| 编号 | 操作码 | 检查项 |
|------|--------|--------|
| V-INST-TRUNC-1 | Trunc | 1 个操作数，源为 `IRIntegerType`，目标为 `IRIntegerType`，目标 BitWidth < 源 BitWidth |
| V-INST-ZEXT-1 | ZExt | 1 个操作数，源为 `IRIntegerType`，目标为 `IRIntegerType`，目标 BitWidth > 源 BitWidth |
| V-INST-SEXT-1 | SExt | 1 个操作数，源为 `IRIntegerType`，目标为 `IRIntegerType`，目标 BitWidth > 源 BitWidth |
| V-INST-FPTRUNC-1 | FPTrunc | 1 个操作数，源为 `IRFloatType`，目标为 `IRFloatType`，目标 BitWidth < 源 BitWidth |
| V-INST-FPEXT-1 | FPExt | 1 个操作数，源为 `IRFloatType`，目标为 `IRFloatType`，目标 BitWidth > 源 BitWidth |
| V-INST-FPTOSI-1 | FPToSI | 1 个操作数，源为 `IRFloatType`，目标为 `IRIntegerType` |
| V-INST-FPTOUI-1 | FPToUI | 1 个操作数，源为 `IRFloatType`，目标为 `IRIntegerType` |
| V-INST-SITOFP-1 | SIToFP | 1 个操作数，源为 `IRIntegerType`，目标为 `IRFloatType` |
| V-INST-UITOFP-1 | UIToFP | 1 个操作数，源为 `IRIntegerType`，目标为 `IRFloatType` |
| V-INST-PTRTOINT-1 | PtrToInt | 1 个操作数，源为 `IRPointerType`，目标为 `IRIntegerType` |
| V-INST-INTTOPTR-1 | IntToPtr | 1 个操作数，源为 `IRIntegerType`，目标为 `IRPointerType` |
| V-INST-BITCAST-1 | BitCast | 1 个操作数，源与目标不同类型，源和目标的大小相同（均为指针、或同大小整数/浮点） |

#### 比较操作

| 编号 | 操作码 | 检查项 |
|------|--------|--------|
| V-INST-ICMP-1 | ICmp | 操作数数量 == 2，两个操作数类型相同且为 `IRIntegerType` 或 `IRPointerType`，结果类型为 `IRIntegerType(1)` |
| V-INST-FCMP-1 | FCmp | 操作数数量 == 2，两个操作数类型相同且为 `IRFloatType`，结果类型为 `IRIntegerType(1)` |

#### 函数调用

| 编号 | 检查项 |
|------|--------|
| V-INST-CALL-1 | 操作数数量 >= 1，operand[0] 为 `IRConstantFunctionRef` |
| V-INST-CALL-2 | `IRConstantFunctionRef::getFunction()` 返回非空 |
| V-INST-CALL-3 | 实参数量 == 被调用函数签名参数数量（`FTy->getNumParams()`） |
| V-INST-CALL-4 | 每个实参类型与签名中对应参数类型匹配 |
| V-INST-CALL-5 | 结果类型 == 被调用函数返回类型 |

#### Phi / Select / 其他

| 编号 | 操作码 | 检查项 |
|------|--------|--------|
| V-INST-PHI-1 | Phi | 结果类型非空，非 void |
| V-INST-PHI-2 | Phi | 位于 BB 开头（Phi 之前无其他非 Phi 指令） |
| V-INST-SELECT-1 | Select | 操作数数量 == 3，operand[0] 为 i1，operand[1] 和 operand[2] 类型相同，结果类型 == operand[1] 类型 |

#### 操作数通用检查（适用于所有指令）

| 编号 | 检查项 |
|------|--------|
| V-INST-GEN-1 | 所有操作数指针非空（`getOperand(i) != nullptr`） |
| V-INST-GEN-2 | 指令结果类型非空（`I.getType() != nullptr`） |
| V-INST-GEN-3 | 指令有所属 BB（`I.getParent() != nullptr`） |
| V-INST-GEN-4 | BB 的 Parent 函数非空（`I.getParent()->getParent() != nullptr`） |

---

## 操作数布局参考表

> 以下是从 `IRBuilder.cpp` 提取的实际操作数布局，VerifierPass 必须按照此布局验证。
> 操作数索引从 0 开始。

| Opcode | 操作数[0] | 操作数[1] | 操作数[2] | 操作数[N] | 结果类型 |
|--------|-----------|-----------|-----------|-----------|----------|
| Ret (void) | (无) | | | | IRVoidType |
| Ret (val) | 返回值 | | | | 值的类型 |
| Br | IRBasicBlockRef | | | | IRVoidType |
| CondBr | 条件(i1) | IRBasicBlockRef | IRBasicBlockRef | | IRVoidType |
| Invoke | IRConstantFunctionRef | arg0...argN | IRBasicBlockRef(Normal) | IRBasicBlockRef(Unwind) | 函数返回类型 |
| Unreachable | (无) | | | | IRVoidType |
| Add/Sub/Mul/UDiv/SDiv/URem/SRem | LHS | RHS | | | LHS的类型 |
| FAdd/FSub/FMul/FDiv/FRem | LHS | RHS | | | LHS的类型 |
| Shl/LShr/AShr/And/Or/Xor | LHS | RHS | | | LHS的类型 |
| Alloca | (无) | | | | IRPointerType |
| Load | 指针 | | | | 指针的pointee类型 |
| Store | 值 | 指针 | | | IRVoidType |
| GEP | 指针 | index0 | index1 | ... | IRPointerType |
| Trunc/ZExt/SExt | 值 | | | | 目标类型 |
| FPTrunc/FPExt | 值 | | | | 目标类型 |
| FPToSI/FPToUI/SIToFP/UIToFP | 值 | | | | 目标类型 |
| PtrToInt/IntToPtr | 值 | | | | 目标类型 |
| BitCast | 值 | | | | 目标类型 |
| ICmp | LHS | RHS | | | IRIntegerType(1) |
| FCmp | LHS | RHS | | | IRIntegerType(1) |
| Call | IRConstantFunctionRef | arg0 | arg1 | ... | 函数返回类型 |
| Phi | IRConstantInt(NumIncoming) | | | | Phi的类型 |
| Select | 条件(i1) | TrueVal | FalseVal | | TrueVal的类型 |
| ExtractValue | 聚合值 | index0(i32) | index1(i32) | ... | 聚合值的类型 |
| InsertValue | 聚合值 | 插入值 | index0(i32) | ... | 聚合值的类型 |

---

## CMakeLists.txt 修改

在 `src/IR/CMakeLists.txt` 的 `add_library(blocktype-ir ...)` 列表中追加：

```
IRPass.cpp
IRVerifier.cpp
```

---

## 实现约束

1. **命名空间**：所有代码在 `namespace blocktype::ir` 中
2. **依赖限制**：`IRVerifier` 仅依赖 `blocktype-basic`（ADT）和 IR 层其他头文件，不链接 LLVM
3. **只读**：`VerifierPass` 不修改 IRModule，仅读取并验证
4. **断言模式**：当构造函数传入 `Diagnostics == nullptr` 时，发现错误直接 `assert(false)` 并输出错误信息到 `errs()`
5. **收集模式**：当传入 `Diagnostics` 指针时，将所有错误追加到该 vector，不 assert
6. **错误消息格式**：`"[Verifier] <CategoryName>: <描述> (function @<F>, basic block <BB>, instruction <I>)"`
7. **不验证 getSuccessors()**：当前 `IRBasicBlock::getSuccessors()` 未实现（返回空 vector），VerifierPass 不依赖该方法

---

## 验收标准

### V1：合法 IRModule 通过验证

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("test", TCtx, "x86_64-unknown-linux-gnu");

auto* FTy = TCtx.getFunctionType(TCx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
auto* F = M.getOrInsertFunction("add", FTy);
auto* Entry = F->addBasicBlock("entry");

IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
auto* Arg0 = reinterpret_cast<IRValue*>(F->getArg(0));
auto* Arg1 = reinterpret_cast<IRValue*>(F->getArg(1));
auto* Sum = Builder.createAdd(Arg0, Arg1, "sum");
Builder.createRet(Sum);

SmallVector<VerificationDiagnostic, 32> Errors;
VerifierPass VP(&Errors);
bool OK = VP.run(M);
assert(OK && "Legal module should pass verification");
assert(Errors.empty() && "No errors expected");
```

### V2：含 OpaqueType 的模块不通过

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("opaque_test", TCtx);

auto* Opaque = TCtx.getOpaqueType("unresolved");
auto* FTy = TCtx.getFunctionType(Opaque, {});  // 返回类型为 Opaque
auto* F = M.getOrInsertFunction("bad_func", FTy);
auto* Entry = F->addBasicBlock("entry");

IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
Builder.createRetVoid();

SmallVector<VerificationDiagnostic, 32> Errors;
VerifierPass VP(&Errors);
bool OK = VP.run(M);
assert(!OK && "Module with OpaqueType should fail verification");
// 至少有一条 V-TYPE-2 错误
bool hasOpaqueError = false;
for (auto& E : Errors) {
  if (E.Message.find("opaque") != std::string::npos) hasOpaqueError = true;
}
assert(hasOpaqueError && "Expected OpaqueType error");
```

### V3：无终结指令的 BB 不通过

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("no_term", TCtx);

auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
auto* F = M.getOrInsertFunction("no_terminator", FTy);
auto* Entry = F->addBasicBlock("entry");

IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
Builder.createAdd(Builder.getInt32(1), Builder.getInt32(2), "dead");
// 没有 terminator

SmallVector<VerificationDiagnostic, 32> Errors;
VerifierPass VP(&Errors);
bool OK = VP.run(M);
assert(!OK && "BB without terminator should fail");
```

### V4：类型不匹配的二元运算不通过

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("type_mismatch", TCtx);

auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
auto* F = M.getOrInsertFunction("bad_binop", FTy);
auto* Entry = F->addBasicBlock("entry");

IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
// 手动构造类型不匹配的 add（i32 + i64）
auto* V1 = Builder.getInt32(1);
auto* V2 = Builder.getInt64(2);
auto* BadAdd = Builder.createAdd(V1, V2, "bad");
Builder.createRetVoid();

SmallVector<VerificationDiagnostic, 32> Errors;
VerifierPass VP(&Errors);
bool OK = VP.run(M);
assert(!OK && "Type mismatch in binary op should fail");
```

### V5：静态 verify() 接口

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("static_test", TCtx);

// 构建合法模块（同 V1）
// ...

bool OK = VerifierPass::verify(M);
assert(OK);
```

### V6：断言模式（Debug 构建下验证失败触发 assert）

```cpp
// 不传 Diagnostics，验证失败直接 assert
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("assert_mode", TCtx);

auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
auto* F = M.getOrInsertFunction("no_term", FTy);
F->addBasicBlock("entry"); // 空 BB，无 terminator

VerifierPass VP; // 无 Diagnostics
// VP.run(M);  // Debug 构建下会 assert(false)
// 此验收标准在测试中需用 EXPECT_DEATH 或类似机制验证
```

### V7：Call 指令参数不匹配不通过

```cpp
IRContext Ctx;
IRTypeContext& TCtx = Ctx.getTypeContext();
IRModule M("call_mismatch", TCtx);

auto* AddTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
auto* Callee = M.getOrInsertFunction("add", AddTy);
auto* CallerTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
auto* Caller = M.getOrInsertFunction("caller", CallerTy);
auto* Entry = Caller->addBasicBlock("entry");

IRBuilder Builder(Ctx);
Builder.setInsertPoint(Entry);
// 错误：只传了 1 个参数，但 add 需要 2 个
Builder.createCall(Callee, {Builder.getInt32(1)});
Builder.createRetVoid();

SmallVector<VerificationDiagnostic, 32> Errors;
VerifierPass VP(&Errors);
bool OK = VP.run(M);
assert(!OK && "Call with wrong arg count should fail");
```

---

> **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A.6 — IRVerifier + Pass 基类" && git push origin HEAD
> ```
