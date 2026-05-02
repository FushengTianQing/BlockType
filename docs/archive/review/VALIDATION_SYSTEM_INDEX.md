# 防错机制文件索引（v3.0）

**版本**: v3.0 - 7-Phase完整方案  
**最后更新**: 2026-04-20 00:00  
**目的**: 清晰分类所有防错机制相关文件，避免混淆  
**计划版本**: PROJECT_REVIEW_PLAN_V2.md v3.0（7个Phase，28个Task，22个功能域）

---

## 📁 文件组织结构

```
BlockType/
├── docs/review/              # 文档目录
│   ├── VALIDATION_SYSTEM_INDEX.md    # ← 本文件（索引）
│   │
│   ├── 📘 核心文档
│   │   ├── AUTOMATED_VALIDATION_GUIDE.md          # v3.0使用指南（7-Phase方案）
│   │   ├── PLAN_V3_UPDATE_NOTES.md                # v3.0更新说明
│   │   ├── DOCUMENT_UPDATE_SUMMARY.md             # 文档更新总结
│   │   └── ERROR_PREVENTION_MECHANISM.md          # v1.0说明（已废弃，保留作历史参考）
│   │
│   ├── 📋 工作辅助文档
│   │   ├── PHASE_PROGRESS.md                      # 实时进度看板（v3.0）
│   │   └── PHASE_COMPLETION_CHECKLIST.md          # Phase完成检查清单模板（v3.0）
│   │
│   ├── 📊 验证报告（自动生成）
│   │   ├── validation_report_phase2_task2.1.md    # Task 2.1验证报告
│   │   ├── validation_report_phase2_task2.3.md    # Task 2.3验证报告
│   │   └── ...                                    # 其他Task验证报告
│   │
│   └── 📝 Task执行报告
│       ├── review_task_1.4_report.md              # Phase 1流程地图
│       ├── review_task_2.1_report.md              # 22个功能域定义
│       ├── task_2.2.*_report.md                   # 12个子任务报告
│       ├── task_2.3_flowchart_mapping.md          # Task 2.3映射报告
│       ├── flowchart_main.md                      # 主流程图
│       ├── flowchart_parser_detail.md             # Parser详细图
│       └── ...                                    # 其他Task报告
│
└── scripts/                          # 可执行脚本目录
    ├── 🛠️ 核心验证脚本
    │   ├── phase_validator.py                     # 统一验证框架（主入口，v2.0）
    │   └── verify_parser_completeness.py          # Parser函数完整性验证
    │
    ├── 🔄 工作流自动化脚本
    │   ├── run_task.sh                            # 开始Task（自动更新状态）
    │   └── complete_task.py                       # 完成任务（自动记录时间）
    │
    └── 📜 遗留脚本（v1.0）
        └── verify_phase_completion.py             # 简单文件存在性检查（已被phase_validator.py替代）
```

---

## 📘 核心文档说明

### 1. AUTOMATED_VALIDATION_GUIDE.md (v3.0) ✅ **当前有效**

**位置**: `docs/review/AUTOMATED_VALIDATION_GUIDE.md`  
**大小**: ~12 KB  
**作用**: **防错机制v3.0的完整使用指南（7-Phase方案）**

**包含内容**:
- 为什么v1.0失败了
- 标准工作流程（强制执行）
- Phase 1-4的验证规则
- **Phase 5: 工程质量保障审查的验证规则** ← 新增
- **Phase 6: 性能优化与深度技术审查的验证规则** ← 新增
- 如何添加新的验证规则
- 禁止的行为
- 验证报告示例
- 故障排查

**何时使用**: 
- **每次执行Task前，必须阅读此文档**
- 了解如何正确使用自动化验证系统

---

### 2. ERROR_PREVENTION_MECHANISM.md (v1.0) ❌ **已废弃**

**位置**: `docs/review/ERROR_PREVENTION_MECHANISM.md`  
**大小**: 10 KB  
**作用**: **历史记录，记录v1.0失败的原因**

**包含内容**:
- 5层防护机制的理论架构
- 为什么这些机制没有生效
- 失败的根本原因分析

