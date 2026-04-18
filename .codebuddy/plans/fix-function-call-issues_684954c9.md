---
name: fix-function-call-issues
overview: 彻底修复审计条目 5/6/7：调用属性设置、隐式参数转换（含变参提升）、变参函数支持
todos:
  - id: ast-funcdecl
    content: FunctionDecl 增加 Attrs 字段、isVariadic/hasAttr/getAttrs/setAttrs 方法
    status: completed
  - id: parse-declspec
    content: DeclSpec 增加 AttrList 字段，parseDeclSpecifierSeq 解析 [[ 属性
    status: completed
    dependencies:
      - ast-funcdecl
  - id: parse-funcdecl
    content: parseDeclaration 传递属性到 FunctionDecl 构造函数
    status: completed
    dependencies:
      - parse-declspec
  - id: codegen-noreturn
    content: GetOrCreateFunctionDecl 检查 [[noreturn]] 设置 Fn->setDoesNotReturn()
    status: completed
    dependencies:
      - ast-funcdecl
  - id: codegen-variadic
    content: EmitCallExpr 增加变参 default argument promotion 和调用点 noreturn 属性
    status: completed
    dependencies:
      - ast-funcdecl
      - codegen-noreturn
  - id: build-test
    content: 编译并运行全量测试验证无回归
    status: completed
    dependencies:
      - codegen-variadic
  - id: update-audit
    content: 更新审计文档条目 5/6/7 状态
    status: completed
    dependencies:
      - build-test
---

## 核心需求

彻底修复 PHASE6-6.2-AUDIT.md 中三个函数调用相关的遗留问题：

### 问题 5: 未设置调用属性

- `GetOrCreateFunctionDecl` 已设置函数级 AlwaysInline/DoesNotThrow，但未处理 `[[noreturn]]`
- 调用点 `CreateCall`/`CreateInvoke` 未设置调用属性
- 需要：解析 `[[noreturn]]` → 设置函数属性 → 设置调用点属性

### 问题 6: 隐式参数转换（变参部分未覆盖）

- 非虚函数命名参数已有 `EmitScalarConversion`（条目 6 已部分修复）
- 虚函数路径也有 `EmitScalarConversion`
- **唯一未覆盖场景**：变参函数的可变参数部分（`I >= Params.size()`）缺少 default argument promotion

### 问题 7: 未处理变参函数

- `FunctionType::isVariadic()` 已存在，但 `FunctionDecl` 无便捷访问方法
- 变参调用时，可变参数部分无 default argument promotion（float→double, short→int 等）
- 变参函数的 LLVM 函数类型已通过 `GetFunctionABI` 正确传递 `isVariadic`，但 CodeGen 层未处理可变参数部分

### 预期效果

- `[[noreturn]] void f()` 正确解析并设置函数和调用点的 NoReturn 属性
- `printf("%d %f", short_val, float_val)` 的变参部分正确提升：short→int, float→double
- `void f(int, ...)` 的 `isVariadic()` 可从 FunctionDecl 便捷查询

## Tech Stack

项目为 C++ 编译器 (BlockType)，使用 LLVM 18 作为后端。修改涉及 AST、Parse、Sema、CodeGen 四层。

## Implementation Approach

### 核心策略

**三层联动修复**：AST 层增加属性存储和查询能力 → Parse 层连接已有的属性解析器 → CodeGen 层应用属性和默认参数提升。

### 修改范围

#### 文件 1: `include/blocktype/AST/Decl.h` — FunctionDecl 扩展

FunctionDef 缺少两个能力：

1. 无 `isVariadic()` 便捷方法（需委托到 FunctionType）
2. 无属性存储（`AttributeListDecl*`）

修改：

- 添加 `AttributeListDecl *Attrs = nullptr` 成员
- 添加方法：`isVariadic()`、`getAttrs()`、`setAttrs()`、`hasAttr(StringRef)`
- 修改构造函数接受 AttributeListDecl 参数

#### 文件 2: `include/blocktype/Parse/DeclSpec.h` — DeclSpec 扩展

当前 `parseDeclSpecifierSeq` 无法传递解析出的属性到声明创建点。

修改：

- 添加 `AttributeListDecl *AttrList = nullptr` 到 DeclSpec

#### 文件 3: `src/Parse/ParseDeclSpec.cpp` — 解析属性

`parseDeclSpecifierSeq` 不处理 `[[` 属性说明符。需要在类型说明符之前处理 `[[noreturn]]` 等属性。

修改：

- 在 `parseDeclSpecifierSeq` 循环开头增加 `[[` 检测，调用已有的 `parseAttributeSpecifier`

#### 文件 4: `src/Parse/ParseDecl.cpp` — 传递属性到 FunctionDecl

函数声明创建点（第 1656 行）不传递属性。

修改：

- 在 `parseDeclaration` 的通用声明路径中，将 DeclSpec.AttrList 传递给 FunctionDecl
- 同理在类成员函数解析中传递属性

#### 文件 5: `src/CodeGen/CodeGenModule.cpp` — 设置 noreturn 函数属性

`GetOrCreateFunctionDecl` 在第 523-529 行设置函数属性，但未处理 `[[noreturn]]`。

修改：

- 在已有属性设置区域，检查 FunctionDecl 的 `hasAttr("noreturn")` 并调用 `Fn->setDoesNotReturn()`

#### 文件 6: `src/CodeGen/CodeGenExpr.cpp` — 变参默认提升 + 调用点属性

`EmitCallExpr` 有两处参数循环（虚函数 783-796 行、非虚 817-827 行），可变参数部分（`I >= Params.size()`）缺少 default argument promotion。

修改：

- **Default Argument Promotion**: 当 `I >= Params.size()` 且函数是 variadic 时，应用提升规则：
- float → double
- 比int窄的整数类型 → int
- **调用点 noreturn**: 在 CreateCall/CreateInvoke 后，如果 CalleeFunction 有 `doesNotReturn()` 属性，设置 CallBase 的 `setDoesNotReturn()`

#### 文件 7: `docs/dev status/PHASE6-6.2-AUDIT.md` — 更新条目

## Directory Structure

```
project-root/
├── include/blocktype/AST/Decl.h          # [MODIFY] FunctionDecl 增加 Attrs 字段和 isVariadic/hasAttr 方法
├── include/blocktype/Parse/DeclSpec.h    # [MODIFY] DeclSpec 增加 AttrList 字段
├── src/Parse/ParseDeclSpec.cpp           # [MODIFY] parseDeclSpecifierSeq 增加 [[ 属性解析
├── src/Parse/ParseDecl.cpp               # [MODIFY] 函数声明创建时传递属性
├── src/CodeGen/CodeGenModule.cpp         # [MODIFY] GetOrCreateFunctionDecl 检查 noreturn 属性
├── src/CodeGen/CodeGenExpr.cpp           # [MODIFY] EmitCallExpr 增加变参默认提升和调用点属性
└── docs/dev status/PHASE6-6.2-AUDIT.md   # [MODIFY] 更新条目 5/6/7 状态
```

## Key Code Structures

### FunctionDecl 新增方法签名

```cpp
class FunctionDecl : public ValueDecl {
  // 新增成员
  AttributeListDecl *Attrs = nullptr;
  
public:
  // 便捷方法
  bool isVariadic() const;           // 委托到 FunctionType::isVariadic()
  AttributeListDecl *getAttrs() const;
  void setAttrs(AttributeListDecl *A);
  bool hasAttr(llvm::StringRef Name) const;  // 遍历 Attrs 查找
};
```

### Default Argument Promotion 逻辑（CodeGen 辅助函数）

```cpp
// 在 CodeGenFunction 中添加辅助方法
llvm::Value *emitDefaultArgPromotion(llvm::Value *Arg, QualType ArgType);
// float → double (FPExt)
// < 32-bit integer → int (IntCast to Int32Ty)
// bool → int
// 其他类型保持不变
```