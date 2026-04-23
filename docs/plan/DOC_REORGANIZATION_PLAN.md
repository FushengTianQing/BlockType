# BlockType 文档治理与重组方案

> **规划人**: planner（规划人员）
> **规划日期**: 2026-04-23
> **文档版本**: v1.0
> **状态**: 待执行

---

## 一、问题诊断

### 1.1 当前文档目录结构

```
docs/
├── (根目录散落) 11 个文件，无统一分类
├── archive/      5 个文件 — 归档区，命名混乱（中英混用）
├── audit/        4 个文件 — 审计报告，与 dev status/ 审计报告重复
├── design/       7 个文件 — 设计文档，子目录命名含中文
├── dev status/  39 个文件 — 开发状态+审计+修复方案混杂，目录名含空格
├── examples/     1 个文件 — 示例代码
├── implementation/ 3 个文件 — 实现文档
├── plan/        17 个文件 — 规划文档，有子目录但编号不统一
├── project_review/ 35 个文件 — 项目审查，含空目录 analysis/
└── review_output/  14 个文件 — Lexer 改进报告，1 个空文件
```

**总计**: 135 个 .md 文件 + 3 个 .csv + 1 个 .cpp = **139 个文件**

### 1.2 核心问题

| # | 问题 | 严重度 | 影响 |
|---|------|--------|------|
| 1 | **根目录散落文件**：11 个文件无分类，包含路线图、架构、特性列表等不同性质文档 | 高 | 新人无法快速找到关键文档 |
| 2 | **dev status/ 与 audit/ 职责重叠**：审计报告同时存在于两个目录 | 高 | 信息重复、查找混乱 |
| 3 | **目录名含空格和中文**：`dev status/`、`AST功能重构/`、`原生中英双语方案/` | 中 | 命令行操作需转义，不利于自动化 |
| 4 | **过期文档未标注**：ROADMAP.md 标注当前阶段为 Phase 0，实际已到 Phase 7 | 高 | 误导性极强 |
| 5 | **内容矛盾**：多个文档间存在数据不一致（详见 1.3 节） | 高 | 开发者无法确定以哪个为准 |
| 6 | **空文件**：`review_output/phase1_task1.0_lexer_analysis.md` 为 0 字节 | 中 | 占位但无内容 |
| 7 | **空目录**：`project_review/analysis/` 为空 | 低 | 无实际影响 |
| 8 | **review_output/ 定位不清**：14 个 Lexer 改进报告，属于阶段性产物 | 中 | 已完成的改进报告应归档 |
| 9 | **project_review/ 体量过大**：35 个文件，审查报告与进度看板混杂 | 中 | 难以快速定位 |

### 1.3 文档间矛盾清单

| # | 矛盾点 | 文档 A | 文档 B | 正确值 |
|---|--------|--------|--------|--------|
| 1 | 当前阶段 | ROADMAP.md: Phase 0 | PROJECT_PLAN_MASTER.md: Phase 7 | Phase 7 |
| 2 | 开发周期 | 00-MASTER-PLAN.md: 44-56 周 | ROADMAP.md: 63 周 | PROJECT_PLAN_MASTER.md: 剩余 18-20 周 |
| 3 | Contracts 状态 | CPP23-CPP26-FEATURES.md: 未实现 | Phase 7.3 审计: 部分实现 | 部分实现（P0 零调用问题） |
| 4 | 进度看板 | PHASE_PROGRESS.md: Phase 2 | 实际: Phase 7 | Phase 7 |

---

## 二、全量文档盘点与标注

### 2.1 根目录散落文件（11 个）

