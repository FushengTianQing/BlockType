# Phase 8：目标平台与优化
> **目标：** 支持目标平台（Linux x86_64、macOS ARM64），实现优化 Pass
> **前置依赖：** Phase 0-7 完成
> **验收标准：** 能在目标平台上正确编译运行

---

## 📌 阶段总览

```
Phase 8 包含 3 个 Stage，共 8 个 Task，预计 4 周完成。
依赖链：Stage 8.1 → Stage 8.2 → Stage 8.3
```

| Stage | 名称 | 核心交付物 | 建议时长 |
|-------|------|-----------|----------|
| **Stage 8.1** | 目标平台配置 | TargetInfo、ABI 支持 | 1.5 周 |
| **Stage 8.2** | 平台特定代码 | 内建函数、调用约定 | 1.5 周 |
| **Stage 8.3** | 优化 Pass | 内联、常量传播、死代码消除 | 1 周 |

---

## Stage 8.1 — 目标平台配置

### Task 8.1.1 TargetInfo 类

**目标：** 实现目标平台信息管理

**开发要点：**

- **E8.1.1.1** 创建 `include/zetacc/Basic/TargetInfo.h`：
  ```cpp
  #pragma once
  
  #include "llvm/Target/TargetMachine.h"
  #include <string>
  
  namespace zetacc {
  
  class TargetInfo {
    std::string Triple;           // 目标三元组
    std::string CPU;              // 目标 CPU
    std::string ABI;              // 目标 ABI
    
    unsigned PointerWidth;        // 指针宽度
    unsigned LongWidth;           // long 宽度
    unsigned LongLongWidth;       // long long 宽度
    unsigned FloatWidth;          // float 宽度
    unsigned DoubleWidth;         // double 宽度
    unsigned LongDoubleWidth;     // long double 宽度
    
  public:
    TargetInfo(StringRef Triple);
    
    /// 创建目标信息
    static std::unique_ptr<TargetInfo> CreateTargetInfo(StringRef Triple);
    
    /// 获取目标三元组
    StringRef getTriple() const { return Triple; }
    
    /// 获取指针宽度
    unsigned getPointerWidth() const { return PointerWidth; }
    
    /// 获取类型大小
    unsigned getTypeWidth(QualType T) const;
    
    /// 获取类型对齐
    unsigned getTypeAlign(QualType T) const;
    
    /// 判断是否是 64 位
    bool is64Bit() const { return PointerWidth == 64; }
    
    /// 判断是否是 Linux
    bool isLinux() const;
    
    /// 判断是否是 macOS
    bool isMacOS() const;
    
    /// 获取 LLVM TargetMachine
    llvm::TargetMachine* createTargetMachine() const;
  };
  
  } // namespace zetacc
  ```

- **E8.1.1.2** 实现 Linux x86_64 和 macOS ARM64 平台配置

**开发关键点提示：**
> 请为 BlockType 实现目标平台信息管理。
>
> **目标平台**：
> - Linux x86_64：x86_64-pc-linux-gnu
> - macOS ARM64：arm64-apple-darwin
>
> **平台差异**：
> - 指针宽度：都是 64 位
> - long 宽度：Linux 64 位，macOS 64 位
> - long double：Linux x87 扩展精度，macOS IEEE 双精度
> - 调用约定：Linux System V AMD64 ABI，macOS ARM64 AAPCS
>
> **类型大小**：
> - int：32 位
> - long：64 位（Linux/macOS）
> - long long：64 位
> - float：32 位
> - double：64 位
> - 指针：64 位

**Checkpoint：** TargetInfo 正确识别平台；类型大小正确

---

### Task 8.1.2 ABI 支持

**目标：** 实现平台 ABI

**开发要点：**

- **E8.1.2.1** 实现 Itanium C++ ABI（Linux）
- **E8.1.2.2** 实现 ARM64 ABI（macOS）