**何时使用**: 
- 仅作为历史参考
- 理解"为什么需要v2.0"
- **不要按照此文档操作**

**状态标记**: 
建议在文件顶部添加：
```markdown
> ⚠️ **已废弃** - 本文档描述的是v1.0防错机制，因依赖自觉性而失败。
> 请使用 [AUTOMATED_VALIDATION_GUIDE.md](AUTOMATED_VALIDATION_GUIDE.md) (v2.0)
```

---

## 📋 工作辅助文档

### 3. PHASE_PROGRESS.md

**位置**: `docs/review/PHASE_PROGRESS.md`  
**大小**: 3.6 KB  
**作用**: **实时进度看板**

**包含内容**:
- 每个Phase、每个Task的状态
- 统计数据和问题汇总
- 更新日志

**何时更新**:
- 开始Task时 → 状态改为 `🔄 IN_PROGRESS`
- 完成Task时 → 状态改为 `✅ DONE`
- 由 `complete_task.py` 自动更新

---

### 4. PHASE_COMPLETION_CHECKLIST.md

**位置**: `docs/review/PHASE_COMPLETION_CHECKLIST.md`  
**大小**: 3.3 KB  
**作用**: **Phase完成检查清单模板**

**包含内容**:
- 5大类检查项（计划对照、输出文件、进度追踪、质量自检、自动化验证）
- 执行人声明和验收人复核区域

**何时使用**:
- Phase所有Task完成后
- 填写后提交给用户验收

**注意**: v2.0中，这个清单的作用被自动化脚本部分替代，但仍可作为人工审查的补充。

---

## 🛠️ 可执行脚本说明

### 5. phase_validator.py ✅ **主验证入口**

**位置**: `scripts/phase_validator.py`  
**类型**: Python脚本  
**作用**: **统一的Phase验证框架**

**功能**:
- 根据Phase编号和Task ID，自动加载对应的验证规则
- 执行所有必需的验证检查
- 生成详细的验证报告
- 返回明确的通过/失败结果

**用法**:
```bash
python3 scripts/phase_validator.py --phase 2 --task 2.3
```

**验证规则**:
- Task 2.3: 检查流程图文件、Mermaid图、函数清单、Parser函数完整性
- Task 2.4: （待实现）检查重复检测报告

---

### 6. verify_parser_completeness.py

**位置**: `scripts/verify_parser_completeness.py`  
**类型**: Python脚本  
**作用**: **Parser函数完整性验证**

**功能**:
- 从12个子任务报告中提取所有提到的Parser函数
- 从flowchart_parser_detail.md中提取已包含的函数
- 对比两者，找出遗漏
- 被 `phase_validator.py` 调用

**用法**:
```bash
# 通常不直接调用，由phase_validator.py自动调用
python3 scripts/verify_parser_completeness.py
```

---

### 7. run_task.sh

**位置**: `scripts/run_task.sh`  
**类型**: Bash脚本  
**作用**: **开始Task的工作流脚本**

**功能**:
- 自动更新PHASE_PROGRESS.md状态为IN_PROGRESS
- 提示用户确认开始执行
- 提供执行完成后的验证命令

**用法**:
```bash
./scripts/run_task.sh 2 2.3 "执行Task 2.3"
```

---

### 8. complete_task.py

**位置**: `scripts/complete_task.py`  
**类型**: Python脚本  
**作用**: **完成任务并自动更新状态**

**功能**:
- 更新PHASE_PROGRESS.md状态为DONE
- 自动记录完成时间
- 添加更新日志条目

**用法**:
```bash
python3 scripts/complete_task.py --phase 2 --task 2.3 --output-file flowchart_parser_detail.md
```

---

### 9. verify_phase_completion.py ⚠️ **遗留脚本**

**位置**: `scripts/verify_phase_completion.py`  
**类型**: Python脚本  
**作用**: **v1.0的简单验证脚本**

**功能**:
- 只检查文件是否存在
- 不检查文件内容质量
- 不进行智能对比

**状态**: 
- ❌ **已被phase_validator.py替代**
- 保留仅作历史参考
- **不应再使用**

---

## 📊 验证报告（自动生成）

### 10. validation_report_phase2_task2.3.md

