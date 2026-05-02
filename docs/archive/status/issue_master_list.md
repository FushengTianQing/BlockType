# BlockType 问题总清单

**生成日期**: 2026-04-23  
**数据来源**: Wave1-Wave7 审查报告 + 代码验证  
**说明**: 由于 git stash 覆盖事故，部分已修复的问题在当前代码中丢失，需要重新修复。

---

## 一、修复状态总览

| 状态 | 数量 |
|------|------|
| ✅ 已修复（Wave1-7确认） | 25 |
| ✅ Wave8 修复 | 4 |
| ✅ Wave9 修复 | 3 |
| ✅ Wave10 修复 | 2 |
| ✅ Wave11 修复 | 2 |
| 📝 仅TODO注释（架构限制） | 1 |
| **总计** | **37** |

---

## 二、Wave8 已修复（4个）

| 编号 | 问题 | 文件 | 修复内容 |
|------|------|------|----------|
| LOST-2 (P0) | TypedefNameDecl→TemplateTypeParmDecl | Sema.cpp:770 | `dyn_cast_or_null` 改为 `TemplateTypeParmDecl` |
| W7-P2-1 | EvaluateExprRequirement 未应用 SubstType | ConstraintSatisfaction.cpp:185 | 添加 `E->setType(SubstType)` |
| W7-P2-2 | InstantiateClassTemplate 未注册 Methods | Sema.cpp:156-165 | 添加方法注册循环 |
| W1-P2 | isCompleteType 重复分支 | Sema.cpp:347-372 | 删除简单版本，保留详细版本 |

## 三、尚未修复的问题（6个）

---

## 三、尚未修复的新发现问题（9个）

### P2 级别（全部）

| 编号 | 来源 | 文件 | 描述 | 工作量 |
|------|------|------|------|--------|
| LOST-1 | Wave2 | `CodeGenModule.cpp` | `forEachAttribute` 统一迭代器从未实现，`QueryAttributes`/`HasAttribute`/`GetAttributeArgument` 大量重复代码 | ✅ Wave11（统一迭代器） |
| W6-P2-1 | Wave6 | `ExceptionAnalysis.cpp:137` | `CheckCatchMatch` 中 `isBuiltinType()` 条件过宽，`catch(int)` 会被错误匹配为 catch-all | ✅ Wave9 |
| W6-P2-2 | Wave6 | `Overload.cpp:245-258` | `addCandidates` 缺少模板参数推导步骤，模板候选参数类型仍含依赖类型 | ✅ Wave10（TODO注释，架构限制） |
| W6-P2-3 | Wave6 | `TemplateInstantiator.cpp:159` | 方法体克隆未使用 StmtCloner，`Method->getBody()` 直接共享 | ✅ Wave10（StmtCloner深拷贝） |
| W6-P2-5 | Wave6 | `TemplateInstantiator.cpp:153` | CXXMethodDecl 构造参数 Access 默认值 AS_private，需验证 getAccess 返回值 | ✅ Wave9（已验证正常） |
| W3-P1-1→P2 | Wave3 | `TypeDeduction.cpp:91-92` | `deduceAutoRefType` 将 CV 放在引用类型上（引用不可被 CV 限定） | ✅ Wave9 |
| W3-P1-2→P2 | Wave3 | `TypeDeduction.cpp:415` | structured binding 不支持 tuple-like 类型 | ✅ Wave11（tuple/pair基本支持） |

---

## 四、所有已完成修复验证清单（25个）

