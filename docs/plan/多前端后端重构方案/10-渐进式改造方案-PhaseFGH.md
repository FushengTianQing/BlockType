# 三、渐进式改造方案 — Phase F/G/H

> 行业前沿增强远期规划：高级特性期、增量编译期、AI辅助与极致优化期
> 来源：09-行业前沿补充优化方案 §3.6

---

## Phase F：高级特性期

**目标**：引入查询式编译基础、声明式指令选择、插件系统、AI调优预留、远程缓存、FFI扩展、模糊测试增强。

### 红线对照

| 红线 | 合规性 | 说明 |
|------|--------|------|
| R1 架构优先 | ✓ | 查询式编译架构为增量编译奠定基础，不妥协现有架构 |
| R2 自由组合 | ✓ | 插件系统增强前后端可扩展性，保持独立替换能力 |
| R3 渐进式 | ✓ | Phase F 依赖 Phase A-E 完成，可独立验证 |
| R4 功能不退化 | ✓ | 所有新增功能为附加增强，不影响现有编译路径 |
| R5 接口抽象 | ✓ | InstructionSelector/RegisterAllocator 通过抽象接口交互 |
| R6 IR中间层解耦 | ✓ | 查询式编译强化IR中间层地位 |

### Task 分解

| 任务ID | 任务描述 | 依赖 | 输入 | 输出 | 实现约束 | 验收标准 | 测试用例 | 依赖文件 |
|--------|---------|------|------|------|---------|---------|---------|---------|
| F-01 | QueryContext + 基础查询实现 | Phase E 完成 | IRModule, IRTypeContext | IRQuery.h, QueryContext.h, 对应.cpp | 1.查询为纯函数语义(相同输入→相同输出) 2.查询结果带指纹 3.缓存查询结果 | 1.QueryContext::query(parseSourceFile, path)→AST 2.QueryContext::query(convertASTToIR, AST)→IRModule 3.相同查询返回缓存结果 | query(parse, "a.cpp")→非空; 二次query→缓存命中 | IRModule.h, CompilerInstance.h |
| F-02 | 依赖图追踪 + 指纹计算 | F-01 | QueryContext | DependencyGraph.h, Fingerprint.h, 对应.cpp | 1.依赖图为DAG 2.指纹使用稳定哈希(XXHash) 3.循环依赖检测 | 1.recordDependency(A,B)→A依赖B 2.指纹计算确定性(相同输入→相同指纹) 3.循环依赖→报错 | recordDependency(A,B)→A依赖B; fingerprint(相同输入)→相同值 | QueryContext.h |
| F-03 | InstructionSelector 声明式规则引擎 | D-F1 | LoweringRule, IRInstruction | InstructionSelector实现.cpp | 1.规则匹配确定性(最高优先级规则胜出) 2.穷尽性检查 3.规则加载无副作用 | 1.select(add i32)→匹配add32规则 2.verifyCompleteness()→true 3.无匹配规则→返回false | select(add i32)→add32; verifyCompleteness()→true | LoweringRule.h, IRInstruction.h |
| F-04 | 声明式规则文件(.isle)解析器 | F-03 | .isle文件格式 | ISLEParser.h, 对应.cpp | 1.解析错误有精确行号 2.支持注释(;;) 3.支持变量绑定(%x) | 1.loadRules("x86_64.isle")→成功 2.语法错误→报行号 3.解析后规则数>0 | loadRules(合法.isle)→成功; 语法错误→行号 | InstructionSelector.h |
| F-05 | PluginManager IR Pass插件注册 | C-F1 | PluginManager, CompilerPlugin | PluginManager.cpp(IR Pass注册) | 1.插件加载后自动注册IR Pass 2.插件卸载后移除已注册Pass 3.插件间无依赖顺序 | 1.loadPlugin("my_pass.so")→注册成功 2.unloadPlugin→Pass移除 3.插件Pass可被PassManager使用 | loadPlugin→注册成功; PassManager→使用插件Pass | PluginManager.h, IRPass.h |
| F-06 | BTIR_PASS_PLUGIN 注册宏 | F-05 | 宏定义 | BTIRPassPlugin.h | 1.宏展开为静态初始化代码 2.编译期类型检查 3.不依赖RTTI | 1.BTIR_PASS_PLUGIN("MyPass", MyPassClass)→编译通过 2.使用注册的Pass→正常工作 | BTIR_PASS_PLUGIN→编译通过; 使用Pass→正常 | PluginManager.h |
| F-07 | IRStatistics 统计特征提取 | Phase E | IRModule | IRStatistics.h, 对应.cpp | 1.统计为只读操作 2.O(n)时间复杂度 3.确定性结果 | 1.compute(含2个函数的IRModule)→NumFunctions==2 2.compute(空IRModule)→全0 | compute(2函数)→NumFunctions==2 | IRModule.h |
| F-08 | AIAutoTuner 接口定义 + 桩实现 | F-07 | IRStatistics, AIAutoTuner | AIAutoTuner.h, StubAIAutoTuner.cpp | 1.接口纯虚 2.桩实现返回默认Pass序列 3.不依赖ML模型 | 1.recommendPassSequence(Stats)→返回默认序列 2.evaluatePassSequence→返回基线评估 | recommendPassSequence→默认序列; evaluatePassSequence→基线 | IRStatistics.h |
| F-09 | RemoteCache 远程缓存接口 | E-F1 | CacheStorage, HTTPS | RemoteCache.h, 对应.cpp | 1.HTTPS传输 2.缓存条目签名验证 3.超时和重试 | 1.lookup(有效Key)→命中 2.store→成功 3.签名验证→通过 | lookup→命中; store→成功; 签名→通过 | CacheStorage.h |
| F-10 | FFI Rust语言映射 | C-F4 | FFITypeMapper, Rust ABI | FFITypeMapper.cpp(Rust部分) | 1.Rust→C ABI桥接 2.Repr(C)结构体零开销 3.Repr(Rust)结构体不支持 | 1.mapExternalType("Rust", "i32")→IRIntegerType(32) 2.Repr(C)结构体→IRStructType | mapExternalType("Rust","i32")→IRIntegerType(32) | FFITypeMapper.h |
| F-11 | FFI WASM调用约定 | C-F4 | CallingConvention::WASM | CallingConvention.cpp(WASM部分) | 1.WASM ABI映射 2.值类型:i32/i64/f32/f64 3.引用类型:externref | 1.mapExternalType("WASM", "i32")→IRIntegerType(32) 2.CallingConvention::WASM→WASM调用 | mapExternalType("WASM","i32")→IRIntegerType(32) | FFITypeMapper.h, CallingConvention |
| F-12 | IRFuzzer delta debugging 最小化 | D-F8 | IRFuzzer | IRFuzzer.cpp(delta debugging) | 1.最小化保持触发条件 2.最小化结果≤原始50% 3.最小化时间≤5分钟 | 1.minimizeTrigger(100行触发IR)→≤50行 2.最小化后仍触发相同bug | minimizeTrigger(100行)→≤50行; 仍触发bug | IRFuzzer.h |

