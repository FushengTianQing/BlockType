# Phase 3 开发计划：声明解析

> **创建日期：** 2026-04-16
> **目标：** 实现完整的 C++26 声明解析
> **预计时长：** 5 周

---

## 📋 当前状态

### 已实现（Phase 2 遗留）

| 功能 | 状态 | 文件 |
|------|------|------|
| 变量声明解析 | ✅ 完成 | ParseDecl.cpp |
| 函数声明解析 | ✅ 完成 | ParseDecl.cpp |
| 参数声明解析 | ✅ 完成 | ParseDecl.cpp |
| 类型解析 | ✅ 完成 | ParseType.cpp |
| 声明节点定义 | ✅ 完成 | Decl.h |

### Phase 3 已实现

| 功能 | 状态 | 文件 |
|------|------|------|
| 类声明解析 | ✅ 完成 | ParseDecl.cpp |
| 结构体声明解析 | ✅ 完成 | ParseDecl.cpp |
| 模板声明解析 | ✅ 完成 | ParseDecl.cpp |
| 命名空间声明解析 | ✅ 完成 | ParseDecl.cpp |
| 模块声明解析 | ✅ 完成 | ParseDecl.cpp |

---

## 🎯 详细任务

### 任务 1：类声明解析（优先级：🔴 极高）

**目标：** 完整解析类声明，包括成员、访问控制、继承等

**子任务：**
- [ ] `parseClassDeclaration()` - 类声明入口
- [ ] `parseClassSpecifier()` - 类说明符
- [ ] `parseClassMemberList()` - 成员列表
- [ ] `parseMemberDeclaration()` - 成员声明
- [ ] `parseAccessSpecifier()` - 访问控制
- [ ] `parseBaseClause()` - 基类列表
- [ ] `parseBaseSpecifier()` - 基类说明符

**语法规则：**
```cpp
class-specifier ::= 'class' identifier? '{' member-specification? '}'
member-specification ::= member-specification member-declaration
member-declaration ::= decl-specifier-seq? member-declarator-list? ';'
                     | function-definition
                     | using-declaration
                     | static_assert-declaration
                     | template-declaration
                     | deduction-guide
```

**测试用例：**
```cpp
// 简单类
class Point {
  int x, y;
public:
  Point(int x, int y) : x(x), y(y) {}
  int getX() const { return x; }
};

// 继承
class Derived : public Base {
private:
  int value;
public:
  void foo() override;
};

// 嵌套类
class Outer {
  class Inner {
    int data;
  };
};
```

---

### 任务 2：结构体声明解析（优先级：🔴 极高）

**目标：** 解析结构体声明（与类类似，默认访问控制不同）

**子任务：**
- [ ] `parseStructDeclaration()` - 结构体声明
- [ ] 复用类声明解析逻辑
- [ ] 处理默认 public 访问控制

**测试用例：**
```cpp
struct Point {
  int x, y;
  double distance() const;
};
```

---

### 任务 3：模板声明解析（优先级：🔴 高）

**目标：** 解析模板声明，包括类型参数、非类型参数、模板模板参数

**子任务：**
- [ ] `parseTemplateDeclaration()` - 模板声明入口
- [ ] `parseTemplateParameterList()` - 模板参数列表
- [ ] `parseTemplateParameter()` - 模板参数
- [ ] `parseTypeParameter()` - 类型参数
- [ ] `parseNonTypeParameter()` - 非类型参数
- [ ] `parseTemplateTemplateParameter()` - 模板模板参数
- [ ] `parseTemplateArgumentList()` - 模板实参列表
- [ ] `parseTemplateArgument()` - 模板实参
- [ ] `parseTemplateId()` - 模板标识符

**语法规则：**
```cpp
template-declaration ::= 'template' '<' template-parameter-list '>' declaration
template-parameter-list ::= template-parameter (',' template-parameter)*
template-parameter ::= type-parameter | parameter-declaration | template-template-parameter
type-parameter ::= 'class' identifier?
                 | 'class' identifier? '=' type-id
                 | 'typename' identifier?
                 | 'typename' identifier? '=' type-id
```

