---
name: root-cause-fix-new-delete-type-system
overview: 根源性修复 BlockType 编译器 new/delete 表达式的类型系统缺陷：Sema 层接入、AST 节点规范化、类型唯一化、QualType::getAsString()、数组 cookie 抽象化。消除所有已识别的 P0-P4 风险。
todos:
  - id: type-as-string
    content: QualType 新增 getAsString() 方法（Type.h 声明 + Type.cpp 实现）
    status: completed
  - id: type-dedup
    content: ASTContext 类型去重：PointerType/LValueReferenceType/RValueReferenceType 加 DenseMap 缓存
    status: completed
  - id: ast-delete-expr
    content: CXXDeleteExpr 增加 AllocatedType 字段，更新 dump()
    status: completed
  - id: sema-new-delete
    content: Sema 新增 ActOnCXXNewExpr/ActOnCXXDeleteExpr/ProcessAST 方法
    status: completed
    dependencies:
      - ast-delete-expr
  - id: parse-integrate
    content: Parse 层 parseCXXDeleteExpression 传入 AllocatedType
    status: completed
    dependencies:
      - ast-delete-expr
  - id: codegen-sema-integrate
    content: CodeGenModule 集成 Sema::ProcessAST，在 CodeGen 前调用
    status: completed
    dependencies:
      - sema-new-delete
  - id: cookie-constant
    content: CGCXX.h 提取 ArrayCookie 常量，CodeGenExpr.cpp 替换硬编码
    status: completed
  - id: codegen-simplify
    content: 简化 EmitCXXNewExpr/EmitCXXDeleteExpr，使用 getType()/getAllocatedType() 和 ArrayCookie 常量
    status: completed
    dependencies:
      - codegen-sema-integrate
      - cookie-constant
  - id: build-test
    content: 编译并运行全量测试验证无回归
    status: completed
    dependencies:
      - codegen-simplify
  - id: update-audit
    content: 更新审计文档 PHASE6-6.2-AUDIT.md
    status: completed
    dependencies:
      - build-test
---

## 产品概述

对 BlockType 编译器的类型系统和 new/delete 表达式处理进行根源性重构，消除已识别的所有架构风险（P0-P4），使编译管线遵循标准的 Parser → Sema → CodeGen 三阶段架构。

## 核心问题

1. **P0**：Sema 不处理 new/delete 表达式，Parser 直接创建 AST 节点并绕过 Sema，导致 `CXXNewExpr::getType()` 为 null，缺少类型检查（抽象类、构造函数参数匹配、数组大小非负等）
2. **P1**：`ASTContext::getPointerType()`/`getReferenceType()` 不做去重（"no deduplication for simplicity"），同类型多实例导致 `isSameType` 指针比较误判
3. **P2**：`QualType` 缺少 `getAsString()` 方法，无法生成类型名的字符串表示，阻碍诊断信息和错误消息
4. **P3**：数组 cookie 偏移量 `sizeof(size_t)=8` 在 `EmitCXXNewExpr` 和 `EmitCXXDeleteExpr` 中各硬编码一次，修改一方必须同步另一方
5. **P4**：`CXXNewExpr::getType()` 为 null（被 `AllocatedType` 字段缓解但非正确架构），其他代码路径可能调用 `getType()` 期望得到 `T*`

## 核心功能

1. **Sema 集成**：新增 `Sema::ActOnCXXNewExpr()` 和 `Sema::ActOnCXXDeleteExpr()`，负责设置 `ExprTy`、类型检查、构造函数匹配验证
2. **类型去重**：为 `PointerType`、`LValueReferenceType`、`RValueReferenceType` 添加 `DenseMap` 缓存，确保同类型指针唯一化
3. **类型序列化**：在 `QualType` 上添加 `getAsString()` 方法，复用已有 `dump()` 基础设施
4. **Cookie 常量集中**：将数组 cookie 偏移量提取为共享常量，消除隐式耦合
5. **AST 完善**：`CXXDeleteExpr` 增加 `AllocatedType` 字段，使 delete 也能直接获取被删除类型而非依赖参数类型推导