**Phase F 工作量**：约 30 人日

---

## Phase G：增量编译期

**目标**：实现红绿标记增量编译算法、查询缓存持久化、投影查询函数级增量重编译、构建系统集成、等价性验证增强。

### 红线对照

| 红线 | 合规性 | 说明 |
|------|--------|------|
| R1 架构优先 | ✓ | 增量编译基于查询式架构，架构设计先行 |
| R2 自由组合 | ✓ | 增量编译对前端/后端透明，不影响自由组合 |
| R3 渐进式 | ✓ | Phase G 依赖 Phase F，可独立验证 |
| R4 功能不退化 | ✓ | 增量编译为可选功能，全量编译路径保留 |
| R5 接口抽象 | ✓ | 查询系统通过 QueryContext 抽象接口交互 |
| R6 IR中间层解耦 | ✓ | 增量编译强化IR作为前后端唯一交互媒介 |

### Task 分解

| 任务ID | 任务描述 | 依赖 | 输入 | 输出 | 实现约束 | 验收标准 | 测试用例 | 依赖文件 |
|--------|---------|------|------|------|---------|---------|---------|---------|
| G-01 | 红绿标记算法实现 | F-02 | QueryContext, DependencyGraph | RedGreenMarker.h, 对应.cpp | 1.标记算法参考Rustc 2.绿色=有效，红色=需重算 3.假阳性优化(重新计算对比指纹) | 1.tryMarkGreen(未修改依赖)→true 2.tryMarkGreen(已修改依赖)→false 3.假阳性正确处理 | tryMarkGreen(未修改)→true; tryMarkGreen(已修改)→false | QueryContext.h, DependencyGraph.h |
| G-02 | 查询缓存持久化到磁盘 | F-01 | QueryContext, 文件系统 | QueryCacheSerializer.h, 对应.cpp | 1.序列化格式与IRFormatVersion兼容 2.加载时版本检查 3.损坏文件→优雅降级 | 1.saveToDisk→文件存在 2.loadFromDisk→缓存恢复 3.损坏文件→返回空缓存 | saveToDisk→文件存在; loadFromDisk→缓存恢复 | QueryContext.h, IRSerializer.h |
| G-03 | StableDefPath 稳定标识符映射 | F-01 | AST Decl, 稳定哈希 | StableDefPath.h, StableIdMap.h, 对应.cpp | 1.稳定哈希128位 2.跨会话ID映射 3.路径格式如"std::vector::push_back" | 1.StableDefPath("std::vector::push_back")→稳定哈希 2.StableIdMap双向查找正确 | lookupOrCreate→稳定ID; toStable→稳定路径 | ASTContext.h |
| G-04 | 投影查询：函数级增量重编译 | G-01 | QueryContext, IRModule | ProjectionQuery.h, 对应.cpp | 1.函数级粒度 2.仅重编译修改的函数 3.未修改函数直接复用缓存 | 1.修改1个函数→仅重编译1个函数 2.未修改函数→缓存命中 3.最终IRModule完整正确 | 修改1函数→重编译1个; 未修改→缓存命中 | QueryContext.h, IRModule.h |
| G-05 | 增量编译与构建系统集成 | G-01 | 构建系统(Make/Ninja) | IncrementalBuildIntegration.h | 1.输出依赖文件(.d) 2.构建系统调用增量编译API 3.全量构建→增量构建无缝切换 | 1.增量编译→仅编译修改文件 2.全量构建→所有文件编译 3.构建时间减少≥50% | 增量编译→时间减少≥50% | QueryContext.h |
| G-06 | IREquivalenceChecker 符号执行验证 | E-F3 | IREquivalenceChecker, 符号执行引擎 | SymbolicExecutor.h, 对应.cpp | 1.符号执行路径覆盖≥80% 2.超时保护(≤30秒/函数) 3.反例可重现 | 1.等价函数→IsEquivalent==true 2.不等价函数→提供反例 3.超时→返回Unknown | 等价→true; 不等价→反例; 超时→Unknown | IREquivalenceChecker.h |
| G-07 | 编译缓存统计与自动清理 | E-F1 | CompilationCacheManager | CacheStatsCollector.h, CacheCleaner.h | 1.统计命中率/空间占用 2.LRU清理策略 3.清理不影响正在使用的缓存 | 1.getStats()→命中率正确 2.超过MaxSize→自动清理 3.清理后缓存可用 | getStats→命中率正确; 超MaxSize→自动清理 | CompilationCacheManager.h |
| G-08 | 诊断交叉引用链（因果关联） | B-F6 | StructuredDiagnostic, DiagnosticNote | DiagnosticCrossRef.h, 对应.cpp | 1.诊断链深度≤5 2.循环引用检测 3.链可序列化为JSON | 1.关联诊断→链正确 2.循环引用→截断 3.JSON输出→可解析 | 关联诊断→链正确; JSON→可解析 | StructuredDiagnostic.h |