**位置**: `docs/review/reports/validation_report_phase2_task2.3.md`  
**类型**: 自动生成的Markdown报告  
**作用**: **Task 2.3的验证结果记录**

**包含内容**:
- 验证时间
- 总检查项数、通过数、失败数
- 每个检查项的详细结果

**生成方式**:
由 `phase_validator.py` 自动生成，无需手动创建。

---

## 🎯 快速开始指南

### 场景1: 开始一个新的Task

```bash
# 步骤1: 使用工作流脚本开始Task
./scripts/run_task.sh 2 2.4 "执行Task 2.4"

# 步骤2: AI执行Task，生成输出文件
# ... AI工作 ...

# 步骤3: 完成任务（自动更新状态）
python3 scripts/complete_task.py --phase 2 --task 2.4 --output-file task_2.4_duplicate_detection.md

# 步骤4: 运行验证
python3 scripts/phase_validator.py --phase 2 --task 2.4

# 步骤5: 验证通过后，提交给用户验收
```

### 场景2: 查看当前进度

```bash
# 打开进度看板
open docs/review/PHASE_PROGRESS.md

# 或直接在终端查看
cat docs/review/PHASE_PROGRESS.md
```

### 场景3: 查看验证报告

```bash
# 查看最新的验证报告
ls -lt docs/review/reports/validation_report_*.md | head -1

# 打开报告
open docs/review/reports/validation_report_phase2_task2.3.md
```

---

## 🔍 常见问题

### Q1: 我应该阅读哪个文档？

**A**: 
- **学习如何使用防错机制**: 阅读 `AUTOMATED_VALIDATION_GUIDE.md` (v2.0)
- **了解历史教训**: 阅读 `ERROR_PREVENTION_MECHANISM.md` (v1.0)

### Q2: 我应该运行哪个验证脚本？

**A**: 
- **始终使用**: `python3 scripts/phase_validator.py --phase X --task Y`
- **不要使用**: `verify_phase_completion.py`（已过时）

### Q3: 如何知道某个Task需要哪些输出文件？

**A**: 
- 查看 `AUTOMATED_VALIDATION_GUIDE.md` 中对应Task的验证规则
- 或查看 `PROJECT_REVIEW_PLAN_V2.md` 中的Task要求

### Q4: 验证失败了怎么办？

**A**: 
1. 查看验证报告中的详细错误信息
2. 修复问题
3. 重新运行验证
4. **不能跳过验证直接宣布完成**

---

## 📝 维护说明

### 谁负责维护这些文件？

- **AI Assistant**: 负责实现和更新验证脚本
- **用户**: 负责审查文档的清晰度，提出改进建议

### 如何添加新的验证规则？

参考 `AUTOMATED_VALIDATION_GUIDE.md` 中的"如何添加新的验证规则"章节。

### 文档混乱了怎么办？

定期运行此索引文档的审查，确保：
1. 所有文件都有明确的用途说明
2. 废弃的文件有明确标记
3. 文件路径准确无误

---

## ✅ 总结

**防错机制v2.0的核心文件**:

| 类型 | 文件名 | 作用 | 状态 |
|------|--------|------|------|
| 📘 核心文档 | AUTOMATED_VALIDATION_GUIDE.md | v2.0使用指南 | ✅ 当前有效 |
| 🛠️ 主验证脚本 | scripts/phase_validator.py | 统一验证框架 | ✅ 当前有效 |
| 🔄 工作流脚本 | scripts/run_task.sh | 开始Task | ✅ 当前有效 |
| 🔄 工作流脚本 | scripts/complete_task.py | 完成任务 | ✅ 当前有效 |
| 📋 辅助文档 | PHASE_PROGRESS.md | 进度看板 | ✅ 当前有效 |

**已废弃的文件**:

| 文件名 | 替代方案 | 原因 |
|--------|---------|------|
| ERROR_PREVENTION_MECHANISM.md | AUTOMATED_VALIDATION_GUIDE.md | v1.0依赖自觉，失败 |
| verify_phase_completion.py | phase_validator.py | 功能太简单，被替代 |

---

**最后更新**: 2026-04-20 00:30  
**维护人**: AI Assistant
