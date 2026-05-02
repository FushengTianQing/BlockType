# BlockType 项目审查 - 工具使用说明

**创建时间**: 2026-04-19

---

## 📁 文件结构

```
docs/
├── review_task_tracker.csv      # 任务跟踪表（CSV格式）
├── review_issues_log.csv        # Issues日志（CSV格式）
├── review_tech_debt_log.csv     # 技术债务清单（CSV格式）
├── review_progress_report.md    # 自动生成的进度报告
├── PROJECT_REVIEW_PLAN_V2.md    # 审查方案文档
└── REVIEW_TOOLS_README.md       # 本文件

scripts/
└── review_manager.py            # Python管理脚本
```

---

## 🛠️ 工具使用方法

### 1. 查看任务统计摘要

```bash
python3 scripts/review_manager.py summary
```

输出示例：
```
============================================================
📊 任务统计摘要
============================================================
总任务数:     16
待开始:       15
进行中:       1
已完成:       0
被阻塞:       0
完成率:       0.0%
============================================================
```

---

### 2. 更新任务状态

```bash
python3 scripts/review_manager.py update TASK_ID STATUS [OUTPUT_FILE]
```

**示例**：
```bash
# 标记Task 1.1为完成
python3 scripts/review_manager.py update 1.1 DONE docs/architecture/review_flowchart.md

# 标记Task 1.2为进行中
python3 scripts/review_manager.py update 1.2 IN_PROGRESS
```

**状态值**：
- `TODO` - 未开始
- `IN_PROGRESS` - 进行中
- `DONE` - 已完成
- `BLOCKED` - 被阻塞

---

### 3. 添加Issue记录

```bash
python3 scripts/review_manager.py add-issue "TITLE" "TYPE" "SEVERITY" [LOCATION]
```

**示例**：
```bash
python3 scripts/review_manager.py add-issue "ActOnCallExpr early return" "Bug" "P0" "src/Sema/Sema.cpp:L2094"
```

**严重性**：
- `P0` - 阻塞核心功能
- `P1` - 重要功能
- `P2` - 边缘功能
- `P3` - 优化建议

---

### 4. 添加技术债务记录

```bash
python3 scripts/review_manager.py add-debt "TITLE" "TYPE" "SEVERITY" [LOCATION]
```

**示例**：
```bash
python3 scripts/review_manager.py add-debt "parseTrailingReturnType not implemented" "Incomplete" "P1" "src/Parse/ParseDecl.cpp"
```

**技术债务类型**：
- `EmptyFunction` - 空函数/桩代码
- `Incomplete` - 不完整实现
- `TODO` - TODO注释
- `Workaround` - 临时方案

---

### 5. 生成进度报告

```bash
python3 scripts/review_manager.py report
```

生成 `docs/review_progress_report.md`，包含：
- 统计摘要
- 按Phase分组的任务列表
- 每个任务的状态和输出文件

---

## 📊 用Excel查看和编辑

### 打开CSV文件

1. **macOS**: 直接用Numbers或Excel打开 `.csv` 文件
2. **Windows**: 用Excel打开，选择UTF-8编码
3. **Linux**: 用LibreOffice Calc打开

### Excel的优势

✅ **多Sheet管理** - 可以同时打开3个CSV文件  
✅ **排序和过滤** - 按状态、优先级排序  
✅ **公式计算** - 自动统计完成率  
✅ **图表** - 生成进度图、饼图  
✅ **条件格式** - 高亮显示阻塞的任务  

### 编辑后保存

1. 在Excel中编辑
2. **另存为CSV (UTF-8)** 格式
3. 确保字段顺序不变
4. 不要添加额外的列

---

## 🔄 工作流程

### 标准流程

```
1. 开始一个Task
   ↓
   python3 scripts/review_manager.py update 1.1 IN_PROGRESS

2. 执行Task
   ↓
   [人工执行具体的审查工作]

3. 发现问题
   ↓
   python3 scripts/review_manager.py add-issue "..." "..." "P0"

4. 完成任务
   ↓
   python3 scripts/review_manager.py update 1.1 DONE output.md

5. 生成报告
   ↓
   python3 scripts/review_manager.py report

6. 查看报告
   ↓
   cat docs/review_progress_report.md
```

---

## 📝 CSV文件格式说明

### Task Tracker CSV

