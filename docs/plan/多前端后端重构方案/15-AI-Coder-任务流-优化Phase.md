# AI Coder 可执行任务流 — 优化审查 Phase

> 本文档是 AI coder 可直接执行的改造任务流。优化任务穿插在 Phase A-E 中执行。
> 每个 OPT Task 标注了依赖的 Phase 和优先级。

---

## 执行规则

1. **OPT-01 必须在 Phase A 之前或之中完成**（P0，影响正确性）
2. **OPT-02/03/04 必须在对应 Phase 中完成**（P0）
3. **OPT-05/06/07/08 在对应 Phase 中完成**（P1，影响质量）
4. **OPT-09/10 在对应 Phase 中完成**（P2，影响扩展性）
5. **接口签名不可修改**
6. **验收标准必须全部通过**
7. **Git 提交与推送**：每个 OPT Task 完成并通过验收后，**必须立即**执行以下操作：
   ```bash
   git add -A
   git commit -m "feat(opt): 完成 <OPT编号> — <OPT标题>"
   git push origin HEAD
   ```
   - commit message 格式：`feat(opt): 完成 OPT-01 — Pass 分层重构`
   - **不得跳过此步骤**——确保每个 Task 的产出都有远端备份，防止工作丢失
   - 如果 push 失败，先 `git pull --rebase origin HEAD` 再重试

---

## 接口关联关系

### OPT 任务与 Phase 任务的关联

```
OPT-01（LLVM依赖分层） ──blocks──→ Phase A 全部任务
  IR库不链接任何LLVM → A.1~A.8 的 CMake 必须正确

OPT-02（CGCXX拆分） ──replaces──→ Phase B Task B.7（原7子文件→4子文件）
  已在 12-AI-Coder-任务流-PhaseB.md 的 B.7 中体现

OPT-03（CompilerInstance重构） ──extends──→ Phase D Task D.2
  已在 13-AI-Coder-任务流-PhaseCDE.md 的 D.2 中体现

OPT-04（差分测试管线） ──blocks──→ Phase B 完成后
  每个 Phase 完成后自动运行

OPT-05（Pass分层） ──extends──→ Phase A Task A.6（IRVerifier）
  VerifierPass 改为 ModulePass 子类

OPT-06（VerifierPass强制化） ──extends──→ Phase A Task A.6
  Debug构建自动验证

OPT-07（IRVisitor模式） ──extends──→ Phase C Task C.3（IRToLLVMConverter）
  IRToLLVMConverter 继承 IRVisitor

OPT-08（TargetLayout字段明确化） ──replaces──→ Phase A Task A.2
  已在 11-AI-Coder-任务流-PhaseA.md 的 A.2 中体现

OPT-09（DialectRegistry） ──extends──→ Phase A Task A-F1
  DialectID 改为 uint16_t + DialectRegistry 动态注册

OPT-10（注册生命周期） ──extends──→ Phase B/C Registry 设计
  已在 12/13 的 B.2/C.1 中体现
```

---

## OPT-01：LLVM 依赖分层（P0）

### 依赖

无（必须在 Phase A 之前完成）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `CMakeLists.txt`（顶层） |
| 修改 | `src/IR/CMakeLists.txt`（新增） |
| 修改 | `src/Sema/CMakeLists.txt` |
| 修改 | `src/AST/CMakeLists.txt` |
| 修改 | `src/Basic/CMakeLists.txt` |
| 修改 | `src/Frontend/CMakeLists.txt` |
| 修改 | `src/CodeGen/CMakeLists.txt` |

### 必须实现的 CMake 变量

```cmake
# 顶层 CMakeLists.txt 中新增
llvm_map_components_to_libnames(LLVM_ADT_LIBS support)
llvm_map_components_to_libnames(LLVM_IR_LIBS
  core native asmparser bitwriter irreader transformutils
  x86asmparser x86codegen x86desc x86info
  aarch64asmparser aarch64codegen aarch64desc aarch64info
)
```

### 依赖关系表

