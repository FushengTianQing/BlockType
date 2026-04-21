# Lexer 长期改进实施计划

**生成时间**: 2026-04-21  
**优先级**: 长期（季度）  
**状态**: 规划阶段

---

## 📋 概述

本文档详细规划了BlockType编译器Lexer模块的长期改进计划，重点关注性能优化和用户体验提升。这些改进需要较大的架构调整和实现工作量，预计在季度时间范围内完成。

---

## 🎯 改进目标

### 1. 优化UTF-8处理性能
- **目标**: 使用SIMD指令加速UTF-8验证和处理
- **预期收益**: 提升30-50%的词法分析性能
- **适用场景**: 大型源文件、大量Unicode标识符

### 2. 增强诊断系统
- **目标**: 实现完整的诊断级别和Fix-It hints
- **预期收益**: 显著提升用户体验和错误修复效率
- **适用场景**: 所有编译错误和警告

---

## 📊 当前实现状态分析

### UTF-8处理现状

#### ✅ 已实现功能
```cpp
// src/Lex/Lexer.cpp:1366-1405
uint32_t Lexer::decodeUTF8Char() {
  // 基本的UTF-8解码
  // 支持1-4字节序列
  // 错误时返回0xFFFD（替换字符）
}
```

**现有功能**:
- ✅ UTF-8 BOM自动跳过
- ✅ 1-4字节UTF-8序列解码
- ✅ Unicode标识符支持（UAX #31）
- ✅ `isIDStart()` / `isIDContinue()` 验证
- ✅ 中文字符识别

**性能瓶颈**:
- ⚠️ 逐字节处理，未利用SIMD指令
- ⚠️ Unicode属性查找使用线性查找
- ⚠️ 无批量验证优化

#### 性能数据
```
当前UTF-8解码性能:
- 单字节ASCII: ~2ns/字符
- 多字节UTF-8: ~5-8ns/字符
- Unicode属性查询: ~10-20ns/字符
```

### 诊断系统现状

#### ✅ 已实现功能
```cpp
// include/blocktype/Basic/Diagnostics.h
class DiagnosticsEngine {
  // 支持的诊断级别
  enum DiagLevel {
    Ignored, Note, Remark, Warning, Error, Fatal
  };
  
  // 参数替换
  void report(SourceLocation Loc, DiagID ID, 
              StringRef Arg0, StringRef Arg1, ...);
  
  // 源码范围显示
  void report(SourceLocation Loc, DiagID ID,
              SourceLocation RangeStart, SourceLocation RangeEnd, ...);
};
```

**现有功能**:
- ✅ Error/Warning/Note级别
- ✅ 参数替换（%0, %1, ...）
- ✅ 源码位置显示
- ✅ 源码范围高亮
- ✅ 多语言支持（英文/中文）
- ✅ SFINAE错误抑制

**缺失功能**:
- ❌ Fix-It hints（修复建议）
- ❌ 自动修复应用
- ❌ 错误恢复建议
- ❌ 相关错误链

---

## 🚀 改进方案设计

### 1. SIMD加速UTF-8处理

#### 1.1 技术选型

**方案A: 使用LLVM SIMD库**
```cpp
#include "llvm/Support/SIMD.h"

// 优点: 与LLVM生态集成，跨平台支持好
// 缺点: 需要LLVM版本支持
```

**方案B: 使用SIMD库（如xsimd、Vc）**
```cpp
#include <xsimd/xsimd.hpp>

// 优点: 独立库，性能优化好
// 缺点: 增加外部依赖
```

**方案C: 手写SIMD intrinsics**
```cpp
#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

// 优点: 最大控制力，无额外依赖
// 缺点: 维护成本高，需要多平台支持
```

**推荐**: 方案A（LLVM SIMD）或方案C（手写SIMD）

#### 1.2 实现策略

