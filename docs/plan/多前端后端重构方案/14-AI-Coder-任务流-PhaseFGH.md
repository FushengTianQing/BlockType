# AI Coder 可执行任务流 — Phase FGH：高级特性 + 增量编译 + AI 辅助

> 本文档是 AI coder 可直接执行的改造任务流。每个 Task 自包含所有必要信息。

---

## 执行规则

1. **严格按 Task 编号顺序执行**，每个 Task 完成并通过验收后再开始下一个
2. **接口签名不可修改**——本文档中的 class/struct/enum/函数签名是硬约束
3. **验收标准必须全部通过**
4. **命名空间**：高级特性在 `namespace blocktype::ir` 和 `namespace blocktype::backend` 中
5. **Phase F/G/H 为远期规划**，接口定义可能随实现调整，但核心语义不变
6. **Git 提交与推送**：每个 Task 完成并通过验收后，**必须立即**执行以下操作：
   ```bash
   git add -A
   git commit -m "feat(<phase>): 完成 <Task编号> — <Task标题>"
   git push origin HEAD
   ```
   - commit message 格式：`feat(F): 完成 F-01 — QueryContext + QueryResult`
   - **不得跳过此步骤**——确保每个 Task 的产出都有远端备份，防止工作丢失
   - 如果 push 失败，先 `git pull --rebase origin HEAD` 再重试

---

## 接口关联关系

### 持有关系

```
QueryContext ──owns──→ IRTypeContext&
QueryContext ──owns──→ DenseMap<QueryID, QueryResult> Cache
QueryContext ──owns──→ DependencyGraph DepGraph
QueryContext ──ref──→ IRModule*（查询目标模块）

DependencyGraph ──owns──→ DenseMap<QueryID, SmallVector<QueryID>> Edges
DependencyGraph ──owns──→ DenseMap<QueryID, Fingerprint> Fingerprints

AIAutoTuner ──ref──→ IRStatistics&（统计特征输入）
AIAutoTuner ──owns──→ SmallVector<PassSequence> History

IRStatistics ──ref──→ const IRModule&（只读操作）

PluginManager ──owns──→ StringMap<unique_ptr<CompilerPlugin>> Plugins
PluginManager ──ref──→ PassManager&（Pass注册目标）

RedGreenMarker ──ref──→ QueryContext&（查询上下文）
RedGreenMarker ──ref──→ DependencyGraph&（依赖图）

SymbolicExecutor ──owns──→ SmallVector<ExecutionPath> ExploredPaths
SymbolicExecutor ──ref──→ const IRModule&（分析目标）

IRSigner ──owns──→ Ed25519 密钥对
IRSigner ──ref──→ IRSerializer&（签名附加到序列化输出）

DistributedCache ──ref──→ IRRemoteCacheClient&（远程缓存后端）
DistributedCache ──owns──→ ConsistentHash Ring
```

### 调用关系

```
QueryContext::query(QueryID, Args)
  ──calls──→ Cache.lookup(ID) 先查缓存
  ──calls──→ DependencyGraph::recordDependency(ID, DepIDs) 记录依赖
  ──calls──→ Fingerprint::compute(Args) 计算指纹
  ──calls──→ 实际查询函数（如 parseSourceFile, convertASTToIR）
  ──calls──→ Cache.store(ID, Result) 存入缓存
  ──returns──→ QueryResult

DependencyGraph::recordDependency(ID, DepIDs)
  ──calls──→ Edges[ID].append(DepIDs) 添加依赖边
  ──calls──→ Fingerprint::compute(DepIDs) 计算依赖指纹

DependencyGraph::getDependents(ID)
  ──calls──→ 反向遍历 Edges 查找所有依赖者
  ──returns──→ SmallVector<QueryID>

RedGreenMarker::tryMarkGreen(QueryID)
  ──calls──→ DependencyGraph::getDependencies(ID) 获取依赖
  ──calls──→ 递归 tryMarkGreen(每个依赖)
  ──calls──→ Fingerprint::compare(当前指纹, 缓存指纹) 对比指纹
  ──calls──→ 假阳性优化：指纹相同→重新计算→对比结果
  ──returns──→ Green（可复用）或 Red（需重算）

AIAutoTuner::recommendPassSequence(IRStatistics)
  ──calls──→ StubAIAutoTuner::recommendPassSequence() 返回默认序列
  ──calls──→ History 查找相似统计特征
  ──returns──→ SmallVector<PassID>（仅建议，不自动执行）

SymbolicExecutor::execute(const IRFunction&)
  ──calls──→ 遍历所有执行路径
  ──calls──→ 收集路径约束
  ──calls──→ 检查路径覆盖 ≥ 80%
  ──returns──→ IsEquivalent + 反例（如不等价）

IRSigner::sign(IRModule&)
  ──calls──→ IRSerializer::writeBitcode() 序列化
  ──calls──→ Ed25519::sign(Data, PrivateKey) 签名
  ──calls──→ 附加签名到序列化输出
  ──returns──→ SignedData

IRSigner::verify(SignedData)
  ──calls──→ Ed25519::verify(Data, Signature, PublicKey) 验签
  ──returns──→ bool
```

### 生命周期约束

```
QueryContext 生命周期 ≥ 所有查询调用
DependencyGraph 生命周期 = QueryContext 生命周期
RedGreenMarker 生命周期 = 单次增量编译会话
SymbolicExecutor 生命周期 = 单次验证调用
AIAutoTuner 生命周期 ≥ 多次编译调用（跨会话学习）
```

---

## Phase F：高级特性期

### Task F-01：QueryContext + 基础查询实现

#### 依赖

- Phase E 全部完成

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/QueryContext.h` |
| 新增 | `src/IR/QueryContext.cpp` |

#### 必须实现的类型定义

```cpp
namespace blocktype::ir {

using QueryID = uint64_t;

class QueryResult {
  enum Kind { Module, Function, Type, Value, Void } ResultKind;
  std::any Data;
public:
  static QueryResult makeModule(IRModule* M);
  static QueryResult makeFunction(IRFunction* F);
  static QueryResult makeType(IRType* T);
  static QueryResult makeValue(IRValue* V);
  static QueryResult makeVoid();
  IRModule* getAsModule() const;
  IRFunction* getAsFunction() const;
  IRType* getAsType() const;
  IRValue* getAsValue() const;
  bool isValid() const { return Data.has_value(); }
};

class QueryContext {
  IRTypeContext& TypeCtx;
  DenseMap<QueryID, QueryResult> Cache;
  DependencyGraph DepGraph;
  IRModule* TargetModule = nullptr;
  uint64_t NextQueryID = 1;
public:
  explicit QueryContext(IRTypeContext& Ctx);
  QueryResult query(QueryID ID, std::function<QueryResult()> Compute);
  void invalidate(QueryID ID);
  void invalidateAll();
  DependencyGraph& getDependencyGraph() { return DepGraph; }
  void setTargetModule(IRModule* M) { TargetModule = M; }
  IRModule* getTargetModule() const { return TargetModule; }
  size_t getCacheSize() const { return Cache.size(); }
  void clearCache() { Cache.clear(); }
};

}
```

#### 实现约束

1. query() 语义：先查缓存→命中返回→未命中计算→存入缓存→返回
2. invalidate(ID) 同时失效所有依赖者（通过 DependencyGraph 反向遍历）
3. QueryResult 使用 std::any 存储结果，类型安全

#### 验收标准

```cpp
// V1: 缓存命中
QueryContext QC(Ctx);
int CallCount = 0;
auto Fn = [&]() { CallCount++; return QueryResult::makeVoid(); };
QC.query(1, Fn);
QC.query(1, Fn);
assert(CallCount == 1);  // 第二次命中缓存