| 库 | 依赖 | 禁止依赖 |
|---|------|---------|
| `libblocktype-basic` | LLVM_ADT_LIBS | LLVM_IR_LIBS |
| `libblocktype-ast` | blocktype-basic + LLVM_ADT_LIBS | LLVM_IR_LIBS |
| `libblocktype-sema` | blocktype-ast + LLVM_ADT_LIBS | LLVM_IR_LIBS |
| `libblocktype-ir` | **blocktype-basic（不链接任何LLVM！）** | LLVM_ADT_LIBS, LLVM_IR_LIBS |
| `libblocktype-frontend` | blocktype-sema + blocktype-ir + LLVM_ADT_LIBS | LLVM_IR_LIBS |
| `libblocktype-backend-llvm` | blocktype-ir + LLVM_ADT_LIBS + LLVM_IR_LIBS | 无禁止 |

### 实现约束

1. `libblocktype-ir` 的 CMake 不包含任何 LLVM 库
2. Sema/AST 仅链接 `LLVM_ADT_LIBS`
3. 所有库独立编译链接成功
4. CodeGen 库重命名为 `libblocktype-backend-llvm`（或作为其内部模块）

### 验收标准

```bash
# V1: libblocktype-ir 不链接任何 LLVM 库
nm libblocktype-ir.a | grep "llvm::" | wc -l
# 输出 == 0

# V2: Sema/AST 仅链接 LLVM_ADT_LIBS
nm libblocktype-sema.a | grep "llvm::IR::" | wc -l
# 输出 == 0

# V3: 所有库独立编译链接成功
cmake --build build
# 退出码 == 0

# V4: 全部测试通过
ctest --output-on-failure
# All tests passed
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-01：LLVM 依赖分层（P0）" && git push origin HEAD
> ```

---

## OPT-02：CGCXX 拆分策略修订（P0）

> **已在 12-AI-Coder-任务流-PhaseB.md 的 Task B.7 中体现。**
> 原 7 子文件 → 4 子文件（Layout/CtorDtor/VTable/Inherit）。
> 此处仅记录验收标准。

### 验收标准

```bash
# V1: 4个子文件编译通过
cmake --build build --target blocktype-frontend
# 退出码 == 0

# V2: CGCXX功能测试全部通过
ctest -R "CXX" --output-on-failure
# All tests passed

# V3: 无跨文件循环依赖
# 4个子文件之间无 #include 循环
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-02：CGCXX 拆分策略修订（P0）" && git push origin HEAD
> ```

---

## OPT-03：CompilerInstance 重构专项（P0）

> **已在 13-AI-Coder-任务流-PhaseCDE.md 的 Task D.2 中体现。**
> LLVM上下文/优化/代码生成移入LLVMBackend。
> 此处仅记录验收标准。

### 验收标准

```bash
# V1: CompilerInstance.h 不包含任何 llvm/IR/ 头文件
grep -c 'llvm/IR/' include/blocktype/Frontend/CompilerInstance.h
# 输出 == 0

# V2: CompilerInstance.h 中 llvm:: 引用数 = 0
grep -c 'llvm::' include/blocktype/Frontend/CompilerInstance.h
# 输出 == 0

# V3: 编译管线通过 BackendBase 接口调用
# CompilerInstance 不直接调用 llvm::PassBuilder/TargetMachine
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-03：CompilerInstance 重构专项（P0）" && git push origin HEAD
> ```

---

## OPT-04：差分测试管线（P0）

### 依赖

- Phase B 完成后启用

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `tests/differential/diff_test.py` |
| 新增 | `tests/differential/diff_test.sh` |

### 必须实现的差分测试脚本

```bash
#!/bin/bash
# diff_test.sh — 差分测试管线
# 对比新旧路径的编译结果

SOURCE_FILE=$1
OLD_OUTPUT=$(mktemp /tmp/diff_old_XXXXXX.o)
NEW_OUTPUT=$(mktemp /tmp/diff_new_XXXXXX.o)

# 旧路径：AST → LLVM IR → 目标文件
blocktype-old $SOURCE_FILE -o $OLD_OUTPUT

# 新路径：AST → BTIR → LLVM IR → 目标文件
blocktype --frontend=cpp --backend=llvm $SOURCE_FILE -o $NEW_OUTPUT

# 对比目标代码
diff <(objdump -d $OLD_OUTPUT) <(objdump -d $NEW_OUTPUT)
DIFF_RESULT=$?

rm -f $OLD_OUTPUT $NEW_OUTPUT
exit $DIFF_RESULT
```