| 文件 | 类型 | 状态 | 准确性 | 处置 |
|------|------|------|--------|------|
| ROADMAP.md | 路线图 | 过期 | 低 | 重写后移入 plan/ |
| ARCHITECTURE.md | 架构 | 需更新 | 中 | 移入 architecture/ 并更新 |
| CPP23-CPP26-FEATURES.md | 特性审计 | 有效 | 高 | 移入 features/ |
| CPP20_Modules_Development_Plan.md | 模块规划 | 有效 | 中 | 移入 plan/ |
| AI_API.md | API 文档 | 需更新 | 中 | 移入 guides/ 并更新 |
| AI_USAGE.md | 使用指南 | 有效 | 中 | 移入 guides/ |
| ACCESS_CONTROL_IMPLEMENTATION.md | 实现报告 | 需更新 | 中 | 移入 guides/ |
| TEST_BASELINE.md | 测试基线 | 有效 | 高 | 移入 guides/ |
| ideal feature.md | 产品愿景 | 有效 | 中 | 移入 guides/ 并重命名 |
| review_flowchart.md | 流程图 | 有效 | 中 | 移入 architecture/ |
| 开发-前中后-三阶段-核查要求.md | 开发规范 | 有效 | 高 | 移入 guides/ 并重命名 |

### 2.2 plan/ 目录（17 个文件）

| 文件 | 类型 | 状态 | 准确性 | 处置 |
|------|------|------|--------|------|
| 00-MASTER-PLAN.md | 总规划 v1 | 需更新 | 低 | 归档至 archive/ |
| PROJECT_PLAN_MASTER.md | 总规划 v2 | 有效 | 高 | 保留为权威文档 |
| 00~09-PHASE*.md | Phase 计划 | 有效 | 高 | 保留 |
| P1061R10-*.md | 修复方案 | 有效 | 高 | 保留 |

### 2.3 dev status/ 目录（39 个文件）

全部迁移至 `status/` 对应 phase 子目录。审计报告与 `audit/` 合并至 `status/audits/`。

### 2.4 audit/ 目录（4 个文件）

与 `dev status/` 审计报告合并，统一迁入 `status/audits/`。

### 2.5 design/ 目录（7 个文件）

保留，重命名含中文的子目录和文件名。

### 2.6 archive/ 目录（5 个文件）

保留，新增归档项。

### 2.7 implementation/ 目录（3 个文件）

迁出至 `features/` 和 `archive/`，删除空目录。

### 2.8 project_review/ 目录（35 个文件）

全部迁入 `review/`，删除空目录 `analysis/`。

### 2.9 review_output/ 目录（14 个文件）

删除空文件，其余归档至 `archive/lexer-reviews/`。

---

## 三、新分类体系设计

### 3.1 设计原则

1. **按文档性质分类**：规划、架构、设计、状态、特性、指南、审查、归档
2. **消除歧义**：每个目录有明确的职责边界，不重叠
3. **英文命名**：目录名全部使用英文小写+连字符，避免空格和中文
4. **扁平优先**：避免过深嵌套，最多 2 层
5. **可扩展**：新文档有明确的归属位置

### 3.2 新目录结构