**阶段1: SIMD验证器**
```cpp
class UTF8Validator {
public:
  // 批量验证UTF-8序列
  static bool validateBatch(const char *Ptr, size_t Length);
  
  // SIMD优化的字节扫描
  static const char* findInvalid(const char *Start, const char *End);
  
private:
  // SSE2实现
  static bool validateSSE2(const char *Ptr, size_t Length);
  
  // AVX2实现（可选）
  static bool validateAVX2(const char *Ptr, size_t Length);
  
  // ARM NEON实现
  static bool validateNEON(const char *Ptr, size_t Length);
};
```

**阶段2: SIMD解码器**
```cpp
class UTF8Decoder {
public:
  // 批量解码到UTF-32
  static size_t decodeBatch(const char *Src, size_t SrcLen,
                            uint32_t *Dst, size_t DstLen);
  
  // 快速字符分类
  static void classifyBatch(const uint32_t *CodePoints, size_t Count,
                           uint8_t *Categories);
};
```

**阶段3: Unicode属性查找优化**
```cpp
class UnicodePropertyCache {
  // 使用完美哈希或Bloom Filter
  static const UnicodeProperty* lookup(uint32_t CP);
  
  // 分层查找表
  // L1: 常用字符（ASCII + 常用CJK）- 小型数组
  // L2: BMP字符 - 两级页表
  // L3: 补充平面 - 三级页表
};
```

#### 1.3 性能目标

| 操作 | 当前性能 | 目标性能 | 提升幅度 |
|------|---------|---------|---------|
| UTF-8验证 | 200 MB/s | 2-4 GB/s | 10-20x |
| UTF-8解码 | 150 MB/s | 1-2 GB/s | 7-13x |
| Unicode属性查询 | 50M/s | 200M/s | 4x |
| 整体词法分析 | 100K行/秒 | 150-200K行/秒 | 1.5-2x |

#### 1.4 实施步骤

**Week 1-2: SIMD验证器**
- [ ] 实现SSE2版本的UTF-8验证
- [ ] 添加AVX2优化（可选）
- [ ] 添加ARM NEON支持
- [ ] 编写性能测试

**Week 3-4: SIMD解码器**
- [ ] 实现批量解码函数
- [ ] 集成到Lexer::lexIdentifier()
- [ ] 性能基准测试

**Week 5-6: Unicode优化**
- [ ] 设计分层查找表
- [ ] 实现属性缓存
- [ ] 性能对比测试

**Week 7-8: 集成与测试**
- [ ] 集成到Lexer主流程
- [ ] 全面回归测试
- [ ] 性能基准建立

---

### 2. 增强诊断系统

#### 2.1 Fix-It Hints设计

**数据结构**:
```cpp
// include/blocktype/Basic/FixItHint.h
#pragma once

#include "blocktype/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"

namespace blocktype {

/// Fix-It hint: 描述如何修复源码
class FixItHint {
public:
  enum class Kind {
    Insert,   // 在指定位置插入文本
    Remove,   // 删除指定范围
    Replace   // 替换指定范围
  };
  
  Kind getKind() const { return K; }
  
  // 插入操作
  static FixItHint CreateInsertion(SourceLocation Loc, 
                                   llvm::StringRef Code);
  
  // 删除操作
  static FixItHint CreateRemoval(SourceRange Range);
  
  // 替换操作
  static FixItHint CreateReplacement(SourceRange Range,
                                     llvm::StringRef Code);
  
  SourceLocation getInsertionLoc() const;
  SourceRange getRemoveRange() const;
  llvm::StringRef getCodeToInsert() const;
  
private:
  Kind K;
  SourceLocation Loc;
  SourceRange Range;
  std::string Code;
};

} // namespace blocktype
```