### 实现约束

1. CI 中差分测试自动运行
2. 新旧路径编译结果 diff = 0
3. 差分测试通过率 100%

### 验收标准

```bash
# V1: CI差分测试自动运行
# .github/workflows/test.yml 中包含 diff_test 步骤

# V2: 新旧路径编译结果一致
./tests/differential/diff_test.sh test.cpp
# 退出码 == 0（diff为空）

# V3: 差分测试覆盖所有测试文件
for f in tests/differential/*.cpp; do
  ./tests/differential/diff_test.sh $f
done
# 所有退出码 == 0
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-04：差分测试管线（P0）" && git push origin HEAD
> ```

---

## OPT-05：Pass 分层设计（P1）

### 依赖

- Phase A Task A.6（IRVerifier）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRPass.h` |
| 新增 | `src/IR/IRPassManager.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::ir {

class InvariantSet {
  uint32_t PreservedBits = 0;
public:
  void preserve(Invariant I) { PreservedBits |= static_cast<uint32_t>(I); }
  void invalidate(Invariant I) { PreservedBits &= ~static_cast<uint32_t>(I); }
  bool isPreserved(Invariant I) const { return (PreservedBits & static_cast<uint32_t>(I)) != 0; }
  static InvariantSet all() { return InvariantSet(~0u); }
  static InvariantSet none() { return InvariantSet(0u); }
};

class ModulePass {
public:
  virtual ~ModulePass() = default;
  virtual bool runOnModule(IRModule& M) = 0;
  virtual StringRef getName() const = 0;
  virtual InvariantSet getPreservedInvariants() const { return InvariantSet::none(); }
};

class FunctionPass {
public:
  virtual ~FunctionPass() = default;
  virtual bool runOnFunction(IRFunction& F) = 0;
  virtual StringRef getName() const = 0;
  virtual InvariantSet getPreservedInvariants() const { return InvariantSet::none(); }
};

class PassManager {
  struct PassEntry {
    unique_ptr<ModulePass> MP;
    unique_ptr<FunctionPass> FP;
  };
  SmallVector<PassEntry, 16> Passes;
public:
  bool run(IRModule& M);
  void addPass(unique_ptr<ModulePass> P);
  void addPass(unique_ptr<FunctionPass> P);  // 自动包装为 ModulePass
  size_t getNumPasses() const { return Passes.size(); }
};

}
```

### FunctionPass 自动包装为 ModulePass 的实现

```cpp
class FunctionPassWrapper : public ModulePass {
  unique_ptr<FunctionPass> FP;
public:
  explicit FunctionPassWrapper(unique_ptr<FunctionPass> F) : FP(std::move(F)) {}
  bool runOnModule(IRModule& M) override {
    bool Changed = false;
    for (auto& F : M.getFunctions()) {
      Changed |= FP->runOnFunction(*F);
    }
    return Changed;
  }
  StringRef getName() const override { return FP->getName(); }
  InvariantSet getPreservedInvariants() const override { return FP->getPreservedInvariants(); }
};
```

### VerifierPass 改为 ModulePass 子类

```cpp
class VerifierPass : public ModulePass {
public:
  StringRef getName() const override { return "verifier"; }
  bool runOnModule(IRModule& M) override;
  InvariantSet getPreservedInvariants() const override { return InvariantSet::all(); }
};
```

### 实现约束

1. ModulePass/FunctionPass 编译通过
2. PassManager 自动包装 FunctionPass
3. 现有 4 个 Pass 迁移到新分层

### 验收标准

```cpp
// V1: ModulePass 编译通过
VerifierPass V;
assert(V.getName() == "verifier");

// V2: FunctionPass 自动包装
PassManager PM;
PM.addPass(make_unique<VerifierPass>());
PM.addPass(make_unique<SomeFunctionPass>());
assert(PM.getNumPasses() == 2);

