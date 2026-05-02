# BlockType 项目审查工作区

**创建时间**: 2026-04-19  
**用途**: 集中管理项目审查相关的所有文档、数据和报告

---

## 📁 文件夹结构

```
project_review/
├── README.md                          # 本文件 - 工作区说明
├── PROJECT_REVIEW_PLAN_V2.md          # 📋 审查方案（主文档）
├── REVIEW_TOOLS_README.md             # 🛠️ 工具使用说明
│
├── data/                              # 📊 结构化数据（CSV格式）
│   ├── review_task_tracker.csv        # 任务跟踪表
│   ├── review_issues_log.csv          # Issues日志
│   └── review_tech_debt_log.csv       # 技术债务清单
│
├── reports/                           # 📄 生成的报告
│   ├── review_progress_report.md      # 实时进度报告（自动生成）
│   ├── review_task_1.1_report.md      # Task 1.1 详细报告
│   └── FUNCTION_INTEGRATION_REPORT.md # 功能集成状态报告（旧版）
│
└── analysis/                          # 🔍 分析文档（待创建）
    └── ...                            # 各模块的深度分析报告
```

---

## 🎯 各文件夹用途

### `data/` - 结构化数据
**存放**: CSV格式的跟踪表  
**特点**: 
- 可用Excel打开编辑
- Python脚本自动更新
- 结构化查询和统计

**文件**:
- `review_task_tracker.csv` - 16个Task的状态跟踪
- `review_issues_log.csv` - 发现的问题记录
- `review_tech_debt_log.csv` - 技术债务清单

---

### `reports/` - 生成的报告
**存放**: Markdown格式的报告文档  
**特点**:
- 人类可读
- 包含详细分析和图表
- 按Task或Phase组织

**文件**:
- `review_progress_report.md` - 自动生成的进度总览
- `review_task_X.X_report.md` - 每个Task的详细报告
- 其他阶段性总结报告

---

### `analysis/` - 深度分析
**存放**: 各模块的详细分析文档  
**特点**:
- 根因分析
- 架构评估
- 修复方案设计

**计划中的文件**:
- `parser_analysis.md` - Parser模块深度分析
- `sema_analysis.md` - Sema模块深度分析
- `codegen_analysis.md` - CodeGen模块深度分析
- `integration_analysis.md` - 集成问题分析

---

### 根目录文件
- `README.md` - 本工作区说明
- `PROJECT_REVIEW_PLAN_V2.md` - **审查方案主文档**（最重要）
- `REVIEW_TOOLS_README.md` - 工具使用指南

---

## 🔄 工作流程

### 1. 查看审查方案
```bash
cat docs/review/PROJECT_REVIEW_PLAN_V2.md
```

### 2. 查看当前进度
```bash
cat docs/review/reports/review_progress_report.md
```

### 3. 用Excel查看数据
```bash
open docs/review/data/review_task_tracker.csv  # macOS
```

### 4. 更新任务状态
```bash
python3 scripts/review_manager.py update 1.2 IN_PROGRESS
```

### 5. 生成新报告
```bash
python3 scripts/review_manager.py report
```

### 6. 查看Task详细报告
```bash
cat docs/review/reports/review_task_1.1_report.md
```

---

## 📝 命名规范

### 数据文件 (data/)
- 格式: `review_<type>.csv`
- 示例: `review_task_tracker.csv`

### 报告文件 (reports/)
- 进度报告: `review_progress_report.md`
- Task报告: `review_task_<ID>_report.md`
- Phase报告: `review_phase_<N>_summary.md`

### 分析文件 (analysis/)
- 格式: `<module>_analysis.md`
- 示例: `parser_analysis.md`, `sema_analysis.md`

---

## 🔗 快速链接

### 核心文档
- [审查方案](PROJECT_REVIEW_PLAN_V2.md) - **从这里开始**
- [工具说明](REVIEW_TOOLS_README.md) - 如何使用Python脚本

### 数据文件
- [任务跟踪表](data/review_task_tracker.csv)
- [Issues日志](data/review_issues_log.csv)
- [技术债务](data/review_tech_debt_log.csv)

### 最新报告
- [进度报告](reports/review_progress_report.md)
- [Task 1.1报告](reports/review_task_1.1_report.md)

---

## 💡 给AI助手的提示

**当你需要**:

1. **了解审查方案** → 阅读 `PROJECT_REVIEW_PLAN_V2.md`
2. **查看任务状态** → 读取 `data/review_task_tracker.csv`
3. **更新任务** → 运行 `python3 scripts/review_manager.py update ...`
4. **查看进度** → 读取 `reports/review_progress_report.md`
5. **添加Issue** → 运行 `python3 scripts/review_manager.py add-issue ...`
6. **生成报告** → 运行 `python3 scripts/review_manager.py report`

**重要提醒**:
- 所有审查相关文件都在 `docs/review/` 下
- 不要在其他地方创建审查相关的文档
- 保持文件夹结构清晰，便于后续查找

---

## 📊 当前状态

**审查进度**: Phase 1-2 完成，Phase 3-7 待开始
**当前Phase**: Phase 3 - 问题诊断与根因分析
**已完成**: Phase 1（流程地图）、Phase 2（功能域分析，22 个功能域，~50 个问题）
**下一个Task**: Task 3.1 - 根因分析

---

**最后更新**: 2026-04-23
