# Task A-F6 优化版：StructuredDiagnostic 基础结构定义

> 文档版本：v1.0 | 由 planner 产出

---

## 一、任务概述

| 项目 | 内容 |
|------|------|
| 任务编号 | A-F6 |
| 任务名称 | StructuredDiagnostic 基础结构定义 |
| 依赖 | 无（独立新增文件） |
| 对现有代码的影响 | 仅 `src/IR/CMakeLists.txt` 和 `tests/unit/IR/CMakeLists.txt` 需追加条目 |

**产出文件**

| 操作 | 文件路径 |
|------|----------|
| 新增 | `include/blocktype/IR/IRDiagnostic.h` |
| 新增 | `src/IR/IRDiagnostic.cpp` |
| 新增 | `tests/unit/IR/IRDiagnosticTest.cpp` |
| 修改 | `src/IR/CMakeLists.txt`（+1 行） |
| 修改 | `tests/unit/IR/CMakeLists.txt`（+2 行） |

---

## 二、代码背景分析 — 6 个关键设计决策

### 决策 1：`ir::IRErrorCode` 不存在 → 定义 `diag::DiagnosticCode`

**现状**：项目中不存在 `ir::IRErrorCode`。A-F10 专门负责该枚举。

**参考模式**：
- `SerializationErrorKind`（`IRSerializer.h:21`）：`enum class : uint8_t`
- `VerificationCategory`（`IRVerifier.h:21`）：`enum class : uint8_t`

**决策**：定义 `diag::DiagnosticCode : uint32_t`，编码规则 `0xGGNN`（GG=组号+1, NN=组内序号）。

理由：
1. 不越界定义 `ir` 命名空间的类型（A-F10 职责）
2. 诊断系统拥有独立错误码空间，符合"接口抽象优先"
3. A-F10 完成后可通过映射函数 `irToDiagCode()` 衔接
4. 0xGGNN 编码可快速反查 DiagnosticGroup

### 决策 2：SourceLocation → 使用 `ir::SourceLocation`

| 类型 | 结构 | 适用场景 |
|------|------|----------|
| `Basic::SourceLocation` | 64-bit 编码（FileID+Offset） | 前端词法分析，需 SourceManager 解码 |
| `ir::SourceLocation` | `Filename+Line+Column` | IR 调试信息，人类可读 |

**决策**：使用 `ir::SourceLocation`（`IRDebugMetadata.h:16`）。

理由：诊断信息面向人类（`toText()`/`toJSON()`），需直接可用的文件名/行号/列号。`IRInstructionDebugInfo`（`IRDebugInfo.h:46`）也使用此类型。

### 决策 3：DenseMap Key → 使用 `uint16_t`

**现状**：`ir::DenseMap` 使用 `std::hash<KeyT>()(K)`。C++23 不保证 `std::hash` 对 scoped enum 的特化。

**决策**：内部使用 `ir::DenseMap<uint16_t, bool>`，通过 `static_cast<uint16_t>(G)` 转换。

理由：零可移植性风险，实现细节隐藏在 `.cpp` 中。

### 决策 4：SmallVector 命名空间

`SmallVector` 在 `blocktype::ir` 命名空间。`diag` 是平级命名空间，使用完整限定 `ir::SmallVector<std::string, 4>`。通过 `#include "blocktype/IR/ADT.h"` 引入。

### 决策 5：命名空间 → `blocktype::diag`

spec 指定 `blocktype::diag`。文件放在 `IR/` 目录是物理组织便利（`blocktype-ir` target），不影响逻辑命名空间。先例：`ir::debug`（`IRDebugInfo.h`）也在 `IR/` 目录下使用独立子命名空间。

### 决策 6：字段访问 → `struct` + public 字段 + getter

**参考**：`VerificationDiagnostic`/`SerializationDiagnostic` 都是 struct + public 字段。

**决策**：`struct StructuredDiagnostic`，public 字段 + getter 方法。支持 spec 中 `D.Level = ...` 直接赋值，同时保留 getter 便于未来渐进改为 private。

---

## 三、完整头文件 — `include/blocktype/IR/IRDiagnostic.h`

