# Unicode 支持设计方案

> **目标：** 为 BlockType 编译器提供轻量级、高效的 Unicode 支持
> **需求：** B2.2 NFC 规范化 + D3 UAX #31 完整实现

---

## 📋 需求分析

### B2.2 - NFC 规范化
- **用途：** 标识符比较、符号查找
- **场景：** 处理用户输入的中文标识符
- **要求：** 将不同规范化形式的标识符统一为 NFC 形式

### D3 - UAX #31 完整实现
- **用途：** 判断字符是否可以作为标识符
- **场景：** 词法分析器识别标识符
- **要求：**
  - `isIDStart(c)` - 字符是否可以作为标识符开头
  - `isIDContinue(c)` - 字符是否可以作为标识符后续字符
  - 支持所有 Unicode 字符（不只是基本多文种平面）

---

## 🎯 推荐方案：定制轻量级 Unicode 数据库

### 为什么不使用 ICU？

| 因素 | ICU4C | 定制方案 |
|------|-------|----------|
| 库体积 | ~30 MB | ~500 KB - 1 MB |
| 构建时间 | 长（需要配置） | 短（直接编译） |
| 依赖复杂度 | 高（多个库） | 低（无外部依赖） |
| 功能覆盖 | 100% | 仅编译器所需 |
| 性能 | 优秀 | 可优化到接近 |

### 核心思路

1. **从 Unicode 官方数据生成静态查找表**
   - 使用 Python 脚本解析 UCD（Unicode Character Database）
   - 生成 C++ 静态常量数组（编译时优化）
   - 使用完美哈希或紧凑数据结构

2. **仅实现必要功能**
   - NFC 规范化（不是完整的 NFKC/NFD）
   - UAX #31 属性查询（ID_Start/ID_Continue）
   - 简单的大小写转换（可选）

3. **优化存储和查询**
   - 使用范围压缩（run-length encoding）
   - 利用字符属性的连续性
   - 使用位图或布隆过滤器加速

---

## 🏗️ 架构设计

### 目录结构
```
src/Unicode/
├── include/blocktype/Unicode/
│   ├── UnicodeData.h        # 核心 API
│   ├── Normalization.h      # NFC 规范化
│   └── UAX31.h              # UAX #31 接口
├── lib/
│   ├── UnicodeData.cpp      # 生成的数据表
│   ├── Normalization.cpp    # 规范化实现
│   └── UAX31.cpp            # UAX #31 实现
└── scripts/
    ├── generate_unicode_data.py   # 数据生成脚本
    └── update_unicode.sh          # 更新 Unicode 版本
```

### API 设计

#### UnicodeData.h
```cpp
#pragma once

#include "blocktype/Basic/LLVM.h"
#include <cstdint>

namespace blocktype {
namespace unicode {

/// Unicode 字符属性
struct CharProps {
  uint16_t GeneralCategory;   // General Category (Lu, Ll, Lt, ...)
  uint8_t IDStart : 1;        // UAX #31: ID_Start
  uint8_t IDContinue : 1;     // UAX #31: ID_Continue
  uint8_t XIDStart : 1;       // UAX #31: XID_Start
  uint8_t XIDContinue : 1;    // UAX #31: XID_Continue
  uint8_t PatternSyntax : 1;  // Pattern_Syntax
  uint8_t PatternWhite : 1;   // Pattern_White_Space
  uint8_t Reserved : 2;
  
  // NFC 规范化相关
  uint8_t DecompType;         // 分解类型
  uint32_t DecompMapping;     // 分解映射索引
  uint8_t CombiningClass;     // 组合类
};

/// 查询字符属性
const CharProps& getCharProps(uint32_t CodePoint);

/// UAX #31 接口
bool isIDStart(uint32_t CodePoint);
bool isIDContinue(uint32_t CodePoint);
bool isXIDStart(uint32_t CodePoint);
bool isXIDContinue(uint32_t CodePoint);

/// NFC 规范化
StringRef normalizeNFC(StringRef Input, llvm::SmallVectorImpl<char> &Output);
uint32_t toNFC(uint32_t CodePoint);

} // namespace unicode
} // namespace blocktype
```

---

## 🔧 实现细节

### 1. 数据压缩策略