**测试用例：**
```cpp
// 类型参数
template<typename T>
class Vector { T* data; };

// 非类型参数
template<int N>
struct Array { int data[N]; };

// 模板模板参数
template<template<typename> class Container>
struct Wrapper { Container<int> c; };

// 变参模板
template<typename... Args>
struct Tuple {};

// 约束模板 (C++20)
template<typename T>
requires std::integral<T>
T add(T a, T b) { return a + b; }
```

---

### 任务 4：命名空间声明解析（优先级：🟡 中）

**目标：** 解析命名空间声明，包括嵌套命名空间和内联命名空间

**子任务：**
- [ ] `parseNamespaceDefinition()` - 命名空间定义
- [ ] `parseNamespaceBody()` - 命名空间体
- [ ] `parseNestedNamespace()` - 嵌套命名空间
- [ ] `parseInlineNamespace()` - 内联命名空间

**语法规则：**
```cpp
namespace-definition ::= 'namespace' identifier? '{' namespace-body '}'
                       | 'namespace' identifier '::' identifier '{' namespace-body '}'
inline-namespace-definition ::= 'inline' 'namespace' identifier '{' namespace-body '}'
```

**测试用例：**
```cpp
// 简单命名空间
namespace mylib {
  int value;
  void foo();
}

// 嵌套命名空间
namespace mylib::detail {
  int internal_value;
}

// 内联命名空间
namespace mylib {
  inline namespace v2 {
    int new_api();
  }
}
```

---

### 任务 5：模块声明解析（优先级：🟡 中）

**目标：** 解析 C++20 模块声明

**子任务：**
- [ ] `parseModuleDeclaration()` - 模块声明
- [ ] `parseModuleImport()` - 模块导入
- [ ] `parseModulePartition()` - 模块分区
- [ ] `parseModulePrivateFragment()` - 模块私有片段

**语法规则：**
```cpp
module-declaration ::= 'export'? 'module' module-name module-partition? attribute-specifier-seq? ';'
module-import ::= 'export'? 'import' module-name module-partition? ';'
module-name ::= identifier ('.' identifier)*
module-partition ::= ':' identifier
```

**测试用例：**
```cpp
// 模块声明
export module mylib;

// 模块导入
import std.core;
export import :submodule;

// 模块分区
module mylib:detail;

// 模块私有片段
module :private;
```

---

## 📊 进度跟踪

| 任务 | 状态 | 开始时间 | 完成时间 |
|------|------|----------|----------|
| 类声明解析 | ✅ 完成 | 2026-04-16 | 2026-04-16 |
| 结构体声明解析 | ✅ 完成 | 2026-04-16 | 2026-04-16 |
| 模板声明解析 | ✅ 完成 | 2026-04-16 | 2026-04-16 |
| 命名空间声明解析 | ✅ 完成 | 2026-04-16 | 2026-04-16 |
| 模块声明解析 | ✅ 完成 | 2026-04-16 | 2026-04-16 |

---

## 🧪 测试策略

### 单元测试

每个功能都需要完整的单元测试覆盖：

1. **正向测试**：正确解析各种声明
2. **错误恢复测试**：处理语法错误
3. **边界测试**：处理复杂嵌套情况

### 集成测试

使用完整 C++26 代码片段测试：

```cpp
// 复杂模板类
template<typename T>
requires std::copyable<T>
class Container {
public:
  Container() = default;
  Container(const Container&) = delete;
  Container& operator=(const Container&) = delete;
  
  template<typename U>
  Container(U&& value);
  
private:
  T* data_ = nullptr;
  std::size_t size_ = 0;
};

// 模块导出
export module mylib;
export template<typename T>
class Vector { /* ... */ };
```

---

## 📝 验收标准

- [x] 所有类声明正确解析
- [x] 所有结构体声明正确解析
- [x] 所有模板声明正确解析
- [x] 所有命名空间声明正确解析
- [x] 所有模块声明正确解析
- [x] 单元测试通过率 ≥ 95% (100%)
- [x] 无编译错误和警告
- [x] AST 节点信息完整

---

## 🚀 启动检查清单

在开始 Phase 3 之前，确保：

- [x] Phase 2 已完成
- [x] 测试通过率 100%
- [x] 无编译错误
- [x] 文档已更新
- [x] 代码已提交

---

*计划创建日期：2026-04-16*
*预计完成日期：2026-05-21*