```cpp
#ifndef BLOCKTYPE_IR_IRDIAGNOSTIC_H
#define BLOCKTYPE_IR_IRDIAGNOSTIC_H

#include <cstdint>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRDebugMetadata.h"

namespace blocktype {
namespace diag {

// ============================================================
// DiagnosticLevel — 诊断严重级别
// ============================================================

enum class DiagnosticLevel : uint8_t {
  Note    = 0,
  Remark  = 1,
  Warning = 2,
  Error   = 3,
  Fatal   = 4,
};

const char* getDiagnosticLevelName(DiagnosticLevel L);

// ============================================================
// DiagnosticGroup — 诊断分组
// ============================================================

enum class DiagnosticGroup : uint16_t {
  TypeMapping            = 0,
  InstructionValidation = 1,
  IRVerification         = 2,
  BackendCodegen         = 3,
  FFIBinding             = 4,
  Serialization          = 5,
};

const char* getDiagnosticGroupName(DiagnosticGroup G);

// ============================================================
// DiagnosticCode — 诊断错误码
// ============================================================

/// 编码规则：0xGGNN — GG=DiagnosticGroup值+1, NN=组内序号
enum class DiagnosticCode : uint32_t {
  Unknown              = 0x0000,

  // TypeMapping (0x01xx)
  TypeMappingFailed    = 0x0100,
  TypeMappingAmbiguous = 0x0101,

  // InstructionValidation (0x02xx)
  InvalidInstruction   = 0x0200,
  InvalidOperand       = 0x0201,

  // IRVerification (0x03xx)
  VerificationFailed   = 0x0300,
  InvalidModule        = 0x0301,

  // BackendCodegen (0x04xx)
  CodegenFailed        = 0x0400,
  UnsupportedType      = 0x0401,

  // FFIBinding (0x05xx)
  FFIBindingFailed     = 0x0500,
  InvalidFFISignature  = 0x0501,

  // Serialization (0x06xx)
  SerializationFailed    = 0x0600,
  DeserializationFailed  = 0x0601,
};

const char* getDiagnosticCodeName(DiagnosticCode C);
DiagnosticGroup getGroupForCode(DiagnosticCode C);

// ============================================================
// StructuredDiagnostic — 单条结构化诊断
// ============================================================

struct StructuredDiagnostic {
  DiagnosticLevel Level = DiagnosticLevel::Note;
  DiagnosticGroup Group = DiagnosticGroup::TypeMapping;
  DiagnosticCode  Code  = DiagnosticCode::Unknown;
  std::string Message;
  ir::SourceLocation Loc;
  ir::SmallVector<std::string, 4> Notes;

  DiagnosticLevel getLevel() const { return Level; }
  DiagnosticGroup getGroup() const { return Group; }
  DiagnosticCode  getCode()  const { return Code;  }
  ir::StringRef getMessage() const { return Message; }
  const ir::SourceLocation& getLocation() const { return Loc; }

  void addNote(ir::StringRef N) { Notes.push_back(N.str()); }

  void setLocation(ir::StringRef Filename, unsigned Line, unsigned Column) {
    Loc.Filename = Filename;
    Loc.Line = Line;
    Loc.Column = Column;
  }

  std::string toJSON() const;
  std::string toText() const;
};

// ============================================================
// StructuredDiagEmitter — 诊断发射器抽象基类
// ============================================================

class StructuredDiagEmitter {
public:
  virtual ~StructuredDiagEmitter() = default;
  virtual void emit(const StructuredDiagnostic& D) = 0;
};

// ============================================================
// DiagnosticGroupManager — 诊断分组开关管理（Pimpl）
// ============================================================

class DiagnosticGroupManager {
public:
  DiagnosticGroupManager();
  ~DiagnosticGroupManager();

  DiagnosticGroupManager(const DiagnosticGroupManager&) = delete;
  DiagnosticGroupManager& operator=(const DiagnosticGroupManager&) = delete;

  void enableGroup(DiagnosticGroup G);
  void disableGroup(DiagnosticGroup G);
  bool isGroupEnabled(DiagnosticGroup G) const;

  void enableAll();
  void disableAll();

private:
  class Impl;
  Impl* Pimpl;
};

} // namespace diag
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRDIAGNOSTIC_H
```

---

## 四、完整实现文件 — `src/IR/IRDiagnostic.cpp`

