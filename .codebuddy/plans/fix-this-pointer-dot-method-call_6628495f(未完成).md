---
name: fix-this-pointer-dot-method-call
overview: 修复 EmitCallExpr 中 obj.method() 调用时 this 指针类型错误的问题，将 !isArrow() 分支改用 EmitLValue(base) 获取对象地址
todos:
  - id: fix-this-ptr
    content: 修复 EmitCallExpr 中 obj.method() 的 this 指针获取逻辑
    status: pending
  - id: add-test
    content: 新增 obj.method() 调用的 lit 测试用例
    status: pending
    dependencies:
      - fix-this-ptr
  - id: build-verify
    content: 编译项目并运行全部测试验证无回归
    status: pending
    dependencies:
      - add-test
  - id: update-audit
    content: 更新 PHASE6-6.2-AUDIT.md 条目 4 状态为已修复
    status: pending
    dependencies:
      - build-verify
---

## 核心需求

修复 `EmitCallExpr` 中 `obj.method()` 调用时 this 指针类型错误：当前对非指针成员调用使用 `EmitExpr(base)` 返回结构体值，应改用 `EmitLValue(base)` 返回结构体指针地址。

## 修复范围

- 修改 `src/CodeGen/CodeGenExpr.cpp` 第 634-641 行 this 指针获取逻辑
- 新增测试用例覆盖 `obj.method()` 调用路径
- 更新审计文档条目状态

## 修复方案

### 问题根因

`EmitCallExpr` 第 635 行统一使用 `EmitExpr(base)` 获取 this 指针。对 `obj.method()`（`!isArrow()`），`EmitExpr` 对 DeclRefExpr(结构体变量) 执行 CreateLoad 返回结构体值（如 `{i32,i32}`），但函数签名的 this 参数类型是 `%struct*`，导致类型不匹配。

### 修复策略

采用与同文件 `EmitMemberExpr`（第 713-717 行）和 `EmitLValue`（第 1091-1095 行）完全一致的模式：

```cpp
llvm::Value *ThisPtr = nullptr;
if (MemberExpression->isArrow()) {
  ThisPtr = EmitExpr(MemberExpression->getBase());
} else {
  ThisPtr = EmitLValue(MemberExpression->getBase());
}
```

将第 635-641 行替换为上述逻辑，删除空的 `!isArrow()` 分支注释。

### 影响范围

仅修改 1 个代码文件的 3 行逻辑，风险极低。模式已在同文件 3 处验证正确。

## 目录结构

```
project-root/
├── src/CodeGen/CodeGenExpr.cpp              # [MODIFY] EmitCallExpr this 指针获取逻辑（第 634-641 行）
├── tests/lit/CodeGen/function-call.test     # [MODIFY] 新增 obj.method() 测试用例
└── docs/dev status/PHASE6-6.2-AUDIT.md      # [MODIFY] 更新条目 4 状态为已修复
```