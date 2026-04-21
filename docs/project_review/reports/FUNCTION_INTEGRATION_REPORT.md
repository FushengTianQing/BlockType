# BlockType 功能集成状态报告

**生成时间**: 2026-04-19  
**扫描范围**: src/Sema, src/Parse  
**总函数数**: Sema (128), Parser (98)

---

## 📊 统计摘要

| 模块 | 总函数数 | 已集成 | 游离 | 集成率 |
|------|---------|--------|------|--------|
| Sema | 128 | 116 | 12 | 90.6% |
| Parser | 98 | 92 | 6 | 93.9% |

---

## 🔴 游离功能清单

### Sema 模块（12个未调用函数）

#### 1. ActOnAssumeAttr
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理 `[[assume]]` 属性
- **状态**: ❌ 未集成
- **影响**: C++23 assume 属性无法使用
- **优先级**: P2（边缘功能）

#### 2. ActOnCXXNamedCastExpr
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理命名类型转换（static_cast, dynamic_cast等）
- **状态**: ❌ 未集成
- **影响**: C-style cast 可能工作，但命名 cast 不工作
- **优先级**: P1（重要功能缺失）

#### 3. ActOnClassTemplatePartialSpecialization
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理类模板偏特化
- **状态**: ❌ 未集成
- **影响**: 类模板偏特化语法无法编译
- **优先级**: P1（模板系统不完整）

#### 4. ActOnDeclStmt
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理声明语句
- **状态**: ❌ 未集成
- **备注**: 已有 ActOnDeclStmtFromDecl 和 ActOnDeclStmtFromDecls，此函数可能是旧版本
- **优先级**: P2（可能有替代方案）

#### 5. ActOnDeclarator
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理声明符
- **状态**: ❌ 未集成
- **备注**: 可能在 Parser 层直接构建 AST，绕过了此函数
- **优先级**: P2（架构设计选择）

#### 6. ActOnExplicitInstantiation
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理显式实例化（template class Foo<int>;）
- **状态**: ❌ 未集成
- **影响**: 无法显式实例化模板
- **优先级**: P2（高级功能）

#### 7. ActOnExplicitSpecialization
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理显式特化（template<> class Foo<int> {...};）
- **状态**: ❌ 未集成
- **影响**: 无法显式特化模板
- **优先级**: P1（模板系统核心功能）

#### 8. ActOnExpr
- **文件**: src/Sema/Sema.cpp
- **功能**: 通用表达式处理
- **状态**: ❌ 未集成
- **备注**: 可能是预留接口，实际使用更具体的 ActOnXXXExpr
- **优先级**: P3（可能废弃）

#### 9. ActOnFunctionDeclFull
- **文件**: src/Sema/Sema.cpp
- **功能**: 完整的函数声明处理
- **状态**: ❌ 未集成
- **备注**: 已有 ActOnFunctionDecl，此函数可能是扩展版本
- **优先级**: P2（需确认是否必需）

#### 10. ActOnMemberExpr
- **文件**: src/Sema/Sema.cpp
- **功能**: 处理成员访问表达式
- **状态**: ❌ 未集成
- **影响**: 成员访问可能在 Parser 层直接构建
- **优先级**: P1（如果确实未工作，严重影响）

#### 11. ActOnTypeAliasTemplateDecl
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理别名模板（template<class T> using Vec = std::vector<T>;）
- **状态**: ❌ 未集成
- **影响**: 别名模板语法无法编译
- **优先级**: P1（C++11 核心功能）

#### 12. ActOnVarTemplateDecl / ActOnVarTemplatePartialSpecialization
- **文件**: src/Sema/SemaTemplate.cpp
- **功能**: 处理变量模板
- **状态**: ❌ 未集成
- **影响**: 变量模板语法无法编译
- **优先级**: P2（C++14 功能，使用较少）

---

### Parser 模块（6个未调用函数）

#### 1. parseCXXForRangeStatement
- **文件**: src/Parse/ParseStmt.cpp
- **功能**: 解析 range-based for 循环
- **状态**: ✅ 已集成
- **修复**: 重构 `parseForStatement` 调用此函数，提高代码模块化
- **架构优势**: 符合单一职责原则，便于维护和测试

#### 2. parseCoawaitExpression
- **文件**: src/Parse/ParseExpr.cpp
- **功能**: 解析 co_await 表达式
- **状态**: ❌ 未集成
- **影响**: C++20 coroutine 不支持
- **优先级**: P2（高级功能）

#### 3. parseFoldExpression
- **文件**: src/Parse/ParseExpr.cpp
- **功能**: 解析折叠表达式
- **状态**: ❌ 未集成
- **影响**: C++17 fold expression 不支持
- **优先级**: P2（模板元编程功能）

#### 4. parseMemberPointerType
- **文件**: src/Parse/ParseType.cpp
- **功能**: 解析成员指针类型
- **状态**: ❌ 未集成
- **影响**: `int Class::*ptr` 语法不支持
- **优先级**: P2（高级类型系统）

#### 5. parsePackIndexingExpr
- **文件**: src/Parse/ParseExpr.cpp
- **功能**: 解析包索引表达式（C++26）
- **状态**: ❌ 未集成
- **影响**: C++26 pack indexing 不支持
- **优先级**: P3（未来标准）

#### 6. parseTrailingReturnType
- **文件**: src/Parse/ParseDecl.cpp
- **功能**: 解析尾置返回类型
- **状态**: ❌ 未集成
- **影响**: `auto func() -> int` 语法不支持
- **优先级**: P1（C++11 常用功能）

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

### P0（立即修复）
1. **ActOnCallExpr 的 unreachable code 问题** - 阻塞函数模板调用
2. **parseCXXForRangeStatement** - 如果 range-for 确实不工作，这是严重问题

### P1（本周完成）
3. **ActOnCXXNamedCastExpr** - 命名类型转换
4. **ActOnExplicitSpecialization** - 模板显式特化
5. **ActOnMemberExpr** - 确认成员访问是否工作
6. **ActOnTypeAliasTemplateDecl** - 别名模板
7. **parseTrailingReturnType** - 尾置返回类型

### P2（本月完成）
8. **ActOnClassTemplatePartialSpecialization** - 类模板偏特化
9. **ActOnExplicitInstantiation** - 显式实例化
10. **parseFoldExpression** - 折叠表达式
11. **其他边缘功能**

### P3（后续规划）
12. **C++26 新特性**
13. **Coroutine 支持**

---

## 📝 下一步行动

### Step 1: 验证关键功能是否真的不工作
对每个"游离功能"，编写测试用例验证：
```cpp
// 测试 range-based for
void test_range_for() {
    std::vector<int> v = {1, 2, 3};
    for (auto x : v) {}  // 应该能编译
}
```

### Step 2: 追踪调用路径
对每个未集成的函数，找出：
- 应该在何处调用
- 为什么没有被调用
- 集成难度评估

### Step 3: 制定集成计划
按优先级逐个集成，每集成一个功能：
- 添加测试用例
- 更新本文档
- 提交代码

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