// V3: PassManager 运行
auto Result = PM.run(*Module);
// 不崩溃
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-05：Pass 分层设计（P1）" && git push origin HEAD
> ```

---

## OPT-06：VerifierPass 强制化（P1）

### 依赖

- OPT-05（Pass 分层）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `src/IR/IRPassManager.cpp` |

### PassManager Debug 模式自动验证

```cpp
bool PassManager::run(IRModule& M) {
  bool Changed = false;
  for (auto& Entry : Passes) {
    bool PassChanged = Entry.MP ? Entry.MP->runOnModule(M)
                                : /* FunctionPass wrapper */;
    Changed |= PassChanged;
    if (DebugMode) {
      VerifierPass V;
      auto Preserved = Entry.MP ? Entry.MP->getPreservedInvariants()
                                : Entry.FP->getPreservedInvariants();
      if (!V.verify(M, Preserved)) {
        Diags.report(IRPassInvariantBroken, Entry.MP ? Entry.MP->getName()
                                                      : Entry.FP->getName());
      }
    }
  }
  return Changed;
}
```

### 实现约束

1. Debug 构建自动在每个 Pass 后运行 VerifierPass
2. InvariantSet 机制工作正常
3. Release 构建无额外开销

### 验收标准

```bash
# V1: Debug构建自动验证
cmake -DCMAKE_BUILD_TYPE=Debug --build build
# 每个 Pass 后自动运行 VerifierPass

# V2: Release构建无额外开销
cmake -DCMAKE_BUILD_TYPE=Release --build build
# PassManager::run 中无 VerifierPass 调用

# V3: Invariant 破坏时报错
# 如果某个 Pass 破坏了 SSA 性质
# Debug 构建应报告 IRPassInvariantBroken
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-06：VerifierPass 强制化（P1）" && git push origin HEAD
> ```

---

## OPT-07：IRVisitor 模式（P1）

### 依赖

- Phase C Task C.3（IRToLLVMConverter）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRVisitor.h` |
| 新增 | `src/IR/IRVisitor.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::ir {

class IRVisitor {
public:
  virtual ~IRVisitor() = default;
  virtual void visitModule(IRModule& M);
  virtual void visitFunction(IRFunction& F);
  virtual void visitBasicBlock(IRBasicBlock& BB);
  virtual void visitInstruction(IRInstruction& I);  // fallback

  virtual void visitRet(IRInstruction& I);
  virtual void visitBr(IRInstruction& I);
  virtual void visitCondBr(IRInstruction& I);
  virtual void visitSwitch(IRInstruction& I);
  virtual void visitInvoke(IRInstruction& I);
  virtual void visitUnreachable(IRInstruction& I);
  virtual void visitResume(IRInstruction& I);
  virtual void visitAdd(IRInstruction& I);
  virtual void visitSub(IRInstruction& I);
  virtual void visitMul(IRInstruction& I);
  virtual void visitUDiv(IRInstruction& I);
  virtual void visitSDiv(IRInstruction& I);
  virtual void visitFAdd(IRInstruction& I);
  virtual void visitFSub(IRInstruction& I);
  virtual void visitFMul(IRInstruction& I);
  virtual void visitFDiv(IRInstruction& I);
  virtual void visitShl(IRInstruction& I);
  virtual void visitLShr(IRInstruction& I);
  virtual void visitAShr(IRInstruction& I);
  virtual void visitAnd(IRInstruction& I);
  virtual void visitOr(IRInstruction& I);
  virtual void visitXor(IRInstruction& I);
  virtual void visitAlloca(IRInstruction& I);
  virtual void visitLoad(IRInstruction& I);
  virtual void visitStore(IRInstruction& I);
  virtual void visitGEP(IRInstruction& I);
  virtual void visitTrunc(IRInstruction& I);
  virtual void visitZExt(IRInstruction& I);
  virtual void visitSExt(IRInstruction& I);
  virtual void visitFPTrunc(IRInstruction& I);
  virtual void visitFPExt(IRInstruction& I);
  virtual void visitBitCast(IRInstruction& I);
  virtual void visitICmp(IRInstruction& I);
  virtual void visitFCmp(IRInstruction& I);
  virtual void visitCall(IRInstruction& I);
  virtual void visitPhi(IRInstruction& I);
  virtual void visitSelect(IRInstruction& I);
  virtual void visitExtractValue(IRInstruction& I);
  virtual void visitInsertValue(IRInstruction& I);

  void visit(IRModule& M);   // 遍历入口
  void visit(IRFunction& F);
  void visit(IRBasicBlock& BB);
};

}
```

### visit() 自动分派算法