// V2: 失效传播
QC.query(2, Fn);
QC.query(3, Fn);
// 假设 3 依赖 2
QC.getDependencyGraph().recordDependency(3, {2});
QC.invalidate(2);
// 3 的缓存也应失效
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-01：QueryContext + 基础查询实现" && git push origin HEAD
> ```

---

### Task F-02：依赖图追踪 + 指纹计算

#### 依赖

- F-01（QueryContext）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/DependencyGraph.h` |
| 新增 | `src/IR/DependencyGraph.cpp` |

#### 必须实现的类型定义

```cpp
namespace blocktype::ir {

using Fingerprint = uint64_t;

class DependencyGraph {
  DenseMap<QueryID, SmallVector<QueryID, 4>> Dependencies;
  DenseMap<QueryID, SmallVector<QueryID, 4>> Dependents;
  DenseMap<QueryID, Fingerprint> Fingerprints;
public:
  void recordDependency(QueryID Dependent, ArrayRef<QueryID> Dependencies);
  SmallVector<QueryID, 4> getDependencies(QueryID ID) const;
  SmallVector<QueryID, 4> getDependents(QueryID ID) const;
  void setFingerprint(QueryID ID, Fingerprint FP);
  Fingerprint getFingerprint(QueryID ID) const;
  bool hasFingerprintChanged(QueryID ID, Fingerprint CurrentFP) const;
  SmallVector<QueryID, 16> getTransitiveDependents(QueryID ID) const;
  bool hasCycle() const;
  size_t size() const { return Dependencies.size(); }
};

Fingerprint computeFingerprint(StringRef Data);
Fingerprint computeFingerprint(const IRModule& M);
Fingerprint computeFingerprint(const IRFunction& F);

}
```

#### 实现约束

1. Fingerprint 使用 XXHash64（稳定、确定性）
2. 依赖图是 DAG，hasCycle() 检测循环依赖
3. getTransitiveDependents() 返回所有传递依赖者（BFS 遍历）

#### 验收标准

```cpp
// V1: 依赖记录和查询
DependencyGraph DG;
DG.recordDependency(3, {1, 2});
auto Deps = DG.getDependencies(3);
assert(Deps.size() == 2);

// V2: 反向依赖
auto DepsOf1 = DG.getDependents(1);
assert(DepsOf1.size() == 1 && DepsOf1[0] == 3);

// V3: 指纹计算
auto FP1 = computeFingerprint("hello");
auto FP2 = computeFingerprint("hello");
auto FP3 = computeFingerprint("world");
assert(FP1 == FP2);
assert(FP1 != FP3);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-02：依赖图追踪 + 指纹计算" && git push origin HEAD
> ```

---

### Task F-03：InstructionSelector 声明式规则引擎

#### 依赖

- D-F1（InstructionSelector 接口定义）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Backend/DeclRuleEngine.cpp` |

#### 必须实现的类型定义

```cpp
namespace blocktype::backend {

struct LoweringRule {
  ir::Opcode SourceOp;
  ir::dialect::DialectID SourceDialect = ir::dialect::DialectID::Core;
  std::string TargetPattern;
  std::string Condition;
  int Priority = 1;
};

class DeclRuleEngine : public InstructionSelector {
  SmallVector<LoweringRule, 64> Rules;
  DenseMap<unsigned, SmallVector<unsigned, 4>> OpcodeToRuleIndices;
public:
  bool select(const ir::IRInstruction& I, TargetInstructionList& Output) override;
  bool loadRules(StringRef RuleFile) override;
  bool verifyCompleteness() override;
  void addRule(LoweringRule R);
  size_t getNumRules() const { return Rules.size(); }
};

}
```

#### 实现约束

1. 多规则匹配时选择最高优先级
2. verifyCompleteness() 检查所有 Opcode 都有至少一条规则
3. Condition 解析支持简单表达式（bitwidth==32, isSigned==false 等）

#### 验收标准

```cpp
// V1: 规则匹配
DeclRuleEngine Engine;
Engine.addRule({ir::Opcode::Add, ir::dialect::DialectID::Core, "add32", "", 1});
Engine.addRule({ir::Opcode::Add, ir::dialect::DialectID::Core, "inc32", "rhs==1", 2});
// add i32 %x, 1 → inc32（优先级2胜出）

// V2: 穷尽性检查
assert(Engine.verifyCompleteness() == false);  // 未覆盖所有 Opcode
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-03：InstructionSelector 声明式规则引擎" && git push origin HEAD
> ```

---

### Task F-04：声明式规则文件(.isle)解析器

#### 依赖

- F-03（DeclRuleEngine）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Backend/ISLEParser.cpp` |

#### .isle 文件格式

```
;; BTIR → x86_64 指令选择规则
(rule (lower (add i32 %x %y)) (add32 %x %y) :priority 1)
(rule (lower (add i64 %x %y)) (add64 %x %y) :priority 1)
(rule (lower (add i32 %x (const i32 1))) (inc32 %x) :priority 2)
(rule (lower (mul i32 %x %y)) (imul32 %x %y) :priority 1)
(rule (lower (load i32 %ptr)) (mov32 (mem %ptr)) :priority 1)
(rule (lower (store i32 %val %ptr)) (mov32 (mem %ptr) %val) :priority 1)
```

#### 实现约束

1. 变量绑定 `%x`、注释 `;;`
2. 解析错误时发出 diag 并跳过该规则
3. 解析结果加载到 DeclRuleEngine

#### 验收标准

```cpp
// V1: 解析 .isle 文件
DeclRuleEngine Engine;
bool OK = Engine.loadRules("x86_64.isle");
assert(OK == true);
assert(Engine.getNumRules() > 0);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-04：声明式规则文件(.isle)解析器" && git push origin HEAD
> ```

---

### Task F-05：PluginManager IR Pass 插件注册