#### 范围压缩（Range Compression）
```cpp
// 示例：ID_Start 字符范围
struct IDStartRange {
  uint32_t Start;
  uint32_t End;
};

// 数据示例（实际从 UCD 生成）
static constexpr IDStartRange IDStartRanges[] = {
  {0x0041, 0x005A},  // A-Z
  {0x0061, 0x007A},  // a-z
  {0x00C0, 0x00D6},  // À-Ö
  {0x00D8, 0x00F6},  // Ø-ö
  {0x00F8, 0x01F5},  // ø-ǵ
  {0x01FA, 0x0217},  // Ǻ-ȥ
  // ... 更多范围
  {0x4E00, 0x9FFF},  // CJK 统一汉字
  {0x3400, 0x4DBF},  // CJK 扩展 A
  // ...
};

bool isIDStart(uint32_t CP) {
  // 二分查找
  auto It = std::lower_bound(IDStartRanges, CP, 
    [](const IDStartRange &R, uint32_t CP) {
      return R.End < CP;
    });
  return It != std::end(IDStartRanges) && CP >= It->Start;
}
```

#### 位图压缩（Bitmap）
```cpp
// 对于 BMP（Basic Multilingual Plane）使用位图
static constexpr uint64_t IDStartBitmap[1024] = { /* ... */ };

bool isIDStartBMP(uint32_t CP) {
  uint32_t Word = CP / 64;
  uint32_t Bit = CP % 64;
  return (IDStartBitmap[Word] >> Bit) & 1;
}
```

### 2. NFC 规范化算法

#### 简化实现（不依赖完整分解表）
```cpp
StringRef normalizeNFC(StringRef Input, SmallVectorImpl<char> &Output) {
  Output.clear();
  
  // 1. 快速路径：检查是否已经是 NFC
  bool NeedsNormalization = false;
  for (char C : Input) {
    uint8_t Byte = static_cast<uint8_t>(C);
    if (Byte >= 0x80) {  // 非 ASCII
      // 检查是否需要规范化
      uint32_t CP = decodeUTF8(Input);
      if (needsNFC(CP)) {
        NeedsNormalization = true;
        break;
      }
    }
  }
  
  if (!NeedsNormalization) {
    return Input;  // 已经是 NFC
  }
  
  // 2. 慢速路径：执行规范化
  SmallVector<uint32_t, 64> CodePoints;
  decodeUTF8(Input, CodePoints);
  
  // 3. 执行规范化（NFC = NFD + compose）
  SmallVector<uint32_t, 64> Normalized;
  decomposeNFD(CodePoints, Normalized);
  composeNFC(Normalized);
  
  // 4. 编码回 UTF-8
  encodeUTF8(Normalized, Output);
  return Output;
}
```

### 3. 数据生成脚本

#### Python 脚本示例
```python
#!/usr/bin/env python3
"""
从 Unicode Character Database 生成 C++ 数据表
"""

import urllib.request
import sys

# 下载 Unicode 数据
def download_ucd(version="15.1.0"):
    base_url = f"https://www.unicode.org/Public/{version}/ucd/"
    
    files = [
        "UnicodeData.txt",
        "DerivedCoreProperties.txt",
        "CompositionExclusions.txt"
    ]
    
    for f in files:
        urllib.request.urlretrieve(base_url + f, f)

# 解析 UnicodeData.txt
def parse_unicode_data():
    id_start_ranges = []
    id_continue_ranges = []
    
    with open("UnicodeData.txt") as f:
        for line in f:
            fields = line.split(';')
            codepoint = int(fields[0], 16)
            category = fields[2]
            
            # 判断 ID_Start/ID_Continue
            is_id_start = category.startswith('L') or category == 'Nl' or category == 'Nl'
            is_id_continue = is_id_start or category == 'Mn' or category == 'Mc' or category == 'Nd' or category == 'Pc'
            
            # 记录范围
            # ...
    
    return id_start_ranges, id_continue_ranges

# 生成 C++ 代码
def generate_cpp_code(id_start, id_continue):
    print("// Auto-generated from Unicode 15.1.0")
    print("// DO NOT EDIT")
    print()
    print("namespace blocktype {")
    print("namespace unicode {")
    print()
    
    # 生成 ID_Start 范围
    print("static constexpr IDStartRange IDStartRanges[] = {")
    for start, end in id_start:
        print(f"  {{0x{start:04X}, 0x{end:04X}}},")
    print("};")
    
    print()
    print("} // namespace unicode")
    print("} // namespace blocktype")

if __name__ == "__main__":
    download_ucd()
    id_start, id_continue = parse_unicode_data()
    generate_cpp_code(id_start, id_continue)
```

---

## 📦 集成方案

### CMake 配置
```cmake
# src/Unicode/CMakeLists.txt

# 添加 Unicode 数据生成步骤
find_package(Python3 REQUIRED)

add_custom_command(
  OUTPUT UnicodeData.cpp
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_unicode_data.py
  DEPENDS scripts/generate_unicode_data.py
  COMMENT "Generating Unicode data tables"
)

add_library(blocktype-unicode STATIC
  UnicodeData.cpp
  Normalization.cpp
  UAX31.cpp
)

target_include_directories(blocktype-unicode PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)
```

### 在 Lexer 中使用
```cpp
// src/Lex/Lexer.cpp
#include "blocktype/Unicode/UAX31.h"

bool Lexer::isIdentifierBody(uint32_t CP) {
  // 使用 UAX #31 完整实现
  return unicode::isIDContinue(CP);
}

bool Lexer::isIdentifierStart(uint32_t CP) {
  // 使用 UAX #31 完整实现
  return unicode::isIDStart(CP);
}
```

---

## 🎯 实施计划

### Phase 1: 基础架构（1-2 天）
1. 创建 `src/Unicode/` 目录结构
2. 实现 `UAX31.h/cpp` 基础框架
3. 编写数据生成脚本框架

### Phase 2: 数据生成（2-3 天）
1. 下载并解析 Unicode 15.1.0 数据
2. 生成 ID_Start/ID_Continue 范围表
3. 实现二分查找查询接口
4. 测试验证正确性

### Phase 3: NFC 规范化（3-4 天）
1. 实现 `Normalization.h/cpp`
2. 实现分解和组合算法
3. 生成必要的分解映射表
4. 性能优化和测试

### Phase 4: 集成和测试（1-2 天）
1. 在 Lexer 中集成 Unicode 支持
2. 添加单元测试
3. 性能基准测试
4. 文档更新

---

## 📊 性能优化建议

### 1. 多级查找表
```cpp
// L1: 快速路径（ASCII）
if (CP < 0x80) return isIDStartASCII(CP);

// L2: BMP（位图）
if (CP < 0x10000) return isIDStartBMP(CP);

// L3: 辅助平面（范围查找）
return isIDStartSupplementary(CP);
```

### 2. 缓存友好设计
```cpp
// 使用紧凑数据结构
struct CompactCharProps {
  uint16_t Data;  // 16 位编码所有属性
  
  bool isIDStart() const { return (Data >> 0) & 1; }
  bool isIDContinue() const { return (Data >> 1) & 1; }
  uint8_t getCombiningClass() const { return (Data >> 2) & 0x3F; }
};
```

### 3. SIMD 优化（可选）
```cpp
// 批量检查多个字符
void isIDContinueBatch(const uint32_t *CPs, bool *Results, size_t N);
```

---

## 🔗 参考资源

1. **Unicode 官方数据**
   - https://www.unicode.org/Public/15.1.0/ucd/
   - UnicodeData.txt, DerivedCoreProperties.txt

2. **UAX #31 规范**
   - https://www.unicode.org/reports/tr31/

3. **UAX #15 规范化**
   - https://www.unicode.org/reports/tr15/

4. **参考实现**
   - utf8proc: https://github.com/JuliaStrings/utf8proc
   - Rust unicode-bdd: https://github.com/unicode-rs/unicode-bdd

5. **优化技术**
   - Perfect Hashing: gperf
   - Range Compression: Unicode Technical Report

---

## 💡 总结

**推荐方案：定制轻量级 Unicode 数据库**

**理由：**
1. ✅ 库体积小（< 1 MB），适合编译器
2. ✅ 无外部依赖，易于构建和分发
3. ✅ 性能可控，可针对性优化
4. ✅ 功能精简，仅包含编译器所需
5. ✅ 可随 Unicode 版本升级

**工作量：**
- 约 1-2 周开发时间
- 数据生成脚本可复用
- 后续维护成本低

**替代方案：**
如果时间紧迫，可以先使用 **utf8proc** 作为过渡方案：
- 快速集成（1-2 天）
- 功能足够（支持 NFC + UAX #31）
- 后续可替换为定制方案