```
visit(IRInstruction& I):
  switch (I.getOpcode()):
    case Opcode::Ret:     visitRet(I);     break;
    case Opcode::Br:      visitBr(I);      break;
    case Opcode::Add:     visitAdd(I);     break;
    ...
    default:              visitInstruction(I);  // fallback
```

### IRToLLVMConverter 改为继承 IRVisitor

```cpp
class IRToLLVMConverter : public IRVisitor {
  // 替代原 convertInstruction() 中的大型 switch-case
  void visitAdd(IRInstruction& I) override;
  void visitSub(IRInstruction& I) override;
  void visitCall(IRInstruction& I) override;
  // ... 每种指令类型一个visit方法
};
```

### 实现约束

1. IRVisitor 编译通过
2. IRToLLVMConverter 继承 IRVisitor
3. 无 switch-case 指令分派

### 验收标准

```cpp
// V1: IRVisitor 自动分派
TestVisitor TV;
TV.visit(*Module);
// 每种指令类型调用对应的 visit 方法

// V2: IRToLLVMConverter 使用 IRVisitor
// IRToLLVMConverter.h 中无 switch-case
grep -c 'switch' src/Backend/IRToLLVMConverter.cpp
# 输出 == 0（或仅用于非指令分派）

// V3: fallback 处理未知指令
// 未知 Opcode → visitInstruction() fallback
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-07：IRVisitor 模式（P1）" && git push origin HEAD
> ```

---

## OPT-08：TargetLayout 字段明确化（P1）

> **已在 11-AI-Coder-任务流-PhaseA.md 的 Task A.2 中体现。**
> TargetLayout 是 llvm::DataLayout 的子集，不持有 llvm::DataLayout 引用。
> 构造路径：TargetTriple → TargetInfo → TargetLayout。
> 此处仅记录补充字段。

### TargetLayout 补充字段

```cpp
class TargetLayout {
  // 基础字段（已在 A.2 中定义）
  std::string TripleStr;
  uint64_t PointerSize, PointerAlign;
  uint64_t IntSize, LongSize, LongLongSize;
  uint64_t FloatSize, DoubleSize, LongDoubleSize;
  uint64_t MaxVectorAlign;
  bool IsLittleEndian, IsLinux, IsMacOS;

  // OPT-08 补充字段
  uint32_t CharWidth = 8;
  uint32_t IntAlign = 4;
  uint32_t LongAlign = 8;
  uint32_t LongLongAlign = 8;
  uint32_t FloatAlign = 4;
  uint32_t DoubleAlign = 8;
  uint32_t LongDoubleAlign = 16;
  uint32_t SizeType = 64;       // size_t 的位宽
  uint32_t PtrDiffType = 64;    // ptrdiff_t 的位宽
  bool IsBigEndian = false;

  // 补充方法
  static TargetLayout fromTargetInfo(const TargetInfo& TI);
  uint64_t getTypeAllocSizeInBits(IRType* T) const;
};
```

### 验收标准

```cpp
// V1: fromTargetInfo 正确映射
auto Layout = TargetLayout::fromTargetInfo(TI);
assert(Layout.getPointerSizeInBits() == 64);

// V2: 不持有 llvm::DataLayout 引用
// TargetLayout.h 中无 #include "llvm/IR/DataLayout.h"
grep -c 'DataLayout' include/blocktype/IR/TargetLayout.h
# 输出 == 0
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-08：TargetLayout 字段明确化（P1）" && git push origin HEAD
> ```

---

## OPT-09：DialectRegistry 动态注册（P2）

### 依赖

- Phase A Task A-F1（DialectID 字段）

### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRDialect.h` |
| 新增 | `src/IR/IRDialect.cpp` |

### 必须实现的类型定义

