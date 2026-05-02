# BlockType 功能集成状态报告

**生成时间**: 2026-04-21 22:20  
**最后更新**: 2026-04-21 22:20  
**扫描范围**: src/Sema, src/Parse  
**总函数数**: Sema (128), Parser (98)

---

## 📊 统计摘要

| 模块 | 总函数数 | 已集成 | 游离 | 集成率 |
|------|---------|--------|------|--------|
| Sema | 128 | 126 | 2 | 98.4% |
| Parser | 98 | 96 | 2 | 97.9% |

**进度**: ✅ P1-P2 优先级函数已全部集成（16个函数）

---

## 🔴 游离功能清单

### Sema 模块（4个未调用函数）

#### 1. ActOnAssumeAttr
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理 `[[assume]]` 属性
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseDecl.cpp:1603-1630
- **提交**: 015c5b9

#### 2. ActOnCXXNamedCastExpr
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理命名类型转换（static_cast, dynamic_cast等）
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseExprCXX.cpp:691, 734, 778, 821
- **提交**: 之前提交
- **备注**: 通过 ActOnCXXNamedCastExprWithType 集成

#### 3. ActOnClassTemplatePartialSpecialization
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理类模板偏特化
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseTemplate.cpp:291-294
- **提交**: 015c5b9

#### 4. ActOnDeclStmt
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理声明语句
- **状态**: ⚠️ 已删除 (2026-04-21)
- **原因**: 已有 ActOnDeclStmtFromDecl 和 ActOnDeclStmtFromDecls 作为替代
- **提交**: 之前提交

#### 5. ActOnDeclarator
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理声明符
- **状态**: ✅ 已分析 (2026-04-21)
- **结论**: 架构设计选择，功能已被具体函数（ActOnVarDecl等）覆盖
- **提交**: 3c38a2e

#### 6. ActOnExplicitInstantiation
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理显式实例化（template class Foo<int>;）
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseTemplate.cpp:52-68
- **提交**: 015c5b9

#### 7. ActOnExplicitSpecialization
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理显式特化（template<> class Foo<int> {...};）
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseTemplate.cpp:75-79
- **提交**: 015c5b9

#### 8. ActOnExpr
- **文件**: src/Sema/Sema.cpp
- **功能**: 通用表达式处理
- **状态**: ⚠️ 已删除 (2026-04-21)
- **原因**: 无实际用途的透传函数
- **提交**: 之前提交

#### 9. ActOnFunctionDeclFull
- **文件**: src/Sema/Sema.cpp
- **功能**: 完整的函数声明处理（含 inline/constexpr/consteval）
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseDecl.cpp:1946-1948
- **提交**: 3c38a2e

#### 10. ActOnMemberExpr
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理成员访问表达式
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseExpr.cpp:545, 571
- **提交**: ad11c25
- **备注**: 已重构 Parser，移除重复的成员查找逻辑

#### 11. ActOnTypeAliasTemplateDecl
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理别名模板（template<class T> using Vec = std::vector<T>;）
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseTemplate.cpp:349-358
- **提交**: 015c5b9

#### 12. ActOnVarTemplateDecl / ActOnVarTemplatePartialSpecialization
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理变量模板
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseTemplate.cpp:340-358
- **提交**: 015c5b9, 13bd0ad

---

### Parser 模块（5个未调用函数）

#### 1. parseCXXForRangeStatement
- **文件**: src/Parse/ParseStmt.cpp
- **功能**: 解析 range-based for 循环
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseStmt.cpp:813-856
- **提交**: 之前提交
- **备注**: 已重构为调用专用函数，提升模块化

#### 2. parseCoawaitExpression
- **文件**: src/Parse/ParseExpr.cpp
- **功能**: 解析 co_await 表达式
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseExpr.cpp:694-695
- **提交**: 当前提交
- **备注**: 在 parsePrimaryExpression 中处理 C++20 协程

#### 3. parseFoldExpression
- **文件**: src/Parse/ParseExpr.cpp
- **功能**: 解析折叠表达式
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseExpr.cpp:670-677
- **提交**: 当前提交
- **备注**: 在 parsePrimaryExpression 中检测折叠表达式模式

#### 4. parseMemberPointerType
- **文件**: src/Parse/ParseType.cpp
- **功能**: 解析成员指针类型
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseType.cpp:372-383
- **提交**: 当前提交
- **备注**: 在 parseDeclarator 中检测 `Class::*` 模式

#### 5. parsePackIndexingExpr
- **文件**: src/Parse/ParseExpr.cpp
- **功能**: 解析包索引表达式（C++26）
- **状态**: ❌ 未集成
- **影响**: C++26 pack indexing 不支持
- **优先级**: P3（未来标准）

