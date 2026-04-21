#!/bin/bash
# 快速审查脚本 - 1-2小时内完成关键诊断

set -e

echo "========================================="
echo "BlockType 项目快速审查 (v4.0)"
echo "预计时间: 1-2小时"
echo "========================================="
echo ""

# 创建输出目录
mkdir -p docs/review_output

# Phase 1: 主调用链梳理
echo "📋 Phase 1: 主调用链梳理..."
echo "Task 1.1: 追踪主调用链"
grep -rn "^int main\|Driver::compile" tools/*.cpp src/Driver/*.cpp 2>/dev/null | head -20 > docs/review_output/main_chain.txt
echo "✅ 完成: docs/review_output/main_chain.txt"
echo ""

# Phase 3: 问题诊断（核心）
echo "🚨 Phase 3: 问题诊断（核心）..."
echo ""

# Task 3.1: 流程断裂分析
echo "Task 3.1: 流程断裂分析..."
echo "检查 ActOnCallExpr early return..."
grep -n "if (!D)" src/Sema/Sema.cpp | head -5 > docs/review_output/flow_breaks.txt
echo "检查 DeduceAndInstantiateFunctionTemplate 调用..."
grep -rn "DeduceAndInstantiateFunctionTemplate" src/Sema/*.cpp >> docs/review_output/flow_breaks.txt
echo "检查 parseTrailingReturnType 调用..."
grep -rn "parseTrailingReturnType" src/Parse/*.cpp >> docs/review_output/flow_breaks.txt
echo "✅ 完成: docs/review_output/flow_breaks.txt"
echo ""

# Task 3.2: 调用缺失分析
echo "Task 3.2: 调用缺失分析..."
echo "查找未被调用的关键函数..."
# 查找定义但未被调用的函数
for func in "parseTrailingReturnType" "DeduceAndInstantiateFunctionTemplate" "CheckContractCondition"; do
    def_count=$(grep -rn "^.*${func}.*{" src/*.cpp src/*/*.cpp 2>/dev/null | wc -l | tr -d ' ')
    call_count=$(grep -rn "${func}" src/*.cpp src/*/*.cpp 2>/dev/null | wc -l | tr -d ' ')
    # 如果定义次数 >= 调用次数，说明可能未被调用
    if [ "$def_count" -ge "$call_count" ]; then
        echo "⚠️  ${func}: 定义${def_count}次，调用${call_count}次 (可能未被调用)" >> docs/review_output/unused_critical.txt
    fi
done
echo "✅ 完成: docs/review_output/unused_critical.txt"
echo ""

# Phase 4: 优先级排序
echo "📊 Phase 4: 优先级排序..."
echo "生成优先级矩阵..."

cat > docs/review_output/priority_matrix.md << 'EOF'
# 问题优先级矩阵

| 问题ID | 问题描述 | 类型 | 影响 | 严重 | 难度 | 风险 | 优先级 |
|--------|---------|------|------|------|------|------|--------|
EOF

# 添加流程断裂问题
if grep -q "if (!D)" docs/review_output/flow_breaks.txt; then
    echo "| FLOW-001 | ActOnCallExpr early return | A | 5 | 5 | 3 | 2 | **P0** |" >> docs/review_output/priority_matrix.md
fi

# 添加调用缺失问题
if [ -s docs/review_output/unused_critical.txt ]; then
    echo "| CALL-001 | 关键函数未被调用 | B | 4 | 4 | 2 | 1 | **P1** |" >> docs/review_output/priority_matrix.md
fi

echo "✅ 完成: docs/review_output/priority_matrix.md"
echo ""

# 生成摘要报告
echo "📝 生成摘要报告..."
cat > docs/review_output/quick_summary.md << EOF
# 快速审查摘要报告

**执行时间**: $(date)
**审查模式**: 快速审查 (1-2小时)

## 发现的问题

### P0 问题（立即修复）
$(grep "^|.*P0" docs/review_output/priority_matrix.md || echo "无")

### P1 问题（尽快修复）
$(grep "^|.*P1" docs/review_output/priority_matrix.md || echo "无")

## 下一步建议

1. 查看P0问题列表: \`cat docs/review_output/priority_matrix.md | grep P0\`
2. 如果有P0问题，立即修复
3. 否则，查看P1问题并修复

## 详细报告

- 主调用链: docs/review_output/main_chain.txt
- 流程断裂: docs/review_output/flow_breaks.txt
- 未调用函数: docs/review_output/unused_critical.txt
- 优先级矩阵: docs/review_output/priority_matrix.md
EOF

echo "✅ 完成: docs/review_output/quick_summary.md"
echo ""

echo "========================================="
echo "✅ 快速审查完成！"
echo "========================================="
echo ""
echo "📊 查看摘要报告:"
echo "   cat docs/review_output/quick_summary.md"
echo ""
echo "🚨 查看P0问题:"
echo "   cat docs/review_output/priority_matrix.md | grep P0"
echo ""
echo "🔧 自动修复P0问题:"
echo "   python3 scripts/review_auto_fix.py --priority P0"
echo ""