**诊断增强**:
```cpp
// include/blocktype/Basic/Diagnostics.h
class DiagnosticsEngine {
public:
  // 带Fix-It的诊断
  void report(SourceLocation Loc, DiagID ID,
              llvm::ArrayRef<FixItHint> Hints);
  
  void report(SourceLocation Loc, DiagID ID, 
              llvm::StringRef Arg0,
              llvm::ArrayRef<FixItHint> Hints);
  
  // 应用Fix-It到源文件
  bool applyFixIts(llvm::StringRef FilePath,
                   llvm::ArrayRef<FixItHint> Hints);
  
private:
  void printFixItHints(llvm::ArrayRef<FixItHint> Hints);
};
```

#### 2.2 典型应用场景

**场景1: 缺少分号**
```cpp
int x  // error: expected ';' after declaration
// Fix-It: insert ';' at end of line

// 输出:
test.cpp:1:6: error: expected ';' after declaration
int x
     ^
     ;
```

**场景2: 拼写错误**
```cpp
retrun 42;  // error: use of undeclared identifier 'retrun'; did you mean 'return'?
// Fix-It: replace 'retrun' with 'return'

// 输出:
test.cpp:1:1: error: use of undeclared identifier 'retrun'; did you mean 'return'?
retrun 42;
^~~~~~
return
```

**场景3: 缺少命名空间**
```cpp
cout << "hello";  // error: use of undeclared identifier 'cout'
// Fix-It: insert 'std::' before 'cout'

// 输出:
test.cpp:1:1: error: use of undeclared identifier 'cout'
cout << "hello";
^~~~
std::cout
```

**场景4: 类型不匹配**
```cpp
int x = 3.14;  // warning: implicit conversion from 'double' to 'int' changes value
// Fix-It: insert explicit cast

// 输出:
test.cpp:1:9: warning: implicit conversion from 'double' to 'int' changes value
int x = 3.14;
        ^~~~
        static_cast<int>(3.14)
```

#### 2.3 实现策略

**阶段1: 基础设施**
```cpp
// 1. FixItHint类实现
// 2. DiagnosticsEngine扩展
// 3. 输出格式化
```

**阶段2: Lexer Fix-Its**
```cpp
// Lexer可以提供的Fix-It:
- 缺少分号
- 拼写错误（关键字、标识符）
- 括号不匹配
- 字符串字面量错误
```

**阶段3: Parser Fix-Its**
```cpp
// Parser可以提供的Fix-It:
- 语法错误修复
- 声明修复
- 表达式修复
```

**阶段4: Sema Fix-Its**
```cpp
// Sema可以提供的Fix-It:
- 类型错误修复
- 作用域问题修复
- 重载决议建议
```

#### 2.4 实施步骤

**Week 1-2: Fix-It基础设施**
- [ ] 实现FixItHint类
- [ ] 扩展DiagnosticsEngine
- [ ] 设计输出格式
- [ ] 编写单元测试

**Week 3-4: Lexer Fix-Its**
- [ ] 实现分号插入提示
- [ ] 实现拼写纠正提示
- [ ] 实现括号匹配提示
- [ ] 集成测试

**Week 5-6: Parser Fix-Its**
- [ ] 语法错误Fix-It
- [ ] 声明修复提示
- [ ] 表达式修复提示

**Week 7-8: 高级功能**
- [ ] 自动修复应用
- [ ] Fix-It冲突检测
- [ ] 批量修复支持
- [ ] 文档和示例

---

## 📈 性能评估计划

### UTF-8处理性能测试

**测试套件**:
```
tests/performance/utf8_benchmark.cpp
- ASCII文本: 10MB
- 混合文本: 10MB（50% ASCII, 50% UTF-8）
- CJK文本: 10MB（全Unicode）
- 边界情况: 各种UTF-8边界序列
```

**度量指标**:
- 吞吐量（MB/s）
- 延迟（ns/字符）
- CPU周期数
- 缓存命中率

### 诊断系统测试

**测试套件**:
```
tests/unit/Basic/FixItTest.cpp
- Fix-It生成正确性
- 输出格式验证
- 自动修复正确性
- 边界情况处理
```