```
docs/
├── plan/                    # 规划文档 — 项目和各 Phase 的开发计划
│   ├── PROJECT_PLAN_MASTER.md   # 权威总规划
│   ├── ROADMAP.md               # 开发路线图（重写）
│   ├── CPP20_Modules_Development_Plan.md
│   ├── 00-PHASE0-foundation.md ~ 09-PHASE9-integration-release.md
│   └── P1061R10-and-template-factory-fix.md
│
├── architecture/            # 架构文档 — 编译器整体架构和模块设计
│   ├── ARCHITECTURE.md          # 编译器架构（更新）
│   └── review_flowchart.md      # 编译流程图
│
├── design/                  # 设计文档 — 具体功能的设计方案
│   ├── keyword-mapping.md
│   ├── sizeof-alignof.md
│   ├── init-list-type-deduction.md
│   ├── bilingual/               # 中英双语方案
│   │   ├── BILINGUAL_ARCHITECTURE.md
│   │   ├── LOCALIZATION.md
│   │   └── DIAGNOSTICS_I18N.md
│   └── unicode/                 # Unicode 支持方案
│       ├── UNICODE_SUPPORT_DESIGN.md
│       ├── UNICODE_SUPPORT.md
│       ├── UNICODE_UPDATE_STRATEGY.md
│       └── UTF8PROC_ANALYSIS.md
│
├── features/                # 特性文档 — C++ 特性支持状态和实现
│   ├── CPP23-CPP26-FEATURES.md
│   ├── P0963R3-if-structured-binding.md
│   └── template-factory-final-status.md
│
├── status/                  # 状态文档 — 各 Phase 开发状态和审计报告
│   ├── phase0/ ~ phase7/        # 按 Phase 分目录
│   ├── ast-refactor/            # AST 重构方案
│   └── audits/                  # 跨模块审计
│
├── guides/                  # 指南文档 — 使用指南和开发规范
│   ├── AI_API.md
│   ├── AI_USAGE.md
│   ├── TEST_BASELINE.md
│   ├── ACCESS_CONTROL_IMPLEMENTATION.md
│   ├── dev-checklist.md
│   └── ideal-features.md
│
├── review/                  # 审查文档 — 项目审查相关
│   ├── PROJECT_REVIEW_PLAN.md
│   ├── PHASE_PROGRESS.md
│   ├── PHASE_COMPLETION_CHECKLIST.md
│   ├── data/                    # CSV 数据
│   └── reports/                 # 审查报告
│
├── archive/                 # 归档文档 — 已完成/过期的历史文档
│   ├── 00-MASTER-PLAN.md
│   ├── lexer-reviews/           # Lexer 改进报告归档
│   └── ... (其他归档文件)
│
└── examples/                # 示例代码
    └── parser_fixit_examples.cpp
```

### 3.3 目录职责定义

| 目录 | 职责 | 准入标准 |
|------|------|---------|
| `plan/` | 项目和各 Phase 的开发计划 | 规划性质，描述"做什么" |
| `architecture/` | 编译器整体架构和模块设计 | 架构性质，描述"怎么组织" |
| `design/` | 具体功能的设计方案 | 设计性质，描述"怎么实现某个功能" |
| `features/` | C++ 特性支持状态和实现文档 | 特性追踪和实现方案 |
| `status/` | 各 Phase 开发状态和审计报告 | 状态追踪，描述"做到哪了" |
| `guides/` | 使用指南和开发规范 | 指导性质，描述"怎么用" |
| `review/` | 项目审查相关文档 | 审查过程记录 |
| `archive/` | 已完成/过期的历史文档 | 不再活跃但需保留 |
| `examples/` | 示例代码 | 可运行的代码示例 |

---

## 四、重组操作清单

### 4.1 删除操作

| 操作 | 原因 |
|------|------|
| 删除 `review_output/phase1_task1.0_lexer_analysis.md` | 空文件（0 字节） |
| 删除 `project_review/analysis/` | 空目录 |

### 4.2 需要重写的文档

| 文档 | 当前问题 | 重写要求 |
|------|---------|---------|
| `ROADMAP.md` | 标注当前阶段为 Phase 0，实际 Phase 7 | 基于 PROJECT_PLAN_MASTER.md 重写 |
| `PHASE_PROGRESS.md` | 标注 Phase 2，实际 Phase 7 | 更新为当前实际进度 |
| `project_review/README.md` | 审查进度 6.2% 已过时 | 更新为当前实际审查状态 |

### 4.3 需要更新的文档

| 文档 | 更新内容 |
|------|---------|
| `ARCHITECTURE.md` | 补充 AST 重构、Sema 激活等最新架构变更 |
| `AI_API.md` | 补充深度集成 API |
| `PHASE0.6_STATUS.md` | 更新完成度为 100% |
| `07-PHASE7-cpp26-features.md` | 更新 Phase 7 最新进展 |
| `08-PHASE8-target-platform.md` | 完善待定内容 |
| `09-PHASE9-integration-release.md` | 完善待定内容 |