## 技术栈

- 语言：C++17（LLVM IR 生成）
- 框架：LLVM/Clang 基础设施模式
- 修改范围：AST + Sema + Parse + CodeGen 四层

## 实现方案

### 整体策略

按依赖关系从底层到顶层重构：AST → ASTContext → Sema → Parse → CodeGen。

核心原则：**遵循 Clang 架构模式** — Parser 创建原始 AST 节点后交给 Sema，Sema 设置类型信息 (`ExprTy`) 并做语义检查，CodeGen 从 Sema 处理后的 AST 生成 IR。

### 架构变更

```
当前管线（有缺陷）:
  Parser::parseCXXNewExpression() → Context.create<CXXNewExpr>(...)
                                   → 直接返回 Expr*（ExprTy=null）
                                   → CodeGen 使用 AllocatedType hack

目标管线（根源性修复）:
  Parser::parseCXXNewExpression() → Context.create<CXXNewExpr>(..., Type)
                                   → Sema::ActOnCXXNewExpr()
                                     - 设置 ExprTy = PointerType(AllocatedType)
                                     - 检查 AllocatedType 是否完整
                                     - 检查数组大小是否非负常量
                                     - 检查类是否有匹配构造函数
                                   → CodeGen 使用 getType() 得到 T*
```

### 1. AST 层修改

**CXXDeleteExpr 增加 AllocatedType 字段**：

- 与 CXXNewExpr 对称，存储被删除的元素类型 T
- 消除 CodeGen 从 Argument 类型推导 ElementType 的脆弱逻辑

**CXXNewExpr 保留 AllocatedType**：

- 作为 `getAllocatedType()` 保留（语义明确：分配的类型 T）
- 同时 Sema 设置 `ExprTy = T*`，使得 `getType()` 返回正确的指针类型
- 两个字段各有用途：`getAllocatedType()` 返回 T，`getType()` 返回 T*

### 2. ASTContext 类型去重

为 `PointerType`、`LValueReferenceType`、`RValueReferenceType` 添加 `DenseMap<const Type*, Type*>` 缓存，与已有的 `RecordTypeCache`/`EnumTypeCache` 模式一致。

```cpp
// 新增缓存
llvm::DenseMap<const Type *, PointerType *> PointerTypeCache;
llvm::DenseMap<const Type *, LValueReferenceType *> LValueRefTypeCache;
llvm::DenseMap<const Type *, RValueReferenceType *> RValueRefTypeCache;
```

### 3. QualType::getAsString()

基于已有的 `QualType::dump(raw_ostream&)` 实现，添加字符串版本：

```cpp
// QualType 新增
std::string getAsString() const;
```

内部使用 `raw_string_ostream` + 现有 `dump()` 逻辑。

### 4. Sema 层新增 ActOnCXXNewExpr/ActOnCXXDeleteExpr

遵循已有的 `ActOnBinaryOperator`/`ActOnCastExpr` 等方法模式：

```cpp
ExprResult ActOnCXXNewExpr(CXXNewExpr *E);  // 设置 ExprTy=T*, 检查
ExprResult ActOnCXXDeleteExpr(CXXDeleteExpr *E);  // 设置 ExprTy=void, 检查
```

**ActOnCXXNewExpr 检查项**：

- AllocatedType 不能是 void/abstract/incomplete
- 如果有 ArraySize 且是常量，检查 > 0
- 如果有 Initializer 且是 CXXConstructExpr，查找匹配构造函数
- 设置 `ExprTy = PointerType(AllocatedType)`

**ActOnCXXDeleteExpr 检查项**：

- Argument 必须是指针类型
- 设置 `ExprTy = void`

### 5. Parse 层集成

**关键发现**：Parser 当前没有 Sema 引用。有两个方案：