```cpp
#include "blocktype/IR/IRDiagnostic.h"

#include <cstdio>
#include <string>

namespace blocktype {
namespace diag {

// ============================================================
// 名称查找
// ============================================================

const char* getDiagnosticLevelName(DiagnosticLevel L) {
  switch (L) {
  case DiagnosticLevel::Note:    return "note";
  case DiagnosticLevel::Remark:  return "remark";
  case DiagnosticLevel::Warning: return "warning";
  case DiagnosticLevel::Error:   return "error";
  case DiagnosticLevel::Fatal:   return "fatal";
  }
  return "unknown";
}

const char* getDiagnosticGroupName(DiagnosticGroup G) {
  switch (G) {
  case DiagnosticGroup::TypeMapping:            return "TypeMapping";
  case DiagnosticGroup::InstructionValidation: return "InstructionValidation";
  case DiagnosticGroup::IRVerification:         return "IRVerification";
  case DiagnosticGroup::BackendCodegen:         return "BackendCodegen";
  case DiagnosticGroup::FFIBinding:             return "FFIBinding";
  case DiagnosticGroup::Serialization:          return "Serialization";
  }
  return "Unknown";
}

const char* getDiagnosticCodeName(DiagnosticCode C) {
  switch (C) {
  case DiagnosticCode::Unknown:              return "Unknown";
  case DiagnosticCode::TypeMappingFailed:    return "TypeMappingFailed";
  case DiagnosticCode::TypeMappingAmbiguous: return "TypeMappingAmbiguous";
  case DiagnosticCode::InvalidInstruction:   return "InvalidInstruction";
  case DiagnosticCode::InvalidOperand:       return "InvalidOperand";
  case DiagnosticCode::VerificationFailed:   return "VerificationFailed";
  case DiagnosticCode::InvalidModule:        return "InvalidModule";
  case DiagnosticCode::CodegenFailed:        return "CodegenFailed";
  case DiagnosticCode::UnsupportedType:      return "UnsupportedType";
  case DiagnosticCode::FFIBindingFailed:     return "FFIBindingFailed";
  case DiagnosticCode::InvalidFFISignature:  return "InvalidFFISignature";
  case DiagnosticCode::SerializationFailed:  return "SerializationFailed";
  case DiagnosticCode::DeserializationFailed:return "DeserializationFailed";
  }
  return "UnknownCode";
}

DiagnosticGroup getGroupForCode(DiagnosticCode C) {
  uint32_t Val = static_cast<uint32_t>(C);
  uint32_t GroupByte = (Val >> 8) & 0xFF;
  if (GroupByte == 0) return DiagnosticGroup::TypeMapping;
  uint32_t GroupVal = GroupByte - 1;
  if (GroupVal > static_cast<uint32_t>(DiagnosticGroup::Serialization))
    return DiagnosticGroup::TypeMapping;
  return static_cast<DiagnosticGroup>(GroupVal);
}

// ============================================================
// JSON 转义辅助
// ============================================================

static std::string escapeJSON(ir::StringRef S) {
  std::string Result;
  Result.reserve(S.size());
  for (char C : S) {
    switch (C) {
    case '"':  Result += "\\\""; break;
    case '\\': Result += "\\\\"; break;
    case '\n': Result += "\\n";  break;
    case '\r': Result += "\\r";  break;
    case '\t': Result += "\\t";  break;
    default:
      if (static_cast<unsigned char>(C) < 0x20) {
        char Buf[8];
        std::snprintf(Buf, sizeof(Buf), "\\u%04x",
                      static_cast<unsigned char>(C));
        Result += Buf;
      } else {
        Result += C;
      }
    }
  }
  return Result;
}

// ============================================================
// StructuredDiagnostic 输出
// ============================================================

std::string StructuredDiagnostic::toJSON() const {
  std::string OS;
  OS.reserve(256);
  OS += "{\n";
  OS += "  \"level\": \""; OS += getDiagnosticLevelName(Level); OS += "\",\n";
  OS += "  \"group\": \""; OS += getDiagnosticGroupName(Group); OS += "\",\n";
  OS += "  \"code\": \"";  OS += getDiagnosticCodeName(Code);   OS += "\",\n";
  OS += "  \"message\": \""; OS += escapeJSON(Message); OS += "\"";
  if (Loc.isValid()) {
    OS += ",\n  \"location\": {\n";
    OS += "    \"file\": \""; OS += escapeJSON(Loc.Filename); OS += "\",\n";
    OS += "    \"line\": "; OS += std::to_string(Loc.Line);  OS += ",\n";
    OS += "    \"column\": "; OS += std::to_string(Loc.Column); OS += "\n  }";
  }
  if (!Notes.empty()) {
    OS += ",\n  \"notes\": [";
    for (size_t i = 0; i < Notes.size(); ++i) {
      if (i > 0) OS += ",";
      OS += "\n    \""; OS += escapeJSON(ir::StringRef(Notes[i])); OS += "\"";
    }
    OS += "\n  ]";
  }
  OS += "\n}\n";
  return OS;
}

std::string StructuredDiagnostic::toText() const {
  std::string OS;
  OS.reserve(128);
  OS += getDiagnosticLevelName(Level);
  OS += ": ["; OS += getDiagnosticGroupName(Group); OS += "] ";
  OS += getDiagnosticCodeName(Code);
  OS += ": "; OS += Message;
  if (Loc.isValid()) {
    OS += "\n  at "; OS += Loc.Filename.str();
    OS += ":"; OS += std::to_string(Loc.Line);
    OS += ":"; OS += std::to_string(Loc.Column);
  }
  for (const auto& N : Notes) {
    OS += "\n  note: "; OS += N;
  }
  OS += "\n";
  return OS;
}

// ============================================================
// DiagnosticGroupManager Pimpl
// ============================================================

class DiagnosticGroupManager::Impl {
public:
  ir::DenseMap<uint16_t, bool> Overrides;
  bool AllEnabled = true;
};

DiagnosticGroupManager::DiagnosticGroupManager()
    : Pimpl(new Impl()) {}

DiagnosticGroupManager::~DiagnosticGroupManager() {
  delete Pimpl;
}

void DiagnosticGroupManager::enableGroup(DiagnosticGroup G) {
  Pimpl->Overrides[static_cast<uint16_t>(G)] = true;
}

void DiagnosticGroupManager::disableGroup(DiagnosticGroup G) {
  Pimpl->Overrides[static_cast<uint16_t>(G)] = false;
}

bool DiagnosticGroupManager::isGroupEnabled(DiagnosticGroup G) const {
  uint16_t Key = static_cast<uint16_t>(G);
  auto It = Pimpl->Overrides.find(Key);
  if (It != Pimpl->Overrides.end())
    return It->second;
  return Pimpl->AllEnabled;
}

void DiagnosticGroupManager::enableAll() {
  Pimpl->Overrides.clear();
  Pimpl->AllEnabled = true;
}

void DiagnosticGroupManager::disableAll() {
  Pimpl->Overrides.clear();
  Pimpl->AllEnabled = false;
}

} // namespace diag
} // namespace blocktype
```