> **Spec 修复说明（2026-05-01）**：原 spec 定义的 PluginManager 接口（instance() 单例、
> loadPlugin(StringRef)、unloadPlugin(StringRef)等）与现有代码
> `include/blocktype/Frontend/PluginManager.h` 中 `blocktype::plugin::PluginManager` 完全不同。
> 现有 PluginManager 使用 registerPlugin(unique_ptr\<CompilerPlugin\>)、unregisterPlugin(StringRef)
> 等方法，无单例、无 loadPlugin。
> 本修复基于现有接口进行扩展：在现有 PluginManager 上新增 Pass 注册回调（PassCreatorFn）
> 和 PassManager 关联功能，不破坏已有接口。

#### 依赖

- C-F1（PluginManager 基础框架）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 修改 | `include/blocktype/Frontend/PluginManager.h` |
| 修改 | `src/Frontend/PluginManager.cpp` |

#### 必须实现的接口（在现有 plugin::PluginManager 上扩展）

```cpp
namespace blocktype::plugin {

/// Pass 工厂函数类型
using PassCreatorFn = std::function<std::unique_ptr<ir::Pass>()>;

class PluginManager {
  // === 现有成员保持不变 ===
  ir::DenseMap<std::string, std::unique_ptr<CompilerPlugin>> LoadedPlugins;

  // === 新增成员 ===
  ir::PassManager* PM_ = nullptr;
  ir::DenseMap<std::string, PassCreatorFn> PassCreators_;

public:
  // === 现有方法保持不变 ===
  bool registerPlugin(std::unique_ptr<CompilerPlugin> Plugin);
  bool unregisterPlugin(ir::StringRef Name);
  CompilerPlugin* getPlugin(ir::StringRef Name) const;
  void registerPluginPasses(CompilerInstance& CI);
  void listPlugins(ir::raw_ostream& OS) const;
  size_t getPluginCount() const;

  // === 新增方法 ===

  /// 设置 PassManager，后续 createAndRegisterPasses 将 Pass 添加到此 PM。
  void setPassManager(ir::PassManager* PM) { PM_ = PM; }

  /// 注册一个 Pass 工厂函数。PassName 重复返回 false。
  bool registerPassCreator(ir::StringRef PassName, PassCreatorFn Creator);

  /// 查找已注册的 Pass 工厂函数。未找到返回 nullptr。
  const PassCreatorFn* getPassCreator(ir::StringRef PassName) const;

  /// 将所有已注册的 PassCreator 创建的 Pass 添加到 PassManager。
  /// 需要先调用 setPassManager()。返回添加的 Pass 数量。
  unsigned createAndRegisterPasses();
};

} // namespace plugin
```

#### 实现约束

1. 在现有 PluginManager.h/cpp 中添加新成员和方法，**不破坏已有接口**
2. `registerPassCreator()` 使用 PassName 去重，重名返回 false
3. `createAndRegisterPasses()` 遍历 PassCreators\_，对每个调用 Creator 生成 Pass 实例，
   通过 `PM_->addPass()` 添加到管线
4. `getPassCreator()` 返回 const 指针，不允许外部修改注册表
5. 所有新代码在 `namespace blocktype::plugin` 中
6. 新增头文件 include 依赖：`blocktype/IR/IRPass.h`、`blocktype/IR/IRPasses.h`

#### 验收标准

```cpp
// V1: 设置 PassManager 并注册 Pass Creator
plugin::PluginManager PMgr;
ir::PassManager IRPM;
PMgr.setPassManager(&IRPM);
bool OK = PMgr.registerPassCreator("my-opt",
  []() -> std::unique_ptr<ir::Pass> {
    return std::make_unique<MyOptPass>();
  });
assert(OK == true);

// V2: 查找 Pass Creator
auto* Creator = PMgr.getPassCreator("my-opt");
assert(Creator != nullptr);

// V3: 重名注册失败
bool OK2 = PMgr.registerPassCreator("my-opt",
  []() -> std::unique_ptr<ir::Pass> { return nullptr; });
assert(OK2 == false);

// V4: 创建并注册所有 Pass
unsigned Count = PMgr.createAndRegisterPasses();
assert(Count == 1);
assert(IRPM.size() == 1);

// V5: 现有接口不受影响
PMgr.registerPlugin(std::make_unique<TestPlugin>());
assert(PMgr.getPluginCount() == 1);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-05：PluginManager IR Pass 插件注册" && git push origin HEAD
> ```

---

### Task F-06：BTIR_PASS_PLUGIN 注册宏

> **Spec 修复说明（2026-05-01）**：原宏使用 PluginManager::instance()（不存在的单例）和
> 不存在的 registerPassCreator 签名。修复为使用 plugin::PluginManager 实例 + F-05 新增的
> registerPassCreator(PassName, PassCreatorFn) 方法。宏改为需要显式传入 PluginManager 实例引用。

#### 依赖

- F-05（PluginManager Pass 注册扩展）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/PluginMacros.h` |

#### 必须实现的宏

```cpp
#include "blocktype/Frontend/PluginManager.h"

/// BTIR_PASS_PLUGIN — 向 PluginManager 注册一个 IR Pass 工厂。
/// @param PassClass  Pass 子类名（必须继承 ir::Pass）
/// @param PluginName Pass 注册名称（字符串字面量）
/// @param PMgr       plugin::PluginManager 实例引用
///
/// 用法：
///   plugin::PluginManager PMgr;
///   BTIR_PASS_PLUGIN(MyOptPass, "my-opt", PMgr)
///
#define BTIR_PASS_PLUGIN(PassClass, PluginName, PMgr) \
  static bool PassClass##_registered_ = \
    (PMgr).registerPassCreator(PluginName, \
      []() -> std::unique_ptr<blocktype::ir::Pass> { \
        return std::make_unique<PassClass>(); \
      });
```

#### 实现约束

1. 不依赖 RTTI
2. 编译期类型检查：PassClass 必须是 `ir::Pass` 子类（由 `std::make_unique<PassClass>()` 模板推导保证）
3. 返回 `bool` 可用于 static_assert 或条件检查
4. 不使用全局单例，通过参数 `PMgr` 显式传入 PluginManager 实例
5. 宏内使用 `static bool` 变量确保注册在 `main()` 之前执行（用于静态注册场景）

#### 验收标准

```cpp
// V1: 宏展开后可注册 Pass
plugin::PluginManager PMgr;
ir::PassManager IRPM;
PMgr.setPassManager(&IRPM);
BTIR_PASS_PLUGIN(MyOptPass, "my-opt", PMgr)
const auto* Creator = PMgr.getPassCreator("my-opt");
assert(Creator != nullptr);

// V2: 注册后可创建 Pass 实例
auto PassObj = (*Creator)();
assert(PassObj != nullptr);

// V3: 创建并执行 Pass 到 PassManager
unsigned Count = PMgr.createAndRegisterPasses();
assert(Count == 1);
assert(IRPM.size() == 1);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-06：BTIR_PASS_PLUGIN 注册宏" && git push origin HEAD
> ```

---

### Task F-07：IRStatistics 统计特征提取

#### 依赖

- Phase A（IRModule）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRStatistics.h` |
| 新增 | `src/IR/IRStatistics.cpp` |