**Phase G 工作量**：约 25 人日

---

## Phase H：AI辅助与极致优化期

**目标**：AI Pass序列自动调优、自动增量编译、安全增强、FFI完整支持、分布式编译缓存、性能回归检测、形式化验证框架。

### 红线对照

| 红线 | 合规性 | 说明 |
|------|--------|------|
| R1 架构优先 | ✓ | AI调优通过抽象接口集成，不侵入核心架构 |
| R2 自由组合 | ✓ | AI调优对前端/后端透明，不影响自由组合 |
| R3 渐进式 | ✓ | Phase H 依赖 Phase G，可独立验证 |
| R4 功能不退化 | ✓ | AI调优仅作为建议，不自动执行，保留默认Pass序列 |
| R5 接口抽象 | ✓ | AIAutoTuner 通过抽象接口交互 |
| R6 IR中间层解耦 | ✓ | AI调优作用于IR Pass序列，不直接接触前端/后端 |

### Task 分解

| 任务ID | 任务描述 | 依赖 | 输入 | 输出 | 实现约束 | 验收标准 | 测试用例 | 依赖文件 |
|--------|---------|------|------|------|---------|---------|---------|---------|
| H-01 | AI Pass序列推荐引擎 | F-08 | AIAutoTuner, IRStatistics | AIPassRecommender.h, 对应.cpp | 1.推荐仅作为建议，不自动执行 2.保留默认Pass序列作为基线 3.推荐结果可审查 | 1.recommendPassSequence→返回建议序列 2.建议序列性能≥默认序列90% 3.最差情况回退到默认序列 | recommend→建议序列; 性能≥默认90% | AIAutoTuner.h, IRStatistics.h |
| H-02 | RL训练数据收集管线 | F-08 | IRStatistics, PassSequenceEvaluation | RLDataCollector.h, 对应.cpp | 1.数据格式: (Stats, Sequence, Eval)三元组 2.数据持久化到磁盘 3.数据量限制(≤1GB/会话) | 1.recordFeedback→数据记录 2.数据可导出为训练格式 3.数据量超限→自动清理 | recordFeedback→数据记录; 导出→训练格式 | AIAutoTuner.h, IRStatistics.h |
| H-03 | AI调优模型集成接口 | H-01 | AIAutoTuner, ML模型 | AIModelIntegration.h, 对应.cpp | 1.模型接口抽象(不依赖具体ML框架) 2.模型加载可选 3.模型不可用→回退到规则引擎 | 1.加载模型→推荐质量提升 2.模型不可用→回退到规则引擎 3.加载失败→不影响编译 | 加载模型→推荐提升; 加载失败→回退 | AIAutoTuner.h |
| H-04 | 自动增量：文件监控+智能重编译 | G-04 | 文件系统监控, QueryContext | FileWatcher.h, SmartRecompiler.h | 1.inotify/FSEvents监控 2.仅重编译受影响文件 3.去抖动(100ms内多次修改合并) | 1.文件修改→自动重编译 2.仅修改的文件重编译 3.100ms内多次修改→合并为1次 | 文件修改→自动重编译; 去抖动→合并 | QueryContext.h, ProjectionQuery.h |
| H-05 | IR签名与验证（安全增强） | E-F6 | IRModule, 密钥对 | IRSigner.h, 对应.cpp | 1.签名算法: Ed25519 2.验证在加载时执行 3.签名失败→拒绝加载 | 1.sign→签名正确 2.verify→验证通过 3.篡改IR→验证失败 | sign→verify通过; 篡改→verify失败 | IRModule.h, IRSerializer.h |
| H-06 | FFI Python绑定自动生成 | F-10 | FFITypeMapper, Python ctypes | FFIPythonGenerator.h, 对应.cpp | 1.生成ctypes/cffi绑定 2.类型映射: IR→Python 3.生成代码可执行 | 1.generateBindings("Python")→Python代码 2.生成的绑定可调用C函数 3.类型映射正确 | generateBindings→Python代码; 调用→正确 | FFITypeMapper.h |
| H-07 | FFI WASM Component Model | F-11 | CallingConvention::WASM, WASM Component | FFIWasmComponent.h, 对应.cpp | 1.WASM Component Model规范 2.接口类型: wasm->func 3.跨语言WASM互操作 | 1.生成WASM Component接口 2.跨语言调用正确 3.类型映射符合规范 | 生成Component→正确; 跨语言调用→正确 | FFITypeMapper.h, CallingConvention |
| H-08 | 分布式编译缓存 | F-09 | RemoteCache, 分布式协议 | DistributedCache.h, 对应.cpp | 1.一致性哈希分片 2.缓存条目签名验证 3.节点故障→优雅降级 | 1.多节点缓存→命中率提升 2.节点故障→降级到本地缓存 3.签名验证→防投毒 | 多节点→命中率提升; 故障→降级 | RemoteCache.h |
| H-09 | 编译器性能回归自动检测 | F-07 | IRStatistics, 性能基准 | PerfRegressionDetector.h, 对应.cpp | 1.基准数据持久化 2.回归阈值: ≥10%性能下降 3.自动报告回归 | 1.性能下降≥10%→报告 2.性能提升→记录 3.历史趋势可查看 | 下降≥10%→报告; 提升→记录 | IRStatistics.h |
| H-10 | 完整形式化验证框架 | G-06 | IREquivalenceChecker, 符号执行 | FormalVerifier.h, 对应.cpp | 1.验证IR Pass语义保持 2.验证编译器端到端正确性 3.验证超时→报告未验证 | 1.Pass前后语义保持→通过 2.端到端验证→通过 3.超时→标记为未验证 | Pass语义保持→通过; 端到端→通过 | IREquivalenceChecker.h, SymbolicExecutor.h |

**Phase H 工作量**：约 40 人日

---

## 各Phase工作量汇总

> 来源：09-行业前沿补充优化方案 §3.7

| Phase | 现有工作量 | 新增工作量 | 总计 |
|-------|-----------|-----------|------|
| Phase A | 约20人日 | +5.5人日 | 约25.5人日 |
| Phase B | 约30人日 | +18人日 | 约48人日 |
| Phase C | 约25人日 | +11.5人日 | 约36.5人日 |
| Phase D | 约30人日 | +15.5人日 | 约45.5人日 |
| Phase E | 约20人日 | +15人日 | 约35人日 |
| Phase F | — | — | 约30人日 |
| Phase G | — | — | 约25人日 |
| Phase H | — | — | 约40人日 |
| **总计** | **约125人日** | **+65.5人日** | **约285.5人日** |

---

> **文档版本**：v1.0
> **创建日期**：2026-04-24
> **来源**：09-行业前沿补充优化方案 §3.6-3.7
> **关联文档**：05-渐进式改造方案-PhaseAB.md、06-渐进式改造方案-PhaseCDE.md、09-行业前沿补充优化方案.md
