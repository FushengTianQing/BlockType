# 游离函数集成分析报告

**生成时间**: 2026-04-21 21:52  
**目的**: 分析已实现但未集成的函数，制定集成方案

---

## 📋 分析方法

对每个函数分析：
1. **功能描述** - 函数的作用
2. **实现状态** - 是否完整实现
3. **未集成原因** - 为什么 Parser 没有调用
4. **集成方案** - 应该在何处调用
5. **优先级** - 集成的紧急程度

---

## 🔍 函数分析

### 1. ActOnExplicitSpecialization

**功能**: 验证模板显式特化（`template<>`）

**实现状态**: ✅ 完整实现（验证逻辑）

**当前问题**:
- Parser 直接调用 `ActOnClassTemplateSpecDecl` 创建特化
- 跳过了验证步骤

**集成方案**:
```cpp
// ParseTemplate.cpp L95 之前
// 添加验证调用
if (PrimaryTemplate) {
  // 验证显式特化
  if (Actions.ActOnExplicitSpecialization(TemplateLoc, LAngleLoc, RAngleLoc).isInvalid()) {
    return nullptr;  // 验证失败，停止解析
  }
  
  // 然后创建特化
  auto *Spec = llvm::cast<ClassTemplateSpecializationDecl>(
      Actions.ActOnClassTemplateSpecDecl(...));
}
```

**优先级**: P1（模板系统核心功能）

---

### 2. ActOnExplicitInstantiation

**功能**: 处理模板显式实例化（`template class Foo<int>;`）

**实现状态**: ✅ 完整实现

**当前问题**:
- Parser 检测到 `template` 后没有 `<`，但没有调用此函数

**集成方案**:
```cpp
// ParseTemplate.cpp L50 附近
// Check for explicit instantiation: template class Vector<int>;
if (!Tok.is(TokenKind::less)) {
  // 解析声明
  Decl *D = parseDeclaration();
  if (D) {
    // 调用显式实例化处理
    return Actions.ActOnExplicitInstantiation(TemplateLoc, D).get();
  }
}
```

**优先级**: P2（高级功能）

---

### 3. ActOnTypeAliasTemplateDecl

**功能**: 处理别名模板（`template<class T> using Vec = std::vector<T>;`）

**实现状态**: ✅ 完整实现

**当前问题**:
- Parser 可能没有解析 `using` 在模板中的情况

**集成方案**:
```cpp
// ParseTemplate.cpp - 在解析模板声明时
if (Tok.is(TokenKind::kw_using)) {
  // 解析别名模板
  return parseTypeAliasTemplateDecl(TemplateLoc, TemplateParams);
}

// 新增函数
Decl *Parser::parseTypeAliasTemplateDecl(SourceLocation TemplateLoc,
                                          TemplateParameterList *Params) {
  // 解析 using Name = Type;
  // 调用 Actions.ActOnTypeAliasTemplateDecl(...)
}
```

**优先级**: P1（C++11 核心功能）

---

### 4. ActOnClassTemplatePartialSpecialization

**功能**: 处理类模板偏特化

**实现状态**: ✅ 完整实现

**当前问题**:
- Parser 有处理逻辑（L270），但可能没有调用此函数

**集成方案**:
检查 `ParseTemplate.cpp:270` 是否正确调用

**优先级**: P2（模板系统完整性）

---

### 5. ActOnVarTemplateDecl

**功能**: 处理变量模板（`template<class T> T pi = 3.14;`）

**实现状态**: ✅ 完整实现

**当前问题**:
- Parser 可能没有解析变量模板

**集成方案**:
```cpp
// ParseTemplate.cpp - 在解析模板声明时
if (Tok.is(TokenKind::kw_template)) {
  // 检查后面是否是变量声明
  // 如果是，调用 ActOnVarTemplateDecl
}
```

**优先级**: P2（C++14 功能）

---

### 6. ActOnVarTemplatePartialSpecialization

**功能**: 处理变量模板偏特化

**实现状态**: ✅ 完整实现

**当前问题**: 同上

**优先级**: P2

---

### 7. ActOnDeclarator

**功能**: 处理声明符

**实现状态**: ✅ 完整实现

**当前问题**:
- Parser 直接构建 AST，绕过了此函数

**集成方案**:
- 这是架构设计选择
- Parser 层直接构建更高效
- 可以保留作为辅助函数

**优先级**: P3（架构选择，非必需）

---

### 8. ActOnFunctionDeclFull

**功能**: 完整的函数声明处理

**实现状态**: ✅ 完整实现

**当前问题**:
- 已有 `ActOnFunctionDecl`，此函数可能是扩展版本

**集成方案**:
- 检查是否需要额外参数
- 如果需要，在某些场景使用此函数

**优先级**: P3（需确认是否必需）

---

### 9. ActOnAssumeAttr

**功能**: 处理 C++23 `[[assume]]` 属性

**实现状态**: ✅ 完整实现

**当前问题**:
- Parser 没有解析 `[[assume]]` 属性

**集成方案**:
```cpp
// ParseAttr.cpp 或 ParseDecl.cpp
if (AttrName == "assume") {
  // 解析 assume 属性
  return Actions.ActOnAssumeAttr(AttrLoc, AssumptionExpr);
}
```

**优先级**: P2（C++23 边缘功能）

---

## 📊 集成优先级

### P1（已完成）
1. ✅ `ActOnExplicitSpecialization` - 已添加验证步骤
2. ✅ `ActOnTypeAliasTemplateDecl` - 已添加验证和注册
3. ✅ `ActOnExplicitInstantiation` - 已集成显式实例化处理

### P2（已完成 ✅）
3. ✅ `ActOnExplicitInstantiation` - 添加显式实例化处理
4. ✅ `ActOnClassTemplatePartialSpecialization` - 已添加验证调用
5. ✅ `ActOnVarTemplateDecl` - 已添加变量模板验证
6. ✅ `ActOnVarTemplatePartialSpecialization` - 已添加验证调用
7. ✅ `ActOnAssumeAttr` - 已添加 [[assume]] 属性解析（C++23）

### P3（已完成 ✅）
7. ✅ `ActOnDeclarator` - 架构选择，保留作为辅助函数（功能已被具体函数覆盖）
8. ✅ `ActOnFunctionDeclFull` - 已集成，传入 inline/constexpr/consteval 标志

---

## 🎯 下一步行动

### 立即执行（P1）
1. 集成 `ActOnExplicitSpecialization` 验证逻辑
2. 添加别名模板解析支持

### 本周执行（P2）
3. 添加显式实例化处理
4. 验证偏特化集成
5. 添加变量模板支持

### 后续规划（P3）
6. 评估架构设计选择
7. 确认扩展函数用途

---

**报告生成时间**: 2026-04-21 21:52