#### 必须实现的类型定义

```cpp
namespace blocktype::ir {

class IRStatistics {
  unsigned NumFunctions = 0;
  unsigned NumBasicBlocks = 0;
  unsigned NumInstructions = 0;
  unsigned NumGlobalVars = 0;
  unsigned NumIntegerOps = 0;
  unsigned NumFloatOps = 0;
  unsigned NumMemoryOps = 0;
  unsigned NumCallOps = 0;
  unsigned NumBranchOps = 0;
  unsigned MaxFunctionSize = 0;
  double AvgFunctionSize = 0.0;
  unsigned NumRecursiveFunctions = 0;
  unsigned InstructionComplexity = 0;
public:
  static IRStatistics compute(const IRModule& M);
  unsigned getNumFunctions() const { return NumFunctions; }
  unsigned getNumInstructions() const { return NumInstructions; }
  double getAvgFunctionSize() const { return AvgFunctionSize; }
  std::string toJSON() const;
};

}
```

#### 实现约束

1. compute() 对 IRModule 只读操作
2. 时间复杂度 O(n)，n = 总指令数
3. toJSON() 输出 JSON 格式统计

#### 验收标准

```cpp
// V1: 统计计算
auto Stats = IRStatistics::compute(*Module);
assert(Stats.getNumFunctions() == Module->getNumFunctions());
assert(Stats.getNumInstructions() > 0);

// V2: JSON 输出
auto JSON = Stats.toJSON();
assert(JSON.find("NumFunctions") != std::string::npos);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-07：IRStatistics 统计特征提取" && git push origin HEAD
> ```

---

### Task F-08：AIAutoTuner 接口定义 + 桩实现

#### 依赖

- F-07（IRStatistics）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/AIAutoTuner.h` |
| 新增 | `src/IR/AIAutoTuner.cpp` |

#### 必须实现的类型定义

```cpp
namespace blocktype::ir {

using PassSequence = SmallVector<StringRef, 16>;

struct PassSequenceEvaluation {
  PassSequence Sequence;
  double Score = 0.0;
  unsigned CompileTimeMs = 0;
  unsigned CodeSizeBytes = 0;
};

class AIAutoTuner {
public:
  virtual ~AIAutoTuner() = default;
  virtual PassSequence recommendPassSequence(const IRStatistics& Stats) = 0;
  virtual void recordFeedback(const PassSequenceEvaluation& Eval) = 0;
};

class StubAIAutoTuner : public AIAutoTuner {
  PassSequence DefaultSequence;
public:
  StubAIAutoTuner();
  PassSequence recommendPassSequence(const IRStatistics& Stats) override;
  void recordFeedback(const PassSequenceEvaluation& Eval) override;
};

}
```

#### 实现约束

1. recommendPassSequence 仅返回建议，不自动执行
2. StubAIAutoTuner 返回默认 Pass 序列（-O2 标准）
3. recordFeedback 存储但不使用（为 RL 训练积累数据）

#### 验收标准

```cpp
// V1: 桩实现返回默认序列
StubAIAutoTuner Tuner;
auto Seq = Tuner.recommendPassSequence(Stats);
assert(!Seq.empty());

// V2: 反馈记录
Tuner.recordFeedback({Seq, 0.95, 100, 2048});
// 不崩溃
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-08：AIAutoTuner 接口定义 + 桩实现" && git push origin HEAD
> ```

---

### Task F-09：IRRemoteCacheClient 远程缓存客户端

> **Spec 修复说明（2026-05-01）**：原 spec 定义 `blocktype::ir::RemoteCache`，但
> `include/blocktype/IR/IRCache.h` 已有 `blocktype::cache::RemoteCache`（stub 类），
> 类名冲突。修复方案：重命名为 `IRRemoteCacheClient`，放在 `blocktype::cache` 命名空间，
> 继承现有 `cache::CacheStorage` 抽象基类，与现有 `cache::RemoteCache` 并存不冲突。

#### 依赖

- E-F1（CompilationCacheManager）
- IRCache.h（现有 CacheStorage、CacheKey、CacheEntry 类型）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRRemoteCacheClient.h` |
| 新增 | `src/IR/IRRemoteCacheClient.cpp` |

#### 必须实现的类型定义

```cpp
#ifndef BLOCKTYPE_IR_IRREMOTECACHECLIENT_H
#define BLOCKTYPE_IR_IRREMOTECACHECLIENT_H

#include "blocktype/IR/IRCache.h"

namespace blocktype {
namespace cache {

/// IRRemoteCacheClient — 增强版远程缓存客户端
/// 继承自 CacheStorage 接口，提供 HTTPS 传输、签名验证、超时重试等功能。
/// 与现有 RemoteCache（stub）并存，可替换使用。
class IRRemoteCacheClient : public CacheStorage {
  std::string Endpoint_;
  std::string Bucket_;
  uint64_t TimeoutMs_ = 5000;
  unsigned MaxRetries_ = 3;
  Stats Stats_;

public:
  explicit IRRemoteCacheClient(ir::StringRef URL, ir::StringRef Bucket = "default");

  /// CacheStorage 接口实现
  std::optional<CacheEntry> lookup(const CacheKey& Key) override;
  bool store(const CacheKey& Key, const CacheEntry& Entry) override;
  Stats getStats() const override { return Stats_; }

  /// 增强功能
  bool verifySignature(const CacheEntry& Entry) const;
  void setTimeout(uint64_t Ms) { TimeoutMs_ = Ms; }
  void setMaxRetries(unsigned N) { MaxRetries_ = N; }
  uint64_t getTimeout() const { return TimeoutMs_; }
  unsigned getMaxRetries() const { return MaxRetries_; }
  ir::StringRef getEndpoint() const { return Endpoint_; }
  ir::StringRef getBucket() const { return Bucket_; }
};

} // namespace cache
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRREMOTECACHECLIENT_H
```

#### 实现约束

1. HTTPS 传输（当前阶段为**桩实现**：lookup 返回 nullopt，store 返回 false）
2. 缓存条目签名验证（verifySignature 桩实现：当前返回 true，预留签名验证接口）
3. 超时和重试机制（配置接口就绪，逻辑桩实现）
4. 连接失败时优雅降级（返回 nullopt/false，不崩溃，不抛异常）
5. 继承 CacheStorage 接口，可被 CompilationCacheManager 的 Storage\_ 替换使用
6. 不修改现有 IRCache.h 中的任何代码

#### 验收标准