- **方案 A**：Parser 持有 Sema 引用（需要修改 Parser 构造函数和所有调用点）
- **方案 B**：在 CodeGen 入口处对 AST 做一次 Sema 遍历（不修改 Parser）

选择 **方案 B**：在 `CodeGenModule::EmitTranslationUnit()` 中，在 CodeGen 之前调用 `Sema::ProcessAST(TU)`，遍历所有表达式节点对 CXXNewExpr/CXXDeleteExpr 调用对应的 ActOn 方法。这与当前编译管线兼容，不破坏 Parser 和已有的单元测试。

### 6. Cookie 常量集中

在 CGCXX.h 中定义常量：

```cpp
namespace ArrayCookie {
  constexpr uint64_t CookieSize = sizeof(size_t);  // 8 on 64-bit
}
```

`EmitCXXNewExpr` 和 `EmitCXXDeleteExpr` 都引用此常量。

### 7. CodeGen 层简化

有了 Sema 设置的 `ExprTy`，CodeGen 可以简化：

- `EmitCXXNewExpr`：从 `getType()` 得到 `T*`，`getAllocatedType()` 得到 T，无需 null fallback
- `EmitCXXDeleteExpr`：从 `getAllocatedType()` 得到 T，无需从参数类型推导

### 修改文件清单

```
project-root/
├── include/blocktype/AST/Type.h            # [MODIFY] QualType 新增 getAsString() 声明
├── src/AST/Type.cpp                        # [MODIFY] 实现 QualType::getAsString()
├── include/blocktype/AST/ASTContext.h      # [MODIFY] 新增 PointerTypeCache 等3个缓存
├── src/AST/ASTContext.cpp                  # [MODIFY] getPointerType/getLValueReferenceType/getRValueReferenceType 加去重
├── include/blocktype/AST/Expr.h            # [MODIFY] CXXDeleteExpr 增加 AllocatedType 字段
├── src/AST/Expr.cpp                        # [MODIFY] CXXDeleteExpr::dump() 更新
├── include/blocktype/Sema/Sema.h           # [MODIFY] 新增 ActOnCXXNewExpr/ActOnCXXDeleteExpr/ProcessAST
├── src/Sema/Sema.cpp                       # [MODIFY] 实现 ActOnCXXNewExpr/ActOnCXXDeleteExpr/ProcessAST
├── src/Parse/ParseExprCXX.cpp              # [MODIFY] parseCXXDeleteExpression 设置 AllocatedType
├── include/blocktype/CodeGen/CGCXX.h       # [MODIFY] 新增 ArrayCookie 常量命名空间
├── src/CodeGen/CodeGenExpr.cpp             # [MODIFY] EmitCXXNewExpr/EmitCXXDeleteExpr 使用常量和 AllocatedType
├── src/CodeGen/CodeGenModule.cpp           # [MODIFY] EmitTranslationUnit 中调用 Sema::ProcessAST
├── include/blocktype/CodeGen/CodeGenModule.h # [MODIFY] 新增 Sema 成员或参数
└── docs/dev status/PHASE6-6.2-AUDIT.md     # [MODIFY] 更新重构状态
```

## 实现注意事项

- **性能**：PointerType 去重用 `DenseMap<const Type*, Type*>` 查找 O(1)，key 就是被指向的类型指针，无需自定义 hash
- **向后兼容**：`ProcessAST` 只对 CXXNewExpr/CXXDeleteExpr 做处理，其他表达式路径完全不变
- **null 安全**：ActOnCXXDeleteExpr 不改变 delete 语义，仅在 ExprTy 上标记 void 类型
- **Canonical Type**：PointerType 去重后，`getCanonicalType()` 的指针比较自然变得正确——同一逻辑类型只会有一个 `Type*` 实例
- **测试**：所有 662 个现有测试必须通过，同时为 ActOnCXXNewExpr/ActOnCXXDeleteExpr 新增单元测试