# sizeof / alignof 表达式设计文档

## 概述

实现 `sizeof` 和 `alignof` 常量表达式的完整支持，覆盖 Lexer → Parser → AST → CodeGen 四个阶段。

## 动机

审计 Phase 6.1 IRGen 时发现 CodeGenConstant 缺少 sizeof/alignof 常量折叠能力。Clang 在 `ConstantEmitter::EmitConstant()` 中处理了 `UnaryExprOrTypeTraitExpr`（sizeof/alignof/offsetof 等），BlockType 之前完全没有此节点。

## 现有基础设施

| 层级 | 状态 | 位置 |
|------|------|------|
| Lexer Token | ✅ 已有 | `TokenKinds.def:224` (`kw_alignof`), `TokenKinds.def:374` (`kw_sizeof`) |
| OperatorPrecedence | ✅ 已有 | `OperatorPrecedence.cpp:137-138` — 标记为 `Unary` 优先级 |
| TargetInfo | ✅ 已有 | `getTypeSize(QualType)`, `getTypeAlign(QualType)` |
| AST 节点 | ❌ 缺失 | — |
| Parser 解析 | ❌ 缺失 | — |
| CodeGen 生成 | ❌ 缺失 | — |

## 设计方案

### 1. AST 层

#### 新增枚举 `UnaryExprOrTypeTrait`

```cpp
// include/blocktype/AST/Expr.h
enum class UnaryExprOrTypeTrait {
  SizeOf,    ///< sizeof
  AlignOf,   ///< alignof
};
```

#### 新增 AST 节点 `UnaryExprOrTypeTraitExpr`

```cpp
class UnaryExprOrTypeTraitExpr : public Expr {
  UnaryExprOrTypeTrait Kind;
  QualType ArgType;          // sizeof(type) 形式
  Expr *ArgExpr = nullptr;   // sizeof expr 形式
  bool IsArgumentType;       // true = sizeof(type), false = sizeof expr
};
```

**设计决策**：不使用 union 存储两个参数，因为 `QualType` 有非平凡的默认构造函数会导致 union 的默认构造函数被隐式删除。

**支持两种形式**：

| 形式 | 示例 | IsArgumentType |
|------|------|----------------|
| `sizeof(type)` | `sizeof(int)`, `sizeof(MyStruct)` | `true` |
| `sizeof expr` | `sizeof x`, `sizeof(x + y)` | `false` |

#### NodeKinds.def 注册

```
EXPR(UnaryExprOrTypeTraitExpr, Expr)
```

位于 `UnaryOperator` 之后、`ConditionalOperator` 之前。

### 2. Parser 层

#### 入口：parseUnaryExpression()

在 `parseUnaryExpression()` 中，new/delete 检查之后、prefix operator 检查之前，添加 sizeof/alignof 分支：

```cpp
if (Tok.is(TokenKind::kw_sizeof) || Tok.is(TokenKind::kw_alignof)) {
  return parseUnaryExprOrTypeTraitExpr();
}
```

#### 解析函数：parseUnaryExprOrTypeTraitExpr()

**歧义消解策略**：

`sizeof(...)` 可能是 `sizeof(type)` 也可能是 `sizeof(expr)`。采用启发式方法：

1. 消费 `sizeof`/`alignof` 关键字
2. 如果下一个 token 是 `(`：
   - 检查 `(` 之后的 token 是否看起来像类型关键字（`int`, `void`, `class`, `struct`, `enum`, `const`, `unsigned` 等）
   - 如果是 → 解析为 `sizeof(type)`：消费 `(`，调用 `parseType()`，消费 `)`
   - 如果不是 → 回退到表达式形式
3. 如果没有 `(` → 解析为 `sizeof expr`：调用 `parseUnaryExpression()`

**局限性**：当 `sizeof(identifier)` 中 identifier 恰好是类型名时，当前启发式不会识别。完整方案需要 Sema 协助（查询 identifier 是否在当前作用域中解析为类型）。这在 Phase 4 Sema 实现后可改进。