---

## 五、完整测试文件 — `tests/unit/IR/IRDiagnosticTest.cpp`

```cpp
#include <gtest/gtest.h>

#include "blocktype/IR/IRDiagnostic.h"

using namespace blocktype;
using namespace blocktype::diag;
using namespace blocktype::ir;

// ============================================================
// V1: StructuredDiagnostic 创建 + 字段访问
// ============================================================

TEST(IRDiagnosticTest, CreationAndFieldAccess) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Group = DiagnosticGroup::TypeMapping;
  D.Code = DiagnosticCode::TypeMappingFailed;
  D.Message = "Cannot map QualType to IRType";

  EXPECT_EQ(D.getLevel(), DiagnosticLevel::Error);
  EXPECT_EQ(D.getGroup(), DiagnosticGroup::TypeMapping);
  EXPECT_EQ(D.getCode(), DiagnosticCode::TypeMappingFailed);
  EXPECT_EQ(D.getMessage(), "Cannot map QualType to IRType");
}

// ============================================================
// V2: JSON 输出
// ============================================================

TEST(IRDiagnosticTest, JSONOutput) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Group = DiagnosticGroup::TypeMapping;
  D.Code = DiagnosticCode::TypeMappingFailed;
  D.Message = "Cannot map QualType to IRType";

  std::string JSON = D.toJSON();

  EXPECT_NE(JSON.find("\"level\": \"error\""), std::string::npos);
  EXPECT_NE(JSON.find("\"group\": \"TypeMapping\""), std::string::npos);
  EXPECT_NE(JSON.find("\"code\": \"TypeMappingFailed\""), std::string::npos);
  EXPECT_NE(JSON.find("TypeMappingFailed"), std::string::npos);
  EXPECT_NE(JSON.find("Cannot map QualType to IRType"), std::string::npos);
}

// ============================================================
// V3: Text 输出
// ============================================================

TEST(IRDiagnosticTest, TextOutput) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Warning;
  D.Group = DiagnosticGroup::BackendCodegen;
  D.Code = DiagnosticCode::UnsupportedType;
  D.Message = "Unsupported floating point type";

  std::string Text = D.toText();

  EXPECT_NE(Text.find("warning"), std::string::npos);
  EXPECT_NE(Text.find("BackendCodegen"), std::string::npos);
  EXPECT_NE(Text.find("UnsupportedType"), std::string::npos);
  EXPECT_NE(Text.find("Unsupported floating point type"), std::string::npos);
}

// ============================================================
// V4: Location 信息
// ============================================================

TEST(IRDiagnosticTest, LocationInOutput) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Code = DiagnosticCode::InvalidInstruction;
  D.Message = "bad operand";
  D.setLocation("test.cpp", 42, 7);

  std::string JSON = D.toJSON();
  EXPECT_NE(JSON.find("\"file\": \"test.cpp\""), std::string::npos);
  EXPECT_NE(JSON.find("\"line\": 42"), std::string::npos);
  EXPECT_NE(JSON.find("\"column\": 7"), std::string::npos);

  std::string Text = D.toText();
  EXPECT_NE(Text.find("test.cpp:42:7"), std::string::npos);
}

// ============================================================
// V5: Notes 附加
// ============================================================

TEST(IRDiagnosticTest, NotesInOutput) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Code = DiagnosticCode::VerificationFailed;
  D.Message = "module verification failed";
  D.addNote("see instruction in function 'main'");
  D.addNote("expected terminator instruction");

  std::string JSON = D.toJSON();
  EXPECT_NE(JSON.find("\"notes\""), std::string::npos);
  EXPECT_NE(JSON.find("see instruction"), std::string::npos);
  EXPECT_NE(JSON.find("expected terminator"), std::string::npos);

  std::string Text = D.toText();
  EXPECT_NE(Text.find("note: see instruction"), std::string::npos);
  EXPECT_NE(Text.find("note: expected terminator"), std::string::npos);
}

// ============================================================
// V6: DiagnosticLevel 名称
// ============================================================

TEST(IRDiagnosticTest, LevelNames) {
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Note), "note");
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Remark), "remark");
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Warning), "warning");
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Error), "error");
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Fatal), "fatal");
}

// ============================================================
// V7: DiagnosticGroup 名称
// ============================================================

TEST(IRDiagnosticTest, GroupNames) {
  EXPECT_STREQ(getDiagnosticGroupName(DiagnosticGroup::TypeMapping), "TypeMapping");
  EXPECT_STREQ(getDiagnosticGroupName(DiagnosticGroup::IRVerification), "IRVerification");
  EXPECT_STREQ(getDiagnosticGroupName(DiagnosticGroup::Serialization), "Serialization");
}

// ============================================================
// V8: DiagnosticCode 名称 + getGroupForCode
// ============================================================

TEST(IRDiagnosticTest, CodeNamesAndGroupMapping) {
  EXPECT_STREQ(getDiagnosticCodeName(DiagnosticCode::TypeMappingFailed), "TypeMappingFailed");
  EXPECT_STREQ(getDiagnosticCodeName(DiagnosticCode::InvalidInstruction), "InvalidInstruction");
  EXPECT_STREQ(getDiagnosticCodeName(DiagnosticCode::SerializationFailed), "SerializationFailed");

  EXPECT_EQ(getGroupForCode(DiagnosticCode::TypeMappingFailed), DiagnosticGroup::TypeMapping);
  EXPECT_EQ(getGroupForCode(DiagnosticCode::CodegenFailed), DiagnosticGroup::BackendCodegen);
  EXPECT_EQ(getGroupForCode(DiagnosticCode::DeserializationFailed), DiagnosticGroup::Serialization);
}

// ============================================================
// V9: DiagnosticGroupManager 默认全启用
// ============================================================

TEST(IRDiagnosticTest, GroupManagerDefaultAllEnabled) {
  DiagnosticGroupManager Mgr;
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::Serialization));
}

// ============================================================
// V10: DiagnosticGroupManager 启用/禁用
// ============================================================

TEST(IRDiagnosticTest, GroupManagerEnableDisable) {
  DiagnosticGroupManager Mgr;

  Mgr.disableGroup(DiagnosticGroup::TypeMapping);
  EXPECT_FALSE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::IRVerification));

  Mgr.enableGroup(DiagnosticGroup::TypeMapping);
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
}

// ============================================================
// V11: DiagnosticGroupManager disableAll/enableAll
// ============================================================

TEST(IRDiagnosticTest, GroupManagerAllOrNone) {
  DiagnosticGroupManager Mgr;

  Mgr.disableAll();
  EXPECT_FALSE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
  EXPECT_FALSE(Mgr.isGroupEnabled(DiagnosticGroup::Serialization));

  Mgr.enableAll();
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::Serialization));
}

// ============================================================
// V12: JSON 转义
// ============================================================

TEST(IRDiagnosticTest, JSONEscape) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Code = DiagnosticCode::Unknown;
  D.Message = "path \"C:\\test\" has\nnewlines";

  std::string JSON = D.toJSON();
  EXPECT_NE(JSON.find("\\\"C:\\\\test\\\""), std::string::npos);
  EXPECT_NE(JSON.find("\\n"), std::string::npos);
}

// ============================================================
// V13: 无 Location 时 JSON 不输出 location 字段
// ============================================================

TEST(IRDiagnosticTest, NoLocationOmitField) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Note;
  D.Code = DiagnosticCode::Unknown;
  D.Message = "just a note";

  std::string JSON = D.toJSON();
  EXPECT_EQ(JSON.find("\"location\""), std::string::npos);
}

// ============================================================
// V14: StructuredDiagEmitter 抽象基类（派生测试）
// ============================================================

namespace {
class MockEmitter : public StructuredDiagEmitter {
public:
  mutable StructuredDiagnostic Last;
  mutable int Count = 0;
  void emit(const StructuredDiagnostic& D) const override {
    Last = D;
    ++Count;
  }
};
}

TEST(IRDiagnosticTest, EmitterInterface) {
  MockEmitter E;
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Fatal;
  D.Code = DiagnosticCode::Unknown;
  D.Message = "fatal error";

  E.emit(D);
  EXPECT_EQ(E.Count, 1);
  EXPECT_EQ(E.Last.getLevel(), DiagnosticLevel::Fatal);
  EXPECT_EQ(E.Last.getMessage(), "fatal error");
}
```