```csv
TaskID,TaskName,Phase,Status,Dependency,StartTime,EndTime,OutputFile,Notes
1.1,梳理主调用链,Phase1,IN_PROGRESS,-,2026-04-19,,,找到main()入口
```

**字段说明**：
- `TaskID`: 任务编号（如 1.1, 2.3）
- `TaskName`: 任务名称
- `Phase`: 所属阶段（Phase1-5, Final）
- `Status`: 状态（TODO/IN_PROGRESS/DONE/BLOCKED）
- `Dependency`: 依赖的其他Task（多个用逗号，加引号）
- `StartTime`: 开始时间（自动生成）
- `EndTime`: 完成时间（自动生成）
- `OutputFile`: 输出的文件路径
- `Notes`: 备注

---

### Issues Log CSV

```csv
IssueID,Title,Type,Severity,Status,FoundInTask,FoundTime,FixedTime,Location,Description,...
001,ActOnCallExpr early return,Bug,P0,IDENTIFIED,3.1,2026-04-19,,src/Sema/Sema.cpp:L2094,...
```

**关键字段**：
- `IssueID`: 自动编号（001, 002...）
- `Type`: Bug/Breaking/Missing
- `Severity`: P0/P1/P2/P3
- `Status`: IDENTIFIED/ANALYZING/FIXING/FIXED/CLOSED

---

### Tech Debt Log CSV

```csv
DebtID,Title,Type,Severity,Status,Location,FoundTime,FixedTime,Description,...
001,parseTrailingReturnType,Incomplete,P1,RECORDED,src/Parse/ParseDecl.cpp,2026-04-19,,...
```

**关键字段**：
- `DebtID`: 自动编号（001, 002...）
- `Type`: EmptyFunction/Incomplete/TODO/Workaround
- `CompletionPercent`: 完成百分比（0%-100%）

---

## ⚠️ 注意事项

### 1. CSV编辑规则

❌ **不要**：
- 删除表头行
- 改变列的顺序
- 添加额外的列
- 使用特殊字符（如换行符）在字段中

✅ **应该**：
- 用Excel的"另存为CSV UTF-8"
- 保持字段名不变
- 如果字段中有逗号，用引号包裹

### 2. 依赖关系

如果Task A依赖Task B：
- Task B的Dependency字段填写A的ID
- 在Task B完成前，Task A应该是BLOCKED状态

### 3. 时间格式

统一使用：`YYYY-MM-DD HH:MM`  
示例：`2026-04-19 15:30`

### 4. 备份

每次大批量编辑前：
```bash
cp docs/review_task_tracker.csv docs/review_task_tracker.csv.bak
```

---

## 🔧 高级用法

### 批量更新任务状态

```python
# 编写Python脚本
import sys
sys.path.insert(0, 'scripts')
from review_manager import TaskManager

mgr = TaskManager()
for task_id in ['1.1', '1.2', '1.3']:
    mgr.update_task_status(task_id, 'DONE')
```

### 自定义报告

修改 `scripts/review_manager.py` 中的 `generate_markdown_report()` 函数，添加自定义统计。

### 导出为Excel

```python
import pandas as pd
import csv

# 读取CSV
df = pd.read_csv('docs/review_task_tracker.csv')

# 导出为Excel
df.to_excel('docs/review_task_tracker.xlsx', index=False)
```

---

## 📞 常见问题

### Q: CSV文件乱码怎么办？
A: 用Excel打开时选择UTF-8编码，或用文本编辑器转换编码。

### Q: 如何恢复误删的数据？
A: 从备份文件恢复，或从Git历史中找回。

### Q: 可以多人同时编辑吗？
A: 不建议。CSV不适合并发编辑，最好一人编辑后提交Git，其他人拉取后再编辑。

### Q: 如何查看某个Phase的所有任务？
A: 用Excel打开，按Phase列筛选；或用grep：
```bash
grep "Phase1" docs/review_task_tracker.csv
```

---

## 🎯 最佳实践

1. **每次完成任务后立即更新状态** - 不要累积
2. **发现问题立即记录** - 避免遗忘
3. **定期生成报告** - 每天或每完成一个Phase
4. **用Excel查看** - 更直观，支持排序过滤
5. **保持CSV简洁** - 详细说明写在MD文档中
6. **Git提交CSV变更** - 保留历史记录

---

**最后提醒**: 这个工具系统的核心价值是**结构化数据 + 自动化管理**，充分利用它可以大大提高审查效率！