### 3. CodeGen 层

sizeof/alignof 在编译期即可完全求值，因此同时支持常量折叠和运行时生成。

#### 常量折叠（CodeGenConstant）

```cpp
case ASTNode::NodeKind::UnaryExprOrTypeTraitExprKind: {
  auto *SE = cast<UnaryExprOrTypeTraitExpr>(E);
  QualType ArgTy = SE->getTypeOfArgument();
  uint64_t Val = (SE->getTraitKind() == UnaryExprOrTypeTrait::SizeOf)
                 ? CGM.getTarget().getTypeSize(ArgTy)
                 : CGM.getTarget().getTypeAlign(ArgTy);
  return ConstantInt::get(Type::getInt64Ty(Ctx), Val);
}
```

#### 运行时生成（CodeGenFunction）

```cpp
llvm::Value *CodeGenFunction::EmitUnaryExprOrTypeTraitExpr(
    UnaryExprOrTypeTraitExpr *E) {
  QualType ArgTy = E->getTypeOfArgument();
  uint64_t Val = (E->getTraitKind() == UnaryExprOrTypeTrait::SizeOf)
                 ? CGM.getTarget().getTypeSize(ArgTy)
                 : CGM.getTarget().getTypeAlign(ArgTy);
  return ConstantInt::get(Type::getInt64Ty(Ctx), Val);
}
```

**返回类型**：固定为 `i64`。Clang 使用 `size_type`（通常为 `i64` 在 64 位平台），这与 Clang 行为一致。

### 4. getTypeOfArgument() 统一接口

```cpp
QualType getTypeOfArgument() const {
  if (IsArgumentType)
    return ArgType;                    // sizeof(type) → 直接返回类型
  return ArgExpr ? ArgExpr->getType()  // sizeof expr → 返回表达式类型
                 : QualType();
}
```

这简化了 CodeGen 的处理 — 无论哪种形式，只需调用 `getTypeOfArgument()` 获取目标类型，然后查询 `TargetInfo`。

## 修改文件清单

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `include/blocktype/AST/NodeKinds.def` | 新增 1 行 | `EXPR(UnaryExprOrTypeTraitExpr, Expr)` |
| `include/blocktype/AST/Expr.h` | 新增类 | `UnaryExprOrTypeTrait` 枚举 + `UnaryExprOrTypeTraitExpr` 类 |
| `src/AST/Expr.cpp` | 新增方法 | dump 实现 |
| `include/blocktype/Parse/Parser.h` | 新增声明 | `parseUnaryExprOrTypeTraitExpr()` |
| `src/Parse/ParseExpr.cpp` | 新增方法 + 修改 | sizeof/alignof 分支 + 解析实现 |
| `src/CodeGen/CodeGenConstant.cpp` | 新增 case + include | `EmitConstant()` 中 sizeof/alignof 常量折叠 |
| `include/blocktype/CodeGen/CodeGenFunction.h` | 新增声明 | `EmitUnaryExprOrTypeTraitExpr()` |
| `src/CodeGen/CodeGenFunction.cpp` | 新增 case | `EmitExpr()` 中 sizeof/alignof 分发 |
| `src/CodeGen/CodeGenExpr.cpp` | 新增方法 | `EmitUnaryExprOrTypeTraitExpr()` 运行时实现 |

## 未来扩展

### 短期（Phase 4 Sema）

- **类型名消歧**：通过 Sema 查询标识符是否为类型名，消除 `sizeof(x)` 的歧义
- **类型求值**：在 Sema 阶段计算 `sizeof`/`alignof` 的值并缓存到 AST 节点

### 中期

- **更多 trait**：支持 `offsetof`、`__builtin_object_size` 等
- **VLA 支持**：变长数组的 `sizeof` 不是编译期常量，需要运行时计算

### 已知局限

1. `sizeof(identifier)` 当 identifier 是类型名时，会被解析为表达式形式而非类型形式
2. 不支持 `sizeof...`（参数包大小查询）
3. 不支持 `offsetof` 宏/表达式
