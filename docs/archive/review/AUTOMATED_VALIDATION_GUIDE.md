# 防错机制使用指南（强制执行版 v3.0）

**版本**: v3.0 - 7-Phase完整方案  
**创建时间**: 2026-04-20 00:15  
**更新时间**: 2026-04-20 00:00  
**核心原则**: **防错机制必须自动生效，不能依赖自觉**  
**计划版本**: PROJECT_REVIEW_PLAN_V2.md v3.0（7个Phase，28个Task，22个功能域）

---

## 🎯 为什么v1.0失败了？

v1.0的防错机制是"纸面文章"：
- ❌ 创建了检查清单，但没有强制填写
- ❌ 创建了验证脚本，但没有自动运行
- ❌ 创建了进度看板，但没有自动更新
- ❌ 依赖AI的"自觉"，而自觉是不可靠的

**v2.0的改进**：
- ✅ 验证脚本**自动运行**，失败时明确阻断
- ✅ 状态更新**自动化**，减少人工操作
- ✅ 工作流**强制集成**，不验证就不能继续
- ✅ 所有检查**可追溯**，生成详细报告

---

## 🔄 标准工作流程（强制执行）

### 方式1: 使用自动化工作流脚本（推荐）

```bash
# 步骤1: 开始Task（自动更新状态为IN_PROGRESS）
./scripts/run_task.sh 2 2.3 "执行Task 2.3"

# 步骤2: AI执行Task，生成输出文件
# ... AI工作 ...

# 步骤3: 完成任务（自动记录完成时间）
python3 scripts/complete_task.py --phase 2 --task 2.3 --output-file flowchart_parser_detail.md

# 步骤4: 运行自动化验证（失败则阻断）
python3 scripts/phase_validator.py --phase 2 --task 2.3

# 步骤5: 验证通过后，提交给用户验收
# 用户确认后，才能继续下一个Task
```

### 方式2: 手动执行（不推荐，容易遗漏）

如果选择手动执行，**必须**按以下顺序：

```bash
# 1. 手动更新PHASE_PROGRESS.md状态为IN_PROGRESS
# 2. 执行Task
# 3. 运行验证脚本
python3 scripts/phase_validator.py --phase 2 --task 2.3

# 4. 验证通过后，手动更新状态为DONE
# 5. 提交给用户验收
```

⚠️ **警告**: 手动执行容易遗漏步骤，强烈建议使用方式1。

---

## 📋 各Phase的验证规则

### Phase 2 Task 2.3: 映射到流程图

**必需输出文件**:
- `flowchart_main.md` - 主流程图
- `flowchart_parser_detail.md` - Parser层详细图
- （其他子图按需）

**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 包含Mermaid流程图
3. ✅ 包含函数清单表格
4. ✅ Parser函数完整性验证通过（自动对比12个子任务报告）
5. ✅ 无遗漏函数

**运行验证**:
```bash
python3 scripts/phase_validator.py --phase 2 --task 2.3
```

---

### Phase 2 Task 2.4: 重复检测

**必需输出文件**:
- `task_2.4_duplicate_detection.md`

**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 包含重复函数对列表
3. ✅ 每个重复对都有分析结论
4. ✅ 包含决策建议（保留/合并/删除）

**运行验证**:
```bash
python3 scripts/phase_validator.py --phase 2 --task 2.4
```

---

### Phase 5: 工程质量保障审查（新增Phase）

**Task 5.1-5.4的验证规则**:

#### Task 5.1: 测试覆盖率分析
**必需输出文件**: `task_5.1_test_coverage.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 包含各模块的测试统计
3. ✅ 列出untested的功能
4. ✅ 评估测试用例质量

#### Task 5.2: 文档完整性审查
**必需输出文件**: `task_5.2_documentation_review.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 检查关键函数注释
3. ✅ 评估README准确性
4. ✅ 列出缺失的文档

#### Task 5.3: 构建系统审查
**必需输出文件**: `task_5.3_build_system_review.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 分析CMakeLists.txt结构
3. ✅ 验证依赖关系
4. ✅ 提出优化建议

#### Task 5.4: C++标准合规性审查
**必需输出文件**: `task_5.4_cpp_standard_compliance.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 列出已实现的C++26特性
3. ✅ 对照标准文档
4. ✅ 评估实现正确性

---

### Phase 6: 性能优化与深度技术审查（新增Phase）

**Task 6.1-6.5的验证规则**:

#### Task 6.1: 性能Profiling
**必需输出文件**: `task_6.1_performance_profiling.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 包含编译时间数据
3. ✅ 识别性能瓶颈
4. ✅ 提出优化建议

#### Task 6.2: 错误恢复机制审查
**必需输出文件**: `task_6.2_error_recovery_review.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 检查consumeToken使用
3. ✅ 验证无限循环防护
4. ✅ 评估错误消息质量

#### Task 6.3: 模板系统深度审查
**必需输出文件**: `task_6.3_template_system_review.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 分析SFINAE实现
3. ✅ 检查约束满足
4. ✅ 验证包展开支持

#### Task 6.4: Lambda表达式深度审查
**必需输出文件**: `task_6.4_lambda_expression_review.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 分析捕获列表处理
3. ✅ 检查闭包类型生成
4. ✅ 验证泛型Lambda支持

#### Task 6.5: 异常处理深度审查
**必需输出文件**: `task_6.5_exception_handling_review.md`
**验证检查项**:
1. ✅ 输出文件存在且非空
2. ✅ 分析try/catch实现
3. ✅ 检查栈展开机制
4. ✅ 验证noexcept支持

---

## 🛠️ 如何添加新的验证规则

### 步骤1: 创建验证函数