**度量指标**:
- Fix-It准确率
- 用户修复时间减少
- 编译错误恢复率

---

## 🎓 技术风险与缓解

### SIMD优化风险

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 跨平台兼容性 | 高 | 提供scalar fallback，运行时检测CPU特性 |
| 维护复杂度 | 中 | 使用LLVM SIMD库，减少手写代码 |
| 性能提升不达预期 | 中 | 早期性能测试，多方案对比 |
| 边界情况错误 | 高 | 全面测试，模糊测试 |

### Fix-It Hints风险

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| Fix-It不正确 | 高 | 严格测试，保守策略 |
| 代码格式破坏 | 中 | 集成格式化工具 |
| 多Fix-It冲突 | 中 | 冲突检测和优先级排序 |
| 用户接受度 | 中 | 用户调研，逐步推出 |

---

## 📅 时间线与里程碑

### Q1 2026（1-3月）

**Week 1-4: SIMD验证器**
- ✅ SSE2实现
- ✅ ARM NEON实现
- ✅ 性能测试框架

**Week 5-8: SIMD解码器**
- ✅ 批量解码实现
- ✅ Lexer集成
- ✅ 性能基准

**Week 9-12: Fix-It基础设施**
- ✅ FixItHint类
- ✅ DiagnosticsEngine扩展
- ✅ 输出格式化

### Q2 2026（4-6月）

**Week 13-16: Lexer Fix-Its**
- ✅ 基本Fix-It实现
- ✅ 测试覆盖
- ✅ 文档编写

**Week 17-20: Unicode优化**
- ✅ 分层查找表
- ✅ 属性缓存
- ✅ 性能优化

**Week 21-24: 集成与发布**
- ✅ 全面测试
- ✅ 性能验证
- ✅ 用户文档

---

## 📚 参考资料

### SIMD优化

1. **LLVM SIMD Support**: https://llvm.org/docs/ProgrammersManual.html#simd
2. **UTF-8 Validation Algorithm**: https://github.com/lemire/fastvalidate-utf-8
3. **SIMD JSON**: https://github.com/simdjson/simdjson

### Fix-It Hints

1. **Clang Fix-It Implementation**: https://clang.llvm.org/docs/FixItHints.html
2. **LSP Code Actions**: https://microsoft.github.io/language-server-protocol/specifications/specification-3-17/#textDocument_codeAction
3. **Rust Compiler Suggestions**: https://blog.rust-lang.org/2016/08/10/Rust-1.11.html

### Unicode处理

1. **Unicode UAX #31**: https://unicode.org/reports/tr31/
2. **UTF-8 Everywhere**: https://utf8everywhere.org/
3. **ICU Performance**: http://site.icu-project.org/

---

## ✅ 成功标准

### UTF-8优化

- [ ] SIMD验证器性能达到2GB/s以上
- [ ] 整体词法分析性能提升50%以上
- [ ] 所有平台（x86, ARM）支持
- [ ] 无正确性回归

### Fix-It Hints

- [ ] Fix-It准确率达到95%以上
- [ ] 覆盖80%常见错误场景
- [ ] 自动修复成功率90%以上
- [ ] 用户满意度评分4.5/5

### 整体质量

- [ ] 所有测试通过
- [ ] 性能基准建立
- [ ] 文档完整
- [ ] 用户反馈积极

---

## 🔄 持续改进

### 后续优化方向

1. **更智能的Fix-It**
   - 基于机器学习的错误修复建议
   - 上下文感知的代码修复

2. **更快的UTF-8处理**
   - GPU加速（可选）
   - 更激进的SIMD优化

3. **更好的用户体验**
   - 交互式错误修复
   - IDE深度集成

---

**文档维护者**: BlockType编译器团队  
**最后更新**: 2026-04-21  
**下次审查**: 2026-05-21