```cpp
// V1: 构造和配置
cache::IRRemoteCacheClient Client("https://cache.example.com", "my-bucket");
Client.setTimeout(3000);
Client.setMaxRetries(5);
assert(Client.getEndpoint() == "https://cache.example.com");
assert(Client.getBucket() == "my-bucket");
assert(Client.getTimeout() == 3000);
assert(Client.getMaxRetries() == 5);

// V2: 远程查找（桩实现，连接失败返回 nullopt，不崩溃）
cache::CacheKey Key = cache::CacheKey::compute("test", cache::CacheOptions{});
auto Entry = Client.lookup(Key);
// 桩实现：返回 nullopt

// V3: 远程存储（桩实现，返回 false，不崩溃）
cache::CacheEntry SomeEntry;
bool Stored = Client.store(Key, SomeEntry);
// 桩实现：返回 false

// V4: 签名验证（桩实现）
bool Valid = Client.verifySignature(SomeEntry);
// 桩实现：返回 true

// V5: 统计信息可访问
auto Stats = Client.getStats();
// Stats.Hits 和 Stats.Misses 可访问（初始均为 0）
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-09：IRRemoteCacheClient 远程缓存客户端" && git push origin HEAD
> ```

---

### Task F-10：FFI Rust 语言映射

#### 依赖