---

## 六、CMakeLists.txt 修改

### `src/IR/CMakeLists.txt`

在 `add_library(blocktype-ir ...)` 列表末尾添加一行：

```cmake
  IRDiagnostic.cpp
```

即完整列表变为：

```cmake
add_library(blocktype-ir
  APInt.cpp
  IRBasicBlock.cpp
  IRBuilder.cpp
  IRConstant.cpp
  IRContext.cpp
  IRFormatVersion.cpp
  IRFunction.cpp
  IRInstruction.cpp
  IRModule.cpp
  IRPass.cpp
  IRSerializer.cpp
  IRType.cpp
  IRTypeContext.cpp
  IRValue.cpp
  IRVerifier.cpp
  BackendCapability.cpp
  DialectLoweringPass.cpp
  IRDialect.cpp
  IRTelemetry.cpp
  IRDebugMetadata.cpp
  IRDebugInfo.cpp
  TargetLayout.cpp
  raw_ostream.cpp
  IRDiagnostic.cpp    # <-- 新增
)
```

### `tests/unit/IR/CMakeLists.txt`

在 `add_executable(blocktype-ir-test ...)` 列表末尾添加一行：

```cmake
  IRDiagnosticTest.cpp
```

---

## 七、验收标准映射

| 验收编号 | 验收内容 | 对应测试 |
|----------|----------|----------|
| V1 | StructuredDiagnostic 创建 + 字段访问 | `CreationAndFieldAccess` |
| V2 | JSON 输出包含正确字段 | `JSONOutput` |
| V3 | Text 输出格式正确 | `TextOutput` |
| V4 | Location 信息在 JSON/Text 中正确显示 | `LocationInOutput` |
| V5 | Notes 附加和输出 | `NotesInOutput` |
| V6 | DiagnosticLevel 名称映射 | `LevelNames` |
| V7 | DiagnosticGroup 名称映射 | `GroupNames` |
| V8 | DiagnosticCode 名称 + getGroupForCode | `CodeNamesAndGroupMapping` |
| V9 | GroupManager 默认全启用 | `GroupManagerDefaultAllEnabled` |
| V10 | GroupManager 启用/禁用单组 | `GroupManagerEnableDisable` |
| V11 | GroupManager disableAll/enableAll | `GroupManagerAllOrNone` |
| V12 | JSON 特殊字符转义 | `JSONEscape` |
| V13 | 无 Location 时省略 location 字段 | `NoLocationOmitField` |
| V14 | StructuredDiagEmitter 抽象基类可用 | `EmitterInterface` |