| # | 修复项 | 来源 | 验证状态 |
|---|--------|------|----------|
| 1 | Contracts 零调用修复 | Wave1 | ✅ |
| 2 | substituteFunctionSignature 完整实现 | Wave1 | ✅ |
| 3 | EvaluateExprRequirement return false bug | Wave1→Wave4 | ✅ |
| 4 | TypeDeduction 空壳入口修复 | Wave1 | ✅ |
| 5 | EmitAssignment 左值处理修复 | Wave1 | ✅ |
| 6 | AddGlobalCtor(nullptr) hack 修复 | Wave1 | ✅ |
| 7 | Sema.cpp 拆分 | Wave2 | ✅ |
| 8 | 重载解析排名增强 | Wave2 | ✅ |
| 9 | SFINAE 保护 | Wave2 | ✅ |
| 10 | deduceDecltypeType 区分 DeclRefExpr | Wave3→Wave4 | ✅ |
| 11 | EvaluateExprRequirement 使用 ConstantExprEvaluator | Wave4 | ✅ |
| 12 | Mangler 嵌套名修饰（VTable/RTTI/Typeinfo） | Wave4 | ✅ |
| 13 | HTTPClient urlEncode UB 修复 | Wave4 | ✅ |
| 14 | EvaluateExprRequirement 子表达式替换（substituteDependentExpr） | Wave5 | ✅ |
| 15 | mangleNestedName 递归处理 parent 链 | Wave5 | ✅ |
| 16 | 方法名修饰命名空间前缀 | Wave5 | ✅ |
| 17 | TypedefNameDecl→TemplateTypeParmDecl（TemplateInstantiation.cpp） | Wave6 | ✅ |
| 18 | TypedefNameDecl→TemplateTypeParmDecl（ConstraintSatisfaction.cpp） | Wave6 | ✅ |
| 19 | CheckCatchMatch catch(...) 添加 return | Wave6 | ✅ |
| 20 | Lambda 静态变量 err→warn | Wave6 | ✅ |
| 21 | 整型提升 First 步骤修正为 Identity | Wave6 | ✅ |
| 22 | addCandidates 处理 FunctionTemplateDecl | Wave6 | ✅ |
| 23 | 泛型 lambda 推导使用 DeducedArgs | Wave6 | ✅ |
| 24 | InstantiateClassTemplate 完整实现（TemplateInstantiator） | Wave6 | ✅ |
| 25 | InstantiateClassTemplate 委托给 TemplateInstantiator（Sema） | Wave7 | ✅ |
| 26 | TypedefNameDecl→TemplateTypeParmDecl（Sema.cpp） | Wave8 | ✅ |
| 27 | EvaluateExprRequirement setType（SubstType） | Wave8 | ✅ |
| 28 | InstantiateClassTemplate 注册 Methods | Wave8 | ✅ |
| 29 | isCompleteType 重复分支删除 | Wave8 | ✅ |
| 30 | CheckCatchMatch catch(...) 条件修正 | Wave9 | ✅ |
| 31 | deduceAutoRefType CV 限定符位置修正 | Wave9 | ✅ |
| 32 | CXXMethodDecl Access 验证确认 | Wave9 | ✅ |
| 33 | 方法体克隆使用 StmtCloner 深拷贝 | Wave10 | ✅ |
| 34 | addCandidates 模板推导架构限制 TODO | Wave10 | ✅ |
| 35 | forEachAttribute 统一迭代器 + 属性查询去重 | Wave11 | ✅ |
| 36 | structured binding tuple-like 基本支持 | Wave11 | ✅ |

---

## 五、已间接修复的问题

| 编号 | 来源 | 原描述 | 状态 |
|------|------|--------|------|
| W6-P2-4 | Wave6 | TemplateInstantiator 未注册符号表 | ✅ 间接修复（Sema 委托后负责注册） |

---

## 六、修复优先级建议

### 立即修复（影响核心功能）

1. **LOST-2**: `Sema.cpp:770` TypedefNameDecl→TemplateTypeParmDecl — 0.1天
2. **W7-P2-1**: EvaluateExprRequirement 替换结果应用 — 0.1天
3. **W7-P2-2**: InstantiateClassTemplate 方法注册 — 0.1天

### 短期修复（正确性改进）

4. **LOST-1**: CodeGen 属性系统去重 — 3天
5. **W6-P2-3**: 方法体使用 StmtCloner 克隆 — 1天
6. **W6-P2-1**: CheckCatchMatch catch(...) 判断条件 — 0.5天
7. **W3-P1-1→P2**: deduceAutoRefType CV 位置 — 0.5天

### 中期修复（功能完善）

8. **W6-P2-2**: addCandidates 模板参数推导 — 3天
9. **W3-P1-2→P2**: structured binding tuple-like 支持 — 3天
10. **W6-P2-5**: CXXMethodDecl Access 验证 — 0.5天
11. **W1-P2**: isCompleteType 重复分支 — 0.1天

---

## 七、历史审计报告索引

| 文件 | 内容 |
|------|------|
| `docs/status/audits/wave1_wave2_ai_module_audit.md` | Wave1+2 修复审查 + AI/Module 模块审查 |
| `docs/status/audits/wave3_feature_audit.md` | Wave3 功能补全审查（1 P0 + 8 P1 + 10 P2） |
| `docs/status/audits/wave4_audit.md` | Wave4 修复审查（3 P1 + 5 P2） |
| `docs/status/audits/wave6_audit.md` | Wave6 修复审查（2 P1 + 5 P2 新发现） |
| `docs/status/audits/wave7_audit.md` | Wave7 修复审查（2 P2 新发现，首次全绿） |