### 4.4 文件移动操作

#### 移入 plan/

| 源 | 目标 |
|----|------|
| `docs/CPP20_Modules_Development_Plan.md` | `docs/plan/CPP20_Modules_Development_Plan.md` |

#### 移入 architecture/（新建目录）

| 源 | 目标 |
|----|------|
| `docs/ARCHITECTURE.md` | `docs/architecture/ARCHITECTURE.md` |
| `docs/review_flowchart.md` | `docs/architecture/review_flowchart.md` |

#### 移入 features/（新建目录）

| 源 | 目标 |
|----|------|
| `docs/CPP23-CPP26-FEATURES.md` | `docs/features/CPP23-CPP26-FEATURES.md` |
| `docs/implementation/P0963R3-if-structured-binding.md` | `docs/features/P0963R3-if-structured-binding.md` |
| `docs/implementation/template-factory-final-status.md` | `docs/features/template-factory-final-status.md` |

#### 移入 guides/（新建目录）

| 源 | 目标 |
|----|------|
| `docs/AI_API.md` | `docs/guides/AI_API.md` |
| `docs/AI_USAGE.md` | `docs/guides/AI_USAGE.md` |
| `docs/TEST_BASELINE.md` | `docs/guides/TEST_BASELINE.md` |
| `docs/ACCESS_CONTROL_IMPLEMENTATION.md` | `docs/guides/ACCESS_CONTROL_IMPLEMENTATION.md` |
| `docs/ideal feature.md` | `docs/guides/ideal-features.md` |
| `docs/开发-前中后-三阶段-核查要求.md` | `docs/guides/dev-checklist.md` |

#### dev status/ → status/（新建目录及子目录）

将 `docs/dev status/` 下 39 个文件按 Phase 编号迁入 `docs/status/phase0/` ~ `docs/status/phase7/`，AST 重构文档迁入 `docs/status/ast-refactor/`，其他非 Phase 文件按归属放入对应 phase 子目录。

#### audit/ → status/audits/

| 源 | 目标 |
|----|------|
| `docs/audit/P0_sema_layer_audit.md` | `docs/status/audits/P0_sema_layer_audit.md` |
| `docs/audit/P1_codegen_module_audit.md` | `docs/status/audits/P1_codegen_module_audit.md` |
| `docs/audit/P1_parse_module_audit.md` | `docs/status/audits/P1_parse_module_audit.md` |
| `docs/audit/P1_sema_module_audit.md` | `docs/status/audits/P1_sema_module_audit.md` |

#### project_review/ → review/

将 `docs/project_review/` 全部内容迁入 `docs/review/`，保持子目录结构（data/、reports/）。

#### review_output/ → archive/lexer-reviews/

将 `docs/review_output/` 中 13 个非空文件迁入 `docs/archive/lexer-reviews/`。

#### 归档

| 源 | 目标 |
|----|------|
| `docs/plan/00-MASTER-PLAN.md` | `docs/archive/00-MASTER-PLAN.md` |
| `docs/implementation/template-factory-progress.md` | `docs/archive/template-factory-progress.md` |

#### design/ 目录重命名

| 源 | 目标 |
|----|------|
| `docs/design/原生中英双语方案/` | `docs/design/bilingual/` |
| `docs/design/Unicode字符集支持/` | `docs/design/unicode/` |
| `docs/design/初始化列表类型推导+双层架构.md` | `docs/design/init-list-type-deduction.md` |
| `docs/design/KEYWORD_MAPPING.md` | `docs/design/keyword-mapping.md` |

### 4.5 迁移完成后删除的空目录

- `docs/dev status/`
- `docs/audit/`
- `docs/implementation/`
- `docs/project_review/`
- `docs/review_output/`

---

## 五、执行优先级与风险

### 5.1 执行优先级

