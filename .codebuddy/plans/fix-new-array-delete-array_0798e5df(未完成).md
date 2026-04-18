---
name: fix-new-array-delete-array
overview: 修复 EmitCXXNewExpr 和 EmitCXXDeleteExpr 的 new[]/delete[] 支持：让 new T[n] 正确分配 sizeof(T)*n 字节并在前面存储数组长度，让 delete[] 读取数组长度并逆序调用析构函数。
todos:
  - id: add-get-destructor
    content: CGCXX 新增 GetDestructor(CXXRecordDecl*) 辅助方法声明与实现
    status: pending
  - id: fix-emit-new
    content: 重写 EmitCXXNewExpr：数组大小计算、cookie 存储、构造函数调用、初始化器处理
    status: pending
    dependencies:
      - add-get-destructor
  - id: fix-emit-delete
    content: 重写 EmitCXXDeleteExpr：null 检查、isArrayForm 分支、析构循环、cookie 读取
    status: pending
    dependencies:
      - add-get-destructor
  - id: build-test
    content: 编译并运行全量测试验证无回归
    status: pending
    dependencies:
      - fix-emit-new
      - fix-emit-delete
  - id: update-audit
    content: 更新审计文档 PHASE6-6.2-AUDIT.md 条目 6 状态
    status: pending
    dependencies:
      - build-test
---

## 产品概述

修复 BlockType 编译器中 `new[]`/`delete[]` 表达式的代码生成缺陷，使其正确处理数组分配、析构循环和构造函数调用。

## 核心问题

- `EmitCXXNewExpr` 完全忽略 `getArraySize()`，对 `new T[n]` 只分配 `sizeof(T)` 字节（应为 `sizeof(T) * n`），且不调用构造函数
- `EmitCXXDeleteExpr` 完全忽略 `isArrayForm()`，对 `delete[] ptr` 不执行析构循环，直接 free

## 核心功能

1. **数组 new 分配**：`new T[n]` 正确计算总大小 `sizeof(size_t) + sizeof(T)*n`，在数组前置 cookie 存储元素数量，返回偏移后的指针
2. **构造函数调用**：单元素 `new T(args)` 调用构造函数；数组 `new T[n]` 对每个元素调用默认构造函数或按初始化列表逐个初始化
3. **delete[] 析构循环**：从 cookie 读取元素数量，逆序遍历调用每个元素的析构函数，释放原始指针
4. **单元素 delete 析构**：`delete ptr` 在 free 前调用析构函数（如有）
5. **null 安全**：delete 空指针应为无操作

## 技术栈

- 语言：C++（LLVM IR 生成）
- 修改范围：仅 CodeGen 层（`CodeGenExpr.cpp` + `CGCXX.h/cpp`）
- AST 和 Parse 层无需修改（已正确）

## 实现方案

### 内存布局（数组 cookie 方案）

```
malloc 返回:  [count(size_t)][elem0][elem1]...[elemN-1]
              ^raw_ptr       ^user_ptr
new T[n] 返回 user_ptr = raw_ptr + sizeof(size_t)
delete[] ptr: raw_ptr = ptr - sizeof(size_t), count = *(size_t*)raw_ptr
```

这是 Itanium C++ ABI 数组 cookie 的简化版本。

### EmitCXXNewExpr 修改策略

1. 提取 ElementType（已有，从 T* 解引用得到 T）
2. 计算 sizeof(T)
3. **如果有 ArraySize**：

- 求值 ArraySize 得到运行时 count
- TotalSize = sizeof(size_t) + sizeof(T) * count
- malloc(TotalSize)
- 将 count 存入 raw_ptr[0]
- user_ptr = raw_ptr + sizeof(size_t)
- 如果 Initializer 是 InitListExpr → 逐个元素初始化
- 否则如果元素类型有析构函数 → 对每个元素调用默认构造函数
- 否则 → memset 零初始化（可选）

4. **如果无 ArraySize**：

- malloc(sizeof(T))
- 如果 Initializer 是 CXXConstructExpr → 调用构造函数
- 否则 → 零初始化

5. bitcast 到目标指针类型返回

### EmitCXXDeleteExpr 修改策略

1. 求值 Argument 得到指针
2. **null 检查**：if ptr == null, return（delete nullptr 是 no-op）
3. 从 Argument 类型提取 ElementType → CXXRecordDecl
4. **如果 isArrayForm()**：

- raw_ptr = ptr - sizeof(size_t)
- count = load(raw_ptr)
- 如果元素有析构函数：逆序循环 `for (i=count-1; i>=0; --i)` 调用 `dtor(ptr + i * elem_size)`
- free(raw_ptr)

5. **如果非数组**：

- 如果元素有析构函数：调用 `dtor(ptr)`
- free(ptr)

### 析构函数查找

复用 CGCXX.cpp 已有模式，新增 `GetDestructor(CXXRecordDecl*)` 辅助方法：

```cpp
CXXDestructorDecl *CGCXX::GetDestructor(CXXRecordDecl *RD) {
    if (!RD || !RD->hasDestructor()) return nullptr;
    for (CXXMethodDecl *MD : RD->methods()) {
        if (auto *Dtor = dyn_cast<CXXDestructorDecl>(MD))
            return Dtor;
    }
    return nullptr;
}
```

### 修改文件清单

```
project-root/
├── include/blocktype/CodeGen/CGCXX.h          # [MODIFY] 新增 GetDestructor() 声明
├── src/CodeGen/CGCXX.cpp                       # [MODIFY] 实现 GetDestructor() 辅助方法
├── src/CodeGen/CodeGenExpr.cpp                  # [MODIFY] 重写 EmitCXXNewExpr 和 EmitCXXDeleteExpr
└── docs/dev status/PHASE6-6.2-AUDIT.md         # [MODIFY] 更新条目 6 状态为已完成
```

## 实现注意事项

- **数组 cookie 对齐**：sizeof(size_t) = 8 字节在 64 位平台，元素大小已按其自然对齐，无需额外 padding
- **性能**：对于 POD 类型（无析构函数）的 delete[]，跳过析构循环直接 free
- **向后兼容**：非数组 `new T` 和 `delete ptr` 路径保持现有行为，只增加构造/析构调用
- **null 安全**：delete 和 delete[] 都需要 null 检查（delete nullptr 是合法的 no-op）
- **类型提取**：从 delete 的 Argument 表达式类型推断被删除类型（T* → T → CXXRecordDecl）