- C-F4（FFITypeMapper C 语言映射）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Backend/FFIRustMapper.cpp` |

#### Rust 类型映射规则

| Rust 类型 | IRType | 约束 |
|-----------|--------|------|
| `repr(C)` 结构体 | IRStructType | 零开销映射 |
| `repr(Rust)` 结构体 | IROpaqueType | 不支持，报错 |
| `i32/u32` | IRIntegerType(32) | — |
| `f64` | IRFloatType(64) | — |
| `*const T/*mut T` | IRPointerType(mapType(T)) | — |
| `Option<T>` | 不支持 | 报错 |
| `Result<T, E>` | 不支持 | 报错 |

#### 验收标准

```cpp
// V1: repr(C) 结构体映射
auto* Ty = RustMapper.mapRustType(ReprCStruct);
assert(isa<ir::IRStructType>(Ty));

// V2: repr(Rust) 结构体报错
auto* Ty2 = RustMapper.mapRustType(ReprRustStruct);
assert(isa<ir::IROpaqueType>(Ty2));  // 不支持→Opaque
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-10：FFI Rust 语言映射" && git push origin HEAD
> ```

---

### Task F-11：FFI WASM 调用约定

#### 依赖

- C-F4（FFITypeMapper）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Backend/FFIWasmMapper.cpp` |

#### WASM 类型映射规则

| WASM 值类型 | IRType |
|-------------|--------|
| `i32` | IRIntegerType(32) |
| `i64` | IRIntegerType(64) |
| `f32` | IRFloatType(32) |
| `f64` | IRFloatType(64) |
| `externref` | IRPointerType(IRVoidType) |

#### 验收标准

```cpp
// V1: WASM 类型映射
auto* Ty = WasmMapper.mapWasmType(WasmI32);
assert(isa<ir::IRIntegerType>(Ty) && cast<ir::IRIntegerType>(Ty)->getBitWidth() == 32);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-11：FFI WASM 调用约定" && git push origin HEAD
> ```

---

### Task F-12：IRFuzzer 基础框架 + Delta Debugging 最小化

> **Spec 修复说明（2026-05-01）**：原 spec 依赖 D-F8（IRFuzzer 基础），但
> `tests/fuzz/IRFuzzer.cpp` 文件不存在，D-F8 从未实现。
> 修复方案：将 F-12 改为自包含 task，包含 IRFuzzer 基础框架（头文件 + 实现）+ delta debugging 最小化功能。

#### 依赖

- Phase A（IRModule、IRTypeContext 等基础 IR 类型）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRFuzzer.h` |
| 新增 | `src/IR/IRFuzzer.cpp` |
| 新增 | `tests/fuzz/IRFuzzer.cpp` |

#### 必须实现的类型定义

```cpp
#ifndef BLOCKTYPE_IR_IRFUZZER_H
#define BLOCKTYPE_IR_IRFUZZER_H

#include <cstdint>
#include <functional>
#include <vector>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

/// OracleFn — 判断输入是否触发目标条件（崩溃/断言失败等）
using OracleFn = std::function<bool(ir::ArrayRef<uint8_t>)>;

/// IRFuzzer — IR 模糊测试与 Delta Debugging 工具
class IRFuzzer {
  unsigned MaxInputSize_ = 4096;
  unsigned Seed_ = 42;

public:
  /// 用种子输入 + oracle 做 delta debugging 最小化。
  /// 使用经典 DD 算法（二分递归），每次尝试移除一半输入。
  /// 返回仍触发 oracle 的最小化输入（目标 ≤ 原始 50%）。
  /// 如果无法最小化，返回原始输入。
  static std::vector<uint8_t> deltaDebug(ir::ArrayRef<uint8_t> TriggeringInput,
                                          OracleFn Oracle);

  /// 随机生成一个 IR 输入字节序列（用于 fuzzing 基础框架）。
  /// 使用确定性伪随机（基于 Seed_）。
  std::vector<uint8_t> generateRandomInput(size_t Size);

  /// 对输入进行随机变异（字节级变异）。
  /// 变异策略：翻转随机位、替换随机字节、删除随机子序列。
  static std::vector<uint8_t> mutate(ir::ArrayRef<uint8_t> Input, unsigned Seed);

  void setMaxInputSize(unsigned N) { MaxInputSize_ = N; }
  void setSeed(unsigned S) { Seed_ = S; }
  unsigned getMaxInputSize() const { return MaxInputSize_; }
  unsigned getSeed() const { return Seed_; }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRFUZZER_H
```

#### 实现约束

1. **deltaDebug 算法**：经典 DD（Delta Debugging）算法 —— 二分递归，每轮尝试移除一半输入，
   如果 oracle 仍触发则保留较短的，否则恢复。循环直到无法进一步缩小。
2. **保持触发条件**：最小化后的输入仍必须触发 oracle（`Oracle(Minimized) == true`）
3. **结果目标**：最小化结果 ≤ 原始输入的 50%（如果 oracle 允许）
4. **mutate 变异策略**：基于 Seed 的确定性变异 —— 随机翻转位、替换字节、删除子序列
5. **generateRandomInput**：使用 Seed_ 的确定性伪随机数生成，输入大小由参数 Size 决定
6. **不依赖外部 fuzzing 库**（如 libFuzzer/AFL），纯自包含实现

#### 验收标准

```cpp
// V1: delta debugging 最小化
// 创建 100 字节全 0x42 的输入
auto TriggeringInput = std::vector<uint8_t>(100, 0x42);
// Oracle: 输入长度 >= 3 且前 3 字节均为 0x42 即触发
auto Oracle = [](ir::ArrayRef<uint8_t> Input) -> bool {
  return Input.size() >= 3
         && Input[0] == 0x42 && Input[1] == 0x42 && Input[2] == 0x42;
};
auto Minimized = ir::IRFuzzer::deltaDebug(TriggeringInput, Oracle);
assert(Minimized.size() <= TriggeringInput.size() / 2);
assert(Oracle(ir::ArrayRef<uint8_t>(Minimized.data(), Minimized.size())) == true);

// V2: 随机输入生成
ir::IRFuzzer Fuzzer;
Fuzzer.setSeed(12345);
auto RandInput = Fuzzer.generateRandomInput(64);
assert(RandInput.size() == 64);
// 确定性：相同 seed 生成相同输入
Fuzzer.setSeed(12345);
auto RandInput2 = Fuzzer.generateRandomInput(64);
assert(RandInput == RandInput2);

// V3: 变异
auto Mutated = ir::IRFuzzer::mutate(RandInput, 999);
assert(Mutated.size() > 0);
// 变异后不应与原始完全相同（大概率不同）
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task F-12：IRFuzzer 基础框架 + Delta Debugging 最小化" && git push origin HEAD
> ```

---

## Phase G：增量编译期

### Task G-01：红绿标记算法实现

#### 依赖

- F-01（QueryContext）
- F-02（DependencyGraph）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/RedGreenMarker.h` |
| 新增 | `src/IR/RedGreenMarker.cpp` |

#### 必须实现的类型定义

```cpp
namespace blocktype::ir {

enum class MarkColor { Green, Red, Unknown };

class RedGreenMarker {
  QueryContext& QC;
  DependencyGraph& DG;
  DenseMap<QueryID, MarkColor> Marks;
public:
  explicit RedGreenMarker(QueryContext& Q);
  MarkColor tryMarkGreen(QueryID ID);
  MarkColor getMark(QueryID ID) const;
  void markRed(QueryID ID);
  void reset();
  size_t getGreenCount() const;
  size_t getRedCount() const;
};

}
```

#### tryMarkGreen 精确算法

```
1. 如果 ID 已标记为 Green → 返回 Green
2. 获取 ID 的所有依赖 Deps
3. 对每个 Dep 递归 tryMarkGreen：
   a. 如果 Dep 为 Red → 标记 ID 为 Red，返回 Red
   b. 如果 Dep 为 Unknown → 递归 tryMarkGreen(Dep)
4. 所有依赖都为 Green：
   a. 计算当前指纹 FP_current
   b. 对比缓存指纹 FP_cached
   c. 如果 FP_current == FP_cached → 假阳性优化：重新计算对比结果
   d. 如果结果一致 → 标记 Green，返回 Green
   e. 如果不一致 → 标记 Red，返回 Red
```

#### 验收标准

```cpp
// V1: 无变化→全绿
RedGreenMarker Marker(QC);
auto Color = Marker.tryMarkGreen(RootQueryID);
assert(Color == MarkColor::Green);

// V2: 有变化→红
// 修改某个依赖的源文件
auto Color2 = Marker.tryMarkGreen(RootQueryID);
assert(Color2 == MarkColor::Red);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task G-01：红绿标记算法实现" && git push origin HEAD
> ```

---

### Task G-02：查询缓存持久化到磁盘

#### 依赖

- F-01（QueryContext）
- A.7（IRSerializer）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/QueryCacheSerializer.h` |
| 新增 | `src/IR/QueryCacheSerializer.cpp` |

#### 必须实现的类型定义

```cpp
class QueryCacheSerializer {
  StringRef CacheDir;
public:
  explicit QueryCacheSerializer(StringRef Dir);
  bool save(const QueryContext& QC, StringRef ModuleName);
  bool load(QueryContext& QC, StringRef ModuleName);
  bool isValid(StringRef ModuleName) const;
  bool invalidate(StringRef ModuleName);
};
```

#### 实现约束

1. IRFormatVersion 兼容性检查
2. 损坏文件优雅降级（删除并重建）
3. 缓存文件路径：`CacheDir/ModuleName.querycache`

#### 验收标准

```cpp
// V1: 保存和加载
QueryCacheSerializer Ser("/tmp/btcache");
Ser.save(QC, "test_module");
QueryContext QC2(Ctx);
assert(Ser.load(QC2, "test_module") == true);
assert(QC2.getCacheSize() == QC.getCacheSize());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task G-02：查询缓存持久化到磁盘" && git push origin HEAD
> ```

---

### Task G-03：StableDefPath 稳定标识符映射

#### 依赖

- F-01（QueryContext）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/StableDefPath.h` |
| 新增 | `src/IR/StableDefPath.cpp` |

#### 必须实现的类型定义

```cpp
class StableDefPath {
  std::string Path;  // 如 "std::vector::push_back"
  uint128_t StableHash;
public:
  explicit StableDefPath(StringRef P);
  StringRef getPath() const { return Path; }
  uint128_t getStableHash() const { return StableHash; }
  bool operator==(const StableDefPath& Other) const { return StableHash == Other.StableHash; }
};

class StableIdMap {
  DenseMap<const Decl*, StableDefPath> DeclToPath;
  DenseMap<uint128_t, const Decl*> HashToDecl;
public:
  void registerDecl(const Decl* D, StringRef Path);
  optional<StableDefPath> lookupDecl(const Decl* D) const;
  optional<const Decl*> lookupHash(uint128_t Hash) const;
};
```

#### 实现约束

1. 稳定哈希 128 位（跨编译会话一致）
2. 路径格式：`namespace::class::method`
3. 哈希不受源文件位置影响

#### 验收标准

```cpp
// V1: 稳定哈希跨会话一致
StableDefPath P1("std::vector::push_back");
StableDefPath P2("std::vector::push_back");
assert(P1.getStableHash() == P2.getStableHash());

// V2: 不同路径不同哈希
StableDefPath P3("std::vector::pop_back");
assert(P1.getStableHash() != P3.getStableHash());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task G-03：StableDefPath 稳定标识符映射" && git push origin HEAD
> ```

---

### Task G-04：投影查询：函数级增量重编译

#### 依赖

- G-01（红绿标记）
- F-01（QueryContext）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/ProjectionQuery.h` |
| 新增 | `src/IR/ProjectionQuery.cpp` |

#### 必须实现的类型定义

```cpp
class ProjectionQuery {
  QueryContext& QC;
public:
  explicit ProjectionQuery(QueryContext& Q);
  unique_ptr<IRModule> projectFunction(const IRModule& M, StringRef FunctionName);
  SmallVector<IRFunction*, 16> getModifiedFunctions(const IRModule& M);
  bool canReuseCompilation(const IRFunction& F);
};
```

#### 实现约束

1. 函数级粒度增量重编译
2. 未修改函数的编译结果可复用
3. 全局变量修改需重编译所有引用函数

#### 验收标准

```cpp
// V1: 函数级投影
auto Proj = ProjectionQuery(QC);
auto SubModule = Proj.projectFunction(*Module, "main");
assert(SubModule != nullptr);
assert(SubModule->getFunction("main") != nullptr);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task G-04：投影查询：函数级增量重编译" && git push origin HEAD
> ```

---

### Task G-05：增量编译与构建系统集成

#### 依赖

- G-04（投影查询）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Frontend/IncrementalBuildIntegration.cpp` |

#### 实现约束

1. 输出 `.d` 依赖文件（Make/Ninja 格式）
2. 支持 Make 和 Ninja 构建系统
3. 增量编译时仅重编译修改的函数

#### 验收标准

```bash
# V1: 依赖文件输出
# blocktype --dep-file=test.d test.cpp
# cat test.d | grep "test.o:"
# 输出非空
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task G-05：增量编译与构建系统集成" && git push origin HEAD
> ```

---

### Task G-06：IREquivalenceChecker 符号执行验证

#### 依赖

- E-F3（IREquivalenceChecker）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/SymbolicExecutor.h` |
| 新增 | `src/IR/SymbolicExecutor.cpp` |

#### 必须实现的类型定义

```cpp
class SymbolicExecutor {
  unsigned MaxPaths = 1000;
  unsigned TimeoutMs = 30000;
public:
  struct EquivalenceResult {
    bool IsEquivalent;
    std::string Counterexample;
    double PathCoverage;
  };
  EquivalenceResult checkEquivalence(const IRFunction& F1, const IRFunction& F2);
  void setMaxPaths(unsigned N) { MaxPaths = N; }
  void setTimeout(unsigned Ms) { TimeoutMs = Ms; }
};
```

#### 实现约束

1. 路径覆盖 ≥ 80%
2. 超时保护 ≤ 30 秒/函数
3. 不等价时生成反例

#### 验收标准

```cpp
// V1: 等价函数
auto Result = Executor.checkEquivalence(F1, F2);
assert(Result.IsEquivalent == true);
assert(Result.PathCoverage >= 0.8);

// V2: 不等价函数
auto Result2 = Executor.checkEquivalence(F1, F3);
assert(Result2.IsEquivalent == false);
assert(!Result2.Counterexample.empty());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task G-06：IREquivalenceChecker 符号执行验证" && git push origin HEAD
> ```

---

### Task G-07：编译缓存统计与自动清理

#### 依赖

- E-F1（CompilationCacheManager）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Frontend/CacheStatsCollector.cpp` |
| 新增 | `src/Frontend/CacheCleaner.cpp` |

#### 实现约束

1. LRU 清理策略
2. 命中率统计
3. 最大缓存大小可配置

#### 验收标准

```bash
# V1: 缓存统计
# blocktype --fcache-stats
# 输出包含 "hit rate" 和 "total size"

# V2: 自动清理
# blocktype --fcache-size=1G
# 缓存大小不超过 1GB
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task G-07：编译缓存统计与自动清理" && git push origin HEAD
> ```

---

### Task G-08：诊断交叉引用链

#### 依赖

- B-F6（StructuredDiagEmitter）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/DiagnosticCrossRef.h` |

#### 实现约束

1. 诊断链深度 ≤ 5
2. 循环引用检测
3. JSON 序列化

#### 验收标准

```cpp
// V1: 交叉引用链
DiagnosticCrossRef Ref;
Ref.addLink(Diag1, Diag2, "caused by");
Ref.addLink(Diag2, Diag3, "related to");
auto Chain = Ref.getChain(Diag1);
assert(Chain.size() <= 5);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task G-08：诊断交叉引用链" && git push origin HEAD
> ```

---

## Phase H：AI 辅助与极致优化期

### Task H-01：AI Pass 序列推荐引擎

#### 依赖

- F-08（AIAutoTuner）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/AIPassRecommender.h` |

#### 必须实现的类型定义

```cpp
class AIPassRecommender {
  AIAutoTuner& Tuner;
  PassSequence DefaultBaseline;
public:
  explicit AIPassRecommender(AIAutoTuner& T);
  PassSequence recommend(const IRStatistics& Stats);
  void setDefaultBaseline(PassSequence Seq);
  bool isRecommendationValid(const PassSequence& Seq) const;
};
```

#### 实现约束

1. 推荐仅作为建议，不自动执行
2. 无有效推荐时回退到默认基线
3. 推荐结果可验证（isRecommendationValid 检查 Pass 名称存在性）

#### 验收标准

```cpp
// V1: 推荐返回有效序列
AIPassRecommender Rec(Tuner);
auto Seq = Rec.recommend(Stats);
assert(Rec.isRecommendationValid(Seq));
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-01：AI Pass 序列推荐引擎" && git push origin HEAD
> ```

---

### Task H-02：RL 训练数据收集管线

#### 依赖

- F-08（AIAutoTuner）
- F-07（IRStatistics）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/RLDataCollector.h` |

#### 必须实现的类型定义

```cpp
class RLDataCollector {
  uint64_t MaxDataSizeBytes = 1073741824;  // 1GB
  uint64_t CurrentSizeBytes = 0;
public:
  struct DataPoint {
    IRStatistics Stats;
    PassSequence Sequence;
    PassSequenceEvaluation Eval;
  };
  void record(const DataPoint& DP);
  bool exportData(StringRef Path) const;
  uint64_t getDataSize() const { return CurrentSizeBytes; }
  bool isFull() const { return CurrentSizeBytes >= MaxDataSizeBytes; }
};
```

#### 实现约束

1. 数据量限制 ≤ 1GB/会话
2. 数据三元组 (Stats, Sequence, Eval)
3. 导出为 JSON 格式

#### 验收标准

```cpp
// V1: 数据收集
RLDataCollector Collector;
Collector.record({Stats, Seq, Eval});
assert(Collector.getDataSize() > 0);

// V2: 导出
assert(Collector.exportData("/tmp/rl_data.json") == true);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-02：RL 训练数据收集管线" && git push origin HEAD
> ```

---

### Task H-03：AI 调优模型集成接口

#### 依赖

- H-01（AIPassRecommender）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/AIModelIntegration.h` |

#### 必须实现的类型定义

```cpp
class AIModelIntegration {
  AIAutoTuner& Tuner;
  bool ModelLoaded = false;
public:
  explicit AIModelIntegration(AIAutoTuner& T);
  bool loadModel(StringRef ModelPath);
  bool isModelLoaded() const { return ModelLoaded; }
  PassSequence predict(const IRStatistics& Stats);
  void fallbackToRuleEngine();
};
```

#### 实现约束

1. ML 模型接口抽象（不依赖具体框架）
2. 模型加载可选（加载失败回退到规则引擎）
3. 不自动下载模型

#### 验收标准

```cpp
// V1: 无模型时回退
AIModelIntegration AI(Tuner);
auto Seq = AI.predict(Stats);
assert(!Seq.empty());  // 回退到规则引擎仍返回有效序列

// V2: 加载模型
assert(AI.loadModel("/path/to/model") == true || AI.isModelLoaded() == false);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-03：AI 调优模型集成接口" && git push origin HEAD
> ```

---

### Task H-04：自动增量：文件监控 + 智能重编译

#### 依赖

- G-04（投影查询）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/Frontend/FileWatcher.h` |
| 新增 | `include/blocktype/Frontend/SmartRecompiler.h` |

#### 必须实现的类型定义

```cpp
class FileWatcher {
  uint64_t DebounceMs = 100;
public:
  using Callback = std::function<void(StringRef ChangedFile)>;
  void watch(StringRef Path, Callback OnChange);
  void stop();
};

class SmartRecompiler {
  QueryContext& QC;
  ProjectionQuery& PQ;
  FileWatcher& FW;
public:
  SmartRecompiler(QueryContext& Q, ProjectionQuery& P, FileWatcher& F);
  void start(StringRef SourceDir);
  void stop();
};
```

#### 实现约束

1. 使用 inotify（Linux）/ FSEvents（macOS）
2. 去抖动：100ms 内多次修改合并
3. 仅重编译修改的函数

#### 验收标准

```cpp
// V1: 文件监控
FileWatcher FW;
bool Changed = false;
FW.watch("/tmp/test.cpp", [&](StringRef) { Changed = true; });
// 修改文件后
// assert(Changed == true);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-04：自动增量：文件监控 + 智能重编译" && git push origin HEAD
> ```

---

### Task H-05：IR 签名与验证

#### 依赖

- E-F6（IR 完整性验证）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRSigner.h` |

#### 必须实现的类型定义

```cpp
class IRSigner {
  std::vector<uint8_t> PrivateKey;
  std::vector<uint8_t> PublicKey;
public:
  static IRSigner generateKeyPair();
  static IRSigner fromPrivateKey(StringRef Key);
  std::vector<uint8_t> sign(const IRModule& M);
  bool verify(const IRModule& M, ArrayRef<uint8_t> Signature);
  StringRef getPublicKey() const;
};
```

#### 实现约束

1. Ed25519 签名算法
2. 签名验证在加载时执行
3. 签名附加到序列化输出末尾

#### 验收标准

```cpp
// V1: 签名和验证
auto Signer = IRSigner::generateKeyPair();
auto Sig = Signer.sign(*Module);
assert(Signer.verify(*Module, Sig) == true);

// V2: 篡改检测
auto Sig2 = Signer.sign(*Module);
// 修改 Module 后
assert(Signer.verify(*ModifiedModule, Sig2) == false);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-05：IR 签名与验证" && git push origin HEAD
> ```

---

### Task H-06：FFI Python 绑定自动生成

#### 依赖

- F-10（FFI Rust 映射）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Backend/FFIPythonGenerator.cpp` |

#### 必须实现的类型定义

```cpp
class FFIPythonGenerator {
  FFITypeMapper& Mapper;
public:
  explicit FFIPythonGenerator(FFITypeMapper& M);
  std::string generateBindings(const IRModule& M);
  std::string mapIRTypeToPython(ir::IRType* T);
};
```

#### 验收标准

```cpp
// V1: 生成 Python 绑定
FFIPythonGenerator Gen(Mapper);
auto Code = Gen.generateBindings(*Module);
assert(Code.find("import ctypes") != std::string::npos);
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-06：FFI Python 绑定自动生成" && git push origin HEAD
> ```

---

### Task H-07：FFI WASM Component Model

#### 依赖

- F-11（FFI WASM 调用约定）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `src/Backend/FFIWasmComponent.cpp` |

#### 实现约束

1. WASM Component Model 规范
2. 接口类型 `wasm->func`
3. 跨语言 WASM 互操作

#### 验收标准

```cpp
// V1: WASM 组件生成
auto Component = WasmComponent.generate(*Module);
assert(!Component.empty());
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-07：FFI WASM Component Model" && git push origin HEAD
> ```

---

### Task H-08：分布式编译缓存

#### 依赖

- F-09（IRRemoteCacheClient）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/DistributedCache.h` |

#### 必须实现的类型定义

```cpp
class DistributedCache {
  cache::IRRemoteCacheClient& Remote;
  ConsistentHash Ring;
  unsigned NumNodes = 1;
public:
  explicit DistributedCache(cache::IRRemoteCacheClient& R);
  void addNode(StringRef Endpoint);
  void removeNode(StringRef Endpoint);
  optional<CacheEntry> lookup(const CacheKey& Key);
  bool store(const CacheKey& Key, const CacheEntry& Entry);
};
```

#### 实现约束

1. 一致性哈希分片
2. 缓存条目签名验证
3. 节点故障优雅降级

#### 验收标准

```cpp
// V1: 分布式查找
cache::IRRemoteCacheClient RemoteClient("https://cache0.example.com");
DistributedCache DC(RemoteClient);
DC.addNode("https://cache1.example.com");
DC.addNode("https://cache2.example.com");
auto Entry = DC.lookup(Key);
// 节点故障时回退到其他节点
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-08：分布式编译缓存" && git push origin HEAD
> ```

---

### Task H-09：编译器性能回归自动检测

#### 依赖

- F-07（IRStatistics）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/PerfRegressionDetector.h` |

#### 必须实现的类型定义

```cpp
class PerfRegressionDetector {
  double RegressionThreshold = 0.10;  // 10%
  StringRef BaselineDir;
public:
  struct RegressionReport {
    bool HasRegression;
    StringRef Metric;
    double ChangePercent;
  };
  RegressionReport checkCompileTime(const IRStatistics& Current);
  RegressionReport checkCodeSize(const IRModule& Current);
  void setThreshold(double T) { RegressionThreshold = T; }
  void persistBaseline(const IRStatistics& Stats, const IRModule& M);
};
```

#### 实现约束

1. 回归阈值 ≥ 10%
2. 基线数据持久化
3. 自动报告

#### 验收标准

```cpp
// V1: 回归检测
PerfRegressionDetector Det;
Det.persistBaseline(OldStats, *OldModule);
auto Report = Det.checkCompileTime(NewStats);
if (Report.ChangePercent > 0.10) {
  assert(Report.HasRegression == true);
}
```


> ⚠️ **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(FGH): 完成 Task H-09：编译器性能回归自动检测" && git push origin HEAD
> ```

---

### Task H-10：完整形式化验证框架

#### 依赖

- G-06（SymbolicExecutor）

#### 产出文件

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/FormalVerifier.h` |

#### 必须实现的类型定义

```cpp
class FormalVerifier {
  IREquivalenceChecker& EquivChecker;
  SymbolicExecutor& SymbolicExec;
public:
  struct VerificationResult {
    bool IsCorrect;
    std::string Counterexample;
    double Coverage;
    unsigned NumPassesVerified;
  };
  VerificationResult verifyPassSemantics(const IRModule& Before, const IRModule& After);
  VerificationResult verifyEndToEnd(const IRModule& M);
};
```

#### 实现约束

1. IR Pass 语义保持验证
2. 端到端正确性验证
3. 不通过时生成反例

#### 验收标准

```cpp
// V1: Pass 语义验证
auto Result = Verifier.verifyPassSemantics(*Before, *After);
assert(Result.IsCorrect == true);

// V2: 端到端验证
auto Result2 = Verifier.verifyEndToEnd(*Module);
assert(Result2.Coverage >= 0.8);
```