```cpp
namespace blocktype::ir::dialect {

using DialectFactory = std::function<unique_ptr<Dialect>()>;

class Dialect {
  uint16_t ID;
  std::string Name;
public:
  Dialect(uint16_t I, StringRef N) : ID(I), Name(N.str()) {}
  uint16_t getID() const { return ID; }
  StringRef getName() const { return Name; }
};

class DialectRegistry {
  DenseMap<StringRef, DialectFactory> Registry;
  DenseMap<StringRef, uint16_t> NameToID;
  uint16_t NextUserID = 256;
  DialectRegistry() = default;
public:
  static DialectRegistry& instance();
  void registerDialect(StringRef Name, DialectFactory F);
  Dialect* getDialect(StringRef Name);
  uint16_t getDialectID(StringRef Name);

  static constexpr uint16_t BtCoreID = 0;
  static constexpr uint16_t BtCppID = 1;
  static constexpr uint16_t BtTargetID = 2;
  static constexpr uint16_t BtDebugID = 3;
  static constexpr uint16_t BtMetaID = 4;
  static constexpr uint16_t FirstUserID = 256;
};

}
```

### DialectID 从 enum 改为 uint16_t

```cpp
// 原 enum class DialectID : uint8_t → 改为 uint16_t
using DialectID = uint16_t;

// 内置 Dialect 常量
constexpr DialectID Dialect_Core     = DialectRegistry::BtCoreID;
constexpr DialectID Dialect_Cpp      = DialectRegistry::BtCppID;
constexpr DialectID Dialect_Target   = DialectRegistry::BtTargetID;
constexpr DialectID Dialect_Debug    = DialectRegistry::BtDebugID;
constexpr DialectID Dialect_Metadata = DialectRegistry::BtMetaID;
```

### 实现约束

1. DialectRegistry 编译通过
2. 5 个内置 Dialect 自动注册
3. 用户 Dialect 可从 ID=256 开始注册
4. IRType/IRInstruction 的 DialectID 字段改为 uint16_t

### 验收标准

```cpp
// V1: 内置 Dialect 自动注册
auto& Reg = DialectRegistry::instance();
assert(Reg.getDialectID("bt_core") == 0);
assert(Reg.getDialectID("bt_cpp") == 1);

// V2: 用户 Dialect 注册
Reg.registerDialect("bt_coroutine", createCoroutineDialect);
assert(Reg.getDialectID("bt_coroutine") >= 256);

// V3: DialectID 字段兼容
IRType T(IRType::Integer, Dialect_Core);
assert(T.getDialect() == 0);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-09：DialectRegistry 动态注册（P2）" && git push origin HEAD
> ```

---

## OPT-10：Frontend/Backend 注册生命周期管理（P2）

> **已在 12/13 的 B.2/C.1/D.4 中体现。**
> 静态初始化注册 + CompilerInstance 析构时清理 + 多实例不冲突。
> 此处仅记录验收标准。

### 验收标准

```cpp
// V1: 静态注册机制工作正常
assert(FrontendRegistry::instance().hasFrontend("cpp") == true);
assert(BackendRegistry::instance().hasBackend("llvm") == true);

// V2: CompilerInstance 析构时无泄漏
{
  CompilerInstance CI;
  CI.compileFile("test.cpp");
} // ~CompilerInstance() 无内存泄漏

// V3: 多 CompilerInstance 实例不冲突
{
  CompilerInstance CI1;
  CompilerInstance CI2;
  // 两个实例独立工作，Registry 共享
}
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(opt): 完成 OPT-10：Frontend/Backend 注册生命周期管理（P2）" && git push origin HEAD
> ```

---

## OPT 任务执行时间线

| OPT 任务 | 优先级 | 执行时机 | 对应 Phase 任务 | 工作量 |
|---------|--------|---------|----------------|--------|
| OPT-01 | P0 | Phase A 之前 | — | 1天 |
| OPT-02 | P0 | Phase B Task B.7 | B.7（已内联） | 2-3天 |
| OPT-03 | P0 | Phase D Task D.2 | D.2（已内联） | +1天 |
| OPT-04 | P0 | Phase B 完成后 | — | 2天 |
| OPT-05 | P1 | Phase A Task A.6 | A.6（扩展） | +1天 |
| OPT-06 | P1 | Phase A Task A.6 | A.6（扩展） | +0.5天 |
| OPT-07 | P1 | Phase C Task C.3 | C.3（扩展） | +1天 |
| OPT-08 | P1 | Phase A Task A.2 | A.2（已内联） | 0天 |
| OPT-09 | P2 | Phase A Task A-F1 | A-F1（扩展） | +1天 |
| OPT-10 | P2 | Phase B/C | B.2/C.1（已内联） | +0.5天 |