---

## 八、sizeof 影响评估

| 类型 | 预估大小 | 说明 |
|------|----------|------|
| `DiagnosticLevel` | 1 byte | `uint8_t` enum |
| `DiagnosticGroup` | 2 bytes | `uint16_t` enum |
| `DiagnosticCode` | 4 bytes | `uint32_t` enum |
| `ir::SourceLocation` | ~40 bytes | StringRef(16) + 2×unsigned(8) |
| `ir::SmallVector<std::string, 4>` | ~128 bytes | 内联存储 + 指针 + 大小/容量 |
| **StructuredDiagnostic 总计** | **~200 bytes** | 含字符串、Notes 小缓冲 |

说明：StructuredDiagnostic 设计为值类型，可按值传递。Notes 的 `SmallVector<std::string, 4>` 在 4 条以内不会触发堆分配。

---

## 九、风险与注意事项

### 9.1 构建风险

| 风险 | 缓解措施 |
|------|----------|
| `DenseMap<uint16_t, bool>` 在 `.cpp` 中使用，头文件 Pimpl 隐藏 | 前向声明 `class Impl; Impl* Pimpl;` |
| `ir::SourceLocation` 头文件依赖 | 通过 `#include "blocktype/IR/IRDebugMetadata.h"` 引入，已存在 |
| `ir::SmallVector` 命名空间 | 完整限定 `ir::SmallVector` |