在 `scripts/phase_validator.py` 中添加新的验证方法：

```python
def verify_task_2_4(self):
    """验证Task 2.4: 重复检测"""
    print("\n" + "="*70)
    print(f"验证 Task 2.4: 重复检测")
    print("="*70 + "\n")
    
    # 检查1: 输出文件是否存在
    report_file = REPORTS_DIR / "task_2.4_duplicate_detection.md"
    if report_file.exists():
        self.add_result(
            "输出文件存在",
            True,
            f"✅ task_2.4_duplicate_detection.md 存在"
        )
    else:
        self.add_result(
            "输出文件存在",
            False,
            "❌ task_2.4_duplicate_detection.md 不存在"
        )
        return False
    
    # 检查2: 包含重复函数对
    with open(report_file, 'r', encoding='utf-8') as f:
        content = f.read()
        if '重复函数对' in content or 'Duplicate Functions' in content:
            self.add_result(
                "包含重复函数对",
                True,
                "✅ 报告包含重复函数对分析"
            )
        else:
            self.add_result(
                "包含重复函数对",
                False,
                "❌ 报告缺少重复函数对分析"
            )
    
    # ... 更多检查项 ...
    
    return all(r['passed'] for r in self.results)
```

### 步骤2: 在run()方法中注册

```python
def run(self):
    """执行验证"""
    # ...
    
    # 根据task_id选择验证方法
    if self.task_id == "2.3":
        success = self.verify_task_2_3()
    elif self.task_id == "2.4":
        success = self.verify_task_2_4()  # ← 新增
    else:
        print(f"⚠️ 未定义Task {self.task_id}的验证规则")
        return False
    
    # ...
```

---

## 🚫 禁止的行为

以下行为**严格禁止**，一旦发现将触发审计：

1. ❌ **跳过验证直接宣布完成**
   - 错误做法: 生成文件后直接说"Task完成"
   - 正确做法: 先运行`phase_validator.py`，验证通过后再宣布

2. ❌ **手动修改验证结果**
   - 错误做法: 验证失败后，手动修改脚本让它通过
   - 正确做法: 修复实际问题，然后重新验证

3. ❌ **伪造时间戳或进度**
   - 错误做法: 手动编辑PHASE_PROGRESS.md填写虚假时间
   - 正确做法: 使用`complete_task.py`自动记录时间

4. ❌ **未经用户确认就继续下一个Task**
   - 错误做法: 验证通过后，立即开始下一个Task
   - 正确做法: 提交验收报告，等待用户确认

---

## 📊 验证报告示例

运行验证后，会生成详细的报告文件：

```
docs/review/reports/validation_report_phase2_task2.3.md
```

报告内容包括：
- 验证时间
- 总检查项数、通过数、失败数
- 每个检查项的详细结果
- 失败时的具体错误信息

**示例输出**:
```
======================================================================
验证总结:
======================================================================
总检查项: 5
通过: 5
失败: 0

✅ 验证通过！可以继续下一步。
======================================================================
```

或

```
======================================================================
验证总结:
======================================================================
总检查项: 5
通过: 3
失败: 2

❌ 验证失败！请修复上述问题后再继续。

详细报告: docs/review/reports/validation_report_phase2_task2.3.md
======================================================================
```

---

## 🔍 故障排查

### 问题1: 验证脚本找不到输出文件

**原因**: 文件路径错误或文件未生成

**解决**:
1. 检查文件是否在正确的目录（`docs/review/reports/`）
2. 确认文件名完全匹配（包括大小写）
3. 重新生成输出文件

### 问题2: Parser函数完整性验证失败

**原因**: flowchart_parser_detail.md遗漏了某些函数

**解决**:
1. 查看验证报告中的遗漏函数列表
2. 将这些函数添加到Mermaid图和表格中
3. 重新运行验证

### 问题3: 验证通过但用户认为不完整

**原因**: 自动化验证只能检查结构性问题，无法判断质量

**解决**:
1. 这是正常的，自动化验证只是第一道防线
2. 用户的验收是最终的质量保证
3. 根据用户反馈补充内容
4. 如有必要，增强验证规则

---

## 📝 维护说明

### 谁负责维护验证规则？

- **AI Assistant**: 负责实现和更新验证脚本
- **用户**: 负责审查验证规则的合理性，提出改进建议

### 何时更新验证规则？

1. 发现新的遗漏类型时
2. Task要求发生变化时
3. 用户提出改进建议时

### 如何测试验证脚本？

```bash
# 测试Phase 2 Task 2.3验证
python3 scripts/phase_validator.py --phase 2 --task 2.3

# 查看所有可用的验证规则
python3 scripts/phase_validator.py --help
```

---

## ✅ 承诺

我（AI Assistant）郑重承诺：

1. **每次Task完成后，必须运行验证脚本**
2. **验证失败时，绝不擅自宣布完成**
3. **不使用任何手段绕过验证**
4. **接受用户的最终验收和审计**
5. **持续改进验证规则，提高自动化程度**

**签字**: AI Assistant  
**日期**: 2026-04-20 00:15

---

## 📚 相关文件

- [phase_validator.py](../scripts/phase_validator.py) - 统一验证框架
- [verify_parser_completeness.py](../scripts/verify_parser_completeness.py) - Parser函数完整性验证
- [run_task.sh](../scripts/run_task.sh) - 任务执行工作流脚本
- [complete_task.py](../scripts/complete_task.py) - 自动更新状态脚本
- [PHASE_PROGRESS.md](PHASE_PROGRESS.md) - 实时进度看板
- [PROJECT_REVIEW_PLAN_V2.md](PROJECT_REVIEW_PLAN_V2.md) - 项目审查计划