| 优先级 | 操作 | 风险 | 预计耗时 |
|--------|------|------|---------|
| P0 | 重写 ROADMAP.md（当前严重误导） | 低 | 30 分钟 |
| P0 | 更新 PHASE_PROGRESS.md | 低 | 15 分钟 |
| P1 | 创建新目录结构并移动文件 | 中 — 需更新内部链接 | 2 小时 |
| P1 | 重命名含中文/空格的目录和文件 | 中 — 需更新引用 | 30 分钟 |
| P2 | 更新 ARCHITECTURE.md | 低 | 1 小时 |
| P2 | 更新 AI_API.md | 低 | 30 分钟 |
| P2 | 完善 Phase 8/9 计划 | 低 | 2 小时 |
| P3 | 删除空文件和空目录 | 极低 | 5 分钟 |

### 5.2 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 内部链接断裂 | 文档间交叉引用失效 | 移动后全局搜索替换链接 |
| Git 历史断裂 | 移动文件丢失历史 | 使用 `git mv` 保留历史 |
| 开发者习惯打破 | 短期找不到文档 | 在旧位置放置 README 指向新位置（过渡期） |
| 一次性改动过大 | 难以 review | 可分批执行，先 P0 再 P1 再 P2 |

---

## 六、新文档命名规范

### 6.1 文件命名

- **英文小写 + 连字符**：`dev-checklist.md`、`init-list-type-deduction.md`
- **已有约定除外**：Phase 计划文件保持现有编号（`00-PHASE0-foundation.md`）
- **中文文件名**：全部转为英文或拼音

### 6.2 目录命名

- **英文小写 + 连字符**：`ast-refactor/`、`bilingual/`
- **禁止空格**：`dev status/` → `status/`
- **禁止中文**：`AST功能重构/` → `ast-refactor/`

### 6.3 新文档放置规则

| 文档性质 | 放置位置 | 示例 |
|---------|---------|------|
| 新 Phase 计划 | `plan/` | `plan/10-PHASE10-xxx.md` |
| 新架构设计 | `architecture/` | `architecture/module-system.md` |
| 新功能设计 | `design/` | `design/coroutine-design.md` |
| 新特性追踪 | `features/` | `features/P2996-reflection.md` |
| 新阶段状态 | `status/phaseN/` | `status/phase7/PHASE7-STATUS.md` |
| 新审计报告 | `status/audits/` | `status/audits/P1_xxx_audit.md` |
| 新使用指南 | `guides/` | `guides/build-guide.md` |
| 新审查报告 | `review/reports/` | `review/reports/task_3.1_report.md` |

---

## 七、预期成果

### 7.1 重组前后对比

| 指标 | 重组前 | 重组后 |
|------|--------|--------|
| 根目录散落文件 | 11 个 | 0 个 |
| 含中文/空格的目录名 | 3 个 | 0 个 |
| 职责重叠的目录 | 2 对（dev status/audit, review_output/archive） | 0 对 |
| 过期未标注的文档 | 2 个（ROADMAP, PHASE_PROGRESS） | 0 个 |
| 空文件 | 1 个 | 0 个 |
| 空目录 | 1 个 | 0 个 |
| 文档分类体系 | 无 | 9 类，职责清晰 |

### 7.2 查找效率提升

| 场景 | 重组前 | 重组后 |
|------|--------|--------|
| 找项目总规划 | 不知在 plan/ 还是根目录 | `plan/PROJECT_PLAN_MASTER.md` |
| 找当前开发状态 | 在 dev status/ 39 个文件中翻找 | `status/phase7/` |
| 找架构文档 | 在根目录 11 个文件中翻找 | `architecture/` |
| 找特性支持状态 | 在根目录翻找 | `features/CPP23-CPP26-FEATURES.md` |
| 找使用指南 | 在根目录翻找 | `guides/` |

---

*本方案为 BlockType 文档治理的权威指导文档，由 planner 规划人员于 2026-04-23 编写。*