**开发关键点提示：**
> 请为 BlockType 实现平台 ABI 支持。
>
> **Itanium C++ ABI**：
> - 名称修饰（Name Mangling）
> - 虚函数表布局
> - RTTI 布局
> - 异常处理
>
> **调用约定**：
> - Linux x86_64：System V AMD64 ABI
>   - 整数/指针参数：rdi, rsi, rdx, rcx, r8, r9
>   - 浮点参数：xmm0-xmm7
>   - 返回值：rax, rdx, xmm0, xmm1
> - macOS ARM64：AAPCS64
>   - 整数/指针参数：x0-x7
>   - 浮点参数：v0-v7
>   - 返回值：x0, v0

**Checkpoint：** ABI 正确；能与其他编译器互操作

---

### Task 8.1.3 名称修饰

**目标：** 实现 C++ 名称修饰

**开发要点：**

- **E8.1.3.1** 创建 `include/zetacc/CodeGen/Mangler.h`：
  ```cpp
  class Mangler {
    TargetInfo &Target;
    
  public:
    Mangler(TargetInfo &T) : Target(T) {}
    
    /// 修饰名称
    std::string mangleName(NamedDecl *ND);
    
    /// 修饰类型
    std::string mangleType(QualType T);
    
    /// 修饰函数
    std::string mangleFunction(FunctionDecl *FD);
  };
  ```

- **E8.1.3.2** 实现 Itanium ABI 名称修饰规则

**Checkpoint：** 名称修饰正确；能链接 C++ 库

---

## Stage 8.2 — 平台特定代码

### Task 8.2.1 内建函数

**目标：** 实现平台内建函数

**开发要点：**

- **E8.2.1.1** 实现 GCC/Clang 兼容的内建函数：
  - __builtin_abs, __builtin_strlen
  - __builtin_expect, __builtin_unreachable
  - __builtin_ctz, __builtin_clz
  - __builtin_popcount

- **E8.2.1.2** 实现平台特定内建：
  - x86：__builtin_ia32_*
  - ARM：__builtin_arm_*

**Checkpoint：** 内建函数正确

---

### Task 8.2.2 调用约定

**目标：** 实现调用约定代码生成

**开发要点：**

- **E8.2.2.1** 实现参数传递规则
- **E8.2.2.2** 实现返回值处理

**Checkpoint：** 调用约定正确

---

## Stage 8.3 — 优化 Pass

### Task 8.3.1 基本优化

**目标：** 实现基本优化 Pass

**开发要点：**

- **E8.3.1.1** 集成 LLVM 优化 Pass
- **E8.3.1.2** 配置优化级别（-O0, -O1, -O2, -O3）

**开发关键点提示：**
> 请为 BlockType 实现优化 Pass。
>
> **优化级别**：
> - O0：无优化
> - O1：基本优化（常量传播、死代码消除）
> - O2：标准优化（内联、循环优化）
> - O3：激进优化（向量化、函数特殊化）
>
> **常用优化 Pass**：
> - 常量传播（Constant Propagation）
> - 死代码消除（Dead Code Elimination）
> - 内联（Inlining）
> - 循环展开（Loop Unrolling）
> - 公共子表达式消除（CSE）

**Checkpoint：** 优化 Pass 正确

---

### Task 8.3.2 目标特定优化

**目标：** 实现目标特定优化

**开发要点：**

- **E8.3.2.1** 实现 x86_64 优化
- **E8.3.2.2** 实现 ARM64 优化

**Checkpoint：** 目标特定优化正确

---

## 📋 Phase 8 验收检查清单

```
[ ] TargetInfo 实现完成
[ ] ABI 支持实现完成
[ ] 名称修饰实现完成
[ ] 内建函数实现完成
[ ] 调用约定实现完成
[ ] 基本优化 Pass 实现完成
[ ] 目标特定优化实现完成
[ ] Linux x86_64 平台测试通过
[ ] macOS ARM64 平台测试通过
```

---

*Phase 8 完成标志：支持 Linux x86_64 和 macOS ARM64 目标平台；优化 Pass 正确工作；能生成可执行文件。*