#### 6. parseTrailingReturnType
- **文件**: src/Parse/ParseType.cpp
- **功能**: 解析尾置返回类型
- **状态**: ✅ 已集成 (2026-04-21)
- **集成位置**: ParseType.cpp:968-980, ParseDecl.cpp:1928-1936
- **提交**: 当前提交
- **备注**: 扩展 FunctionInfo 结构，支持 `auto func() -> int` 语法

---

## ⚠️ 已知逻辑错误

### 1. ActOnCallExpr 第2094-2098行：早期返回导致 unreachable code

**位置**: src/Sema/Sema.cpp L2094-2098

```cpp
if (!D) {
  // Undeclared identifier — fall back to creating CallExpr directly
  auto *CE = Context.create<CallExpr>(LParenLoc, Fn, Args);
  return ExprResult(CE);  // ← 提前返回
}
// 第2104-2128行的 FunctionTemplateDecl 处理逻辑永远无法执行
```

**问题**: 
- 当 DeclRefExpr 的 Decl 为 nullptr 时，直接创建无效的 CallExpr 并返回
- 跳过了第2104-2128行的模板推导逻辑
- 导致函数模板调用无法工作

**影响**: 
- `make_my_pair(42, 3.14)` 无法进行模板实参推导
- 这是当前最紧急的问题

**修复方向**: 
- 需要在 D=nullptr 时，尝试从 Fn 或其他上下文恢复信息
- 或者在 Parser 层避免创建 DeclRefExpr(nullptr)

---

## 🎯 集成优先级建议

### ✅ 已完成（2026-04-21）

**P1 优先级（4个函数）**:
1. ✅ ActOnExplicitSpecialization - 模板显式特化验证
2. ✅ ActOnTypeAliasTemplateDecl - 别名模板验证
3. ✅ ActOnExplicitInstantiation - 显式实例化处理
4. ✅ ActOnMemberExpr - 成员访问表达式验证
5. ✅ ActOnCXXNamedCastExpr - 命名类型转换

**P2 优先级（5个函数）**:
4. ✅ ActOnClassTemplatePartialSpecialization - 类模板偏特化验证
5. ✅ ActOnVarTemplateDecl - 变量模板验证
6. ✅ ActOnVarTemplatePartialSpecialization - 变量模板偏特化验证
7. ✅ ActOnAssumeAttr - C++23 [[assume]] 属性
8. ✅ parseCXXForRangeStatement - range-based for 循环

**P3 优先级（2个函数）**:
9. ✅ ActOnDeclarator - 架构选择，保留作为辅助函数
10. ✅ ActOnFunctionDeclFull - 函数声明完整参数支持

---

### 🔴 待处理（剩余1个函数）

**P1-P2**: ✅ 全部完成

**P3（后续规划）**:
1. **parsePackIndexingExpr** - C++26 包索引表达式

---

## 📝 下一步行动

### ✅ 已完成工作（2026-04-21）

**集成成果**:
- ✅ 10个游离函数已成功集成
- ✅ Sema 模块集成率从 90.6% 提升至 96.9%
- ✅ Parser 模块集成率从 93.9% 提升至 94.9%
- ✅ 所有修改已通过编译测试

**关键修复**:
1. 模板系统完整性：显式特化、显式实例化、偏特化验证
2. 函数声明完整性：inline/constexpr/consteval 标志支持
3. C++23 特性：[[assume]] 属性解析
4. 变量模板：验证和注册逻辑

---

### 🔜 待处理工作

**Step 1: ✅ 已完成 - P1 函数**
- ✅ ActOnCXXNamedCastExpr（命名类型转换）
- ✅ ActOnMemberExpr（成员访问）
- ✅ parseTrailingReturnType（尾置返回类型）

**Step 2: ✅ 已完成 - P2 边缘功能**
- ✅ parseMemberPointerType（成员指针类型）
- ✅ parseCoawaitExpression（C++20 协程）
- ✅ parseFoldExpression（C++17 折叠表达式）

**Step 3: 未来标准支持**
- parsePackIndexingExpr（C++26 包索引）

---

## 🔍 附录：扫描方法

```bash
# 扫描 Sema 函数
grep -rh "^ExprResult Sema::\|^StmtResult Sema::\|^DeclResult Sema::" src/Sema/*.cpp | \
  sed 's/.*Sema:://' | sed 's/(.*$//' | sort -u > /tmp/sema_functions.txt

# 检查使用情况
while IFS= read -r func; do
  count=$(grep -r "${func}(" src/Parse/*.cpp src/Sema/*.cpp 2>/dev/null | \
          grep -v "^src/Sema/.*::${func}" | wc -l)
  if [ "$count" -eq 0 ]; then
    echo "UNUSED: $func"
  fi
done < /tmp/sema_functions.txt
```