### 9.2 兼容性注意

1. **`isGroupEnabled` 的 const 正确性**：`DenseMap::operator[]` 是非 const 方法（会插入默认值），在 const 方法中必须使用 `find()` + 检查 `end()` 模式
2. **`DiagnosticGroupManager` 不可复制**：使用 Pimpl + `delete` 拷贝构造/赋值，避免悬垂指针
3. **`toJSON()`/`toText()` 返回 `std::string`**：每次调用生成新字符串，热路径应缓存或使用 `raw_ostream` 重载（可后续扩展）

### 9.3 与 A-F10 的衔接

- A-F10 将定义 `ir::IRErrorCode`，届时提供 `DiagnosticCode mapToDiagCode(ir::IRErrorCode)` 转换函数
- `StructuredDiagnostic::Code` 类型不变（`diag::DiagnosticCode`），通过转换函数桥接
- 0xGGNN 编码可覆盖 256 组 × 256 个错误码，足够扩展

### 9.4 红线 Checklist 确认

| 编号 | 红线 | 状态 |
|------|------|------|
| (1) | 架构优先 — 诊断系统独立于前后端 | ✅ `diag` 命名空间，无前后端依赖 |
| (2) | 多前端多后端自由组合 | ✅ 纯新增文件，不引入耦合 |
| (3) | 渐进式改造 | ✅ 仅新增文件 + CMake 追加条目 |
| (4) | 现有功能不退化 | ✅ 不修改任何现有文件接口 |
| (5) | 接口抽象优先 | ✅ `StructuredDiagEmitter` 抽象基类 |
| (6) | IR 中间层解耦 | ✅ 诊断属于 IR 层元数据 |

---

## 十、实现步骤（dev-tester 执行顺序）

| 步骤 | 操作 | 预计耗时 |
|------|------|----------|
| 1 | 创建 `include/blocktype/IR/IRDiagnostic.h`（第三节完整代码） | 1 min |
| 2 | 创建 `src/IR/IRDiagnostic.cpp`（第四节完整代码） | 1 min |
| 3 | 修改 `src/IR/CMakeLists.txt`（第六节） | < 1 min |
| 4 | 修改 `tests/unit/IR/CMakeLists.txt`（第六节） | < 1 min |
| 5 | 创建 `tests/unit/IR/IRDiagnosticTest.cpp`（第五节完整代码） | 1 min |
| 6 | 编译 `blocktype-ir`，确认无错误 | 2 min |
| 7 | 运行测试 `blocktype-ir-test`，确认 14 个测试全部通过 | 2 min |
| 8 | 运行全量测试 `ctest`，确认无退化 | 3 min |
| 9 | Git commit | 1 min |
