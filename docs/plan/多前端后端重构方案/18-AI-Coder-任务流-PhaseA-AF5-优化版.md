# Task A-F5 优化版：IRDebugMetadata 基础类型定义 + 调试信息升级

> **版本**：优化版 v1.0  
> **生成时间**：2026-04-25  
> **依赖任务**：A-F4（已完成，IRInstruction 已有 DbgInfo 字段）  
> **输出路径**：`docs/plan/多前端后端重构方案/18-AI-Coder-任务流-PhaseA-AF5-优化版.md`

---

## 一、任务概述

### 1.1 目标

实现完整的调试信息类型体系，包含两套共存的调试信息：

1. **基础调试信息**（`blocktype::ir` 命名空间）— 简化的 DWARF 风格类型
2. **升级调试信息**（`blocktype::ir::debug` 命名空间）— 完整的指令级调试信息

同时将 A-F4 创建的占位 `IRInstructionDebugInfo` 扩展为完整类型。

### 1.2 红线 Checklist

| # | 红线规则 | 本 Task 遵守方式 |
|---|---------|---------------|
| 1 | 架构优先 | 两套调试信息体系共存，不破坏现有架构 |
| 2 | 多前端多后端自由组合 | 不引入前后端耦合 |
| 3 | 渐进式改造 | 新增文件为主，仅修改 IRDebugFwd.h 和 IRInstruction.h 的 include |
| 4 | 现有功能不退化 | 所有现有测试必须通过 |
| 5 | 接口抽象优先 | DebugMetadata 抽象基类 + DebugInfoEmitter 抽象基类 |
| 6 | IR 中间层解耦 | 仅涉及 IR 层调试信息 |

### 1.3 产出文件清单

| 操作 | 文件路径 | 说明 |
|------|----------|------|
| **新增** | `include/blocktype/IR/IRDebugMetadata.h` | 基础调试信息类型 |
| **新增** | `include/blocktype/IR/IRDebugInfo.h` | 升级调试信息类型 |
| **新增** | `src/IR/IRDebugMetadata.cpp` | 基础调试信息实现 |
| **新增** | `src/IR/IRDebugInfo.cpp` | 升级调试信息实现 |
| **修改** | `include/blocktype/IR/IRDebugFwd.h` | 改为纯前向声明 + include 转发 |
| **修改** | `include/blocktype/IR/IRInstruction.h` | include 从 IRDebugFwd.h 改为 IRDebugInfo.h |
| **修改** | `src/IR/CMakeLists.txt` | 添加新源文件 |
| **新增** | `tests/unit/IR/IRDebugInfoTest.cpp` | 单元测试 |
| **修改** | `tests/unit/IR/CMakeLists.txt` | 添加测试文件 |

---

## 二、关键问题分析

### 2.1 问题一：IRDebugFwd.h 的升级路径

**现状**（A-F4 产出）：
```cpp
// include/blocktype/IR/IRDebugFwd.h
namespace blocktype::ir::debug {
struct IRInstructionDebugInfo {
  // 占位 — A-F4 阶段为空结构体
};
}
```

**A-F5 的升级方案**：将 `IRDebugFwd.h` 改为纯前向声明 + include 转发：

```cpp
// include/blocktype/IR/IRDebugFwd.h — A-F5 后
#ifndef BLOCKTYPE_IR_IRDEBUGFWD_H
#define BLOCKTYPE_IR_IRDEBUGFWD_H

/// 调试信息前向声明。
/// 完整定义在 IRDebugInfo.h 和 IRDebugMetadata.h 中。

// 如果需要完整类型定义，请直接 include IRDebugInfo.h 或 IRDebugMetadata.h。
// 此文件仅提供前向声明，供不需要完整类型的场景使用。

namespace blocktype {
namespace ir {
namespace debug {
  class IRInstructionDebugInfo;  // 前向声明
  class DebugInfoEmitter;        // 前向声明
} // namespace debug
} // namespace ir
} // namespace blocktype

#endif
```

**IRInstruction.h 的 include 变更**：从 `IRDebugFwd.h` 改为 `IRDebugInfo.h`，因为 `std::optional<T>` 需要完整类型。

### 2.2 问题二：SourceLocation 类型定义

**原始 spec** 中 `IRInstructionDebugInfo` 引用 `IRDebugMetadata::SourceLocation`，但此类型未定义。

**解决方案**：在 `IRDebugMetadata.h` 中定义 `SourceLocation` 结构体：

```cpp
namespace blocktype::ir {

/// 源码位置信息
struct SourceLocation {
  StringRef Filename;
  unsigned Line = 0;
  unsigned Column = 0;

  bool isValid() const { return !Filename.empty() && Line > 0; }
  bool operator==(const SourceLocation& Other) const {
    return Filename == Other.Filename && Line == Other.Line && Column == Other.Column;
  }
  bool operator!=(const SourceLocation& Other) const { return !(*this == Other); }
};

}
```

**设计理由**：
- 使用 `StringRef`（非 `std::string`）— 文件名通常由编译器内部字符串池持有，无需拷贝
- `unsigned` 类型与 DWARF 标准一致
- `isValid()` 提供便捷验证
- 放在 `blocktype::ir` 命名空间（非 `debug` 子命名空间），因为 `SourceLocation` 是基础类型，两套调试信息都使用

### 2.3 问题三：两套 DIType 的兼容性

| 类型 | 命名空间 | 性质 | 用途 |
|------|---------|------|------|
| `blocktype::ir::DIType` | `blocktype::ir` | 类（继承 DebugMetadata） | 基础调试信息的类型描述 |
| `blocktype::ir::debug::DIType` | `blocktype::ir::debug` | 结构体（仅含 Kind 枚举） | 升级调试信息的类型分类 |

**分析**：两者在不同命名空间，不存在 C++ 层面的名称冲突 ✅。但可能导致开发者混淆。

**建议**：在文档注释中明确说明两套体系的关系：
- 基础版（`ir::DIType`）：Phase A-D 使用，简单 DWARF 风格
- 升级版（`ir::debug::DIType`）：Phase E+ 替代基础版，更丰富的类型分类

### 2.4 问题四：DebugMetadata 与 IRMetadata 的关系

**IRModule.h 中已有的 IRMetadata 基类**（第 33-37 行）：

```cpp
class IRMetadata {
public:
  virtual ~IRMetadata() = default;
  virtual void print(raw_ostream& OS) const = 0;
};
```

**A-F5 需要新增的 DebugMetadata 基类**：

```cpp
class DebugMetadata {
public:
  virtual ~DebugMetadata() = default;
  virtual void print(raw_ostream& OS) const = 0;
};
```

**分析**：两者接口完全相同（`virtual ~DebugMetadata()` + `virtual void print(raw_ostream&) const`），但是不同的类。

**设计选择**：

- **方案 A**（推荐）：`DebugMetadata` 继承 `IRMetadata`
  ```cpp
  class DebugMetadata : public IRMetadata {
  public:
    virtual ~DebugMetadata() = default;
  };
  ```
  优点：调试元数据可以统一存储在 `IRModule::Metadata` 中（`std::vector<std::unique_ptr<IRMetadata>>`）

- **方案 B**：独立基类，不继承 `IRMetadata`
  优点：解耦更彻底
  缺点：无法存入 IRModule 的 Metadata 容器

**推荐方案 A**：让 `DebugMetadata` 继承 `IRMetadata`，这样 `DICompileUnit`、`DIType` 等可以同时存入 `IRModule::Metadata`，也保持了 `print()` 接口的一致性。

### 2.5 问题五：编译选项不在本 Task 范围

原始 spec 列出的编译选项（`-g`、`-gdwarf-4`、`-gdwarf-5`、`-gcodeview`、`-gir-debug`、`-fdebug-info-for-profiling`）属于命令行解析层，不属于 A-F5 的类型定义范畴。

**A-F5 范围**：仅定义类型和接口。编译选项解析在后续集成 Task 中实现。

---

## 三、现有代码背景

### 3.1 IRDebugFwd.h（A-F4 产出）

**文件**：`include/blocktype/IR/IRDebugFwd.h`（18 行）

当前仅包含占位空结构体 `debug::IRInstructionDebugInfo`。A-F5 将把此文件改为纯前向声明。

### 3.2 IRInstruction.h（A-F4 修改后）

**文件**：`include/blocktype/IR/IRInstruction.h`（67 行）

A-F4 已添加：
- `#include "blocktype/IR/IRDebugFwd.h"`（第 10 行）
- `std::optional<debug::IRInstructionDebugInfo> DbgInfo;`（第 22 行）
- 4 个 getter/setter 方法（第 51-60 行）

A-F5 将把 include 从 `IRDebugFwd.h` 改为 `IRDebugInfo.h`（因为 `std::optional` 需要完整类型定义）。

### 3.3 IRMetadata 基类

**文件**：`include/blocktype/IR/IRModule.h:33-37`

```cpp
class IRMetadata {
public:
  virtual ~IRMetadata() = default;
  virtual void print(raw_ostream& OS) const = 0;
};
```

`DebugMetadata` 应继承此类。

### 3.4 raw_ostream

**文件**：`include/blocktype/IR/ADT/raw_ostream.h`（174 行）

通过 `ADT.h` 统一导出。`DebugMetadata::print()` 和所有 `DI*` 类的 `print()` 使用此类型。

### 3.5 构建配置

- `src/IR/CMakeLists.txt` — 需添加 `IRDebugMetadata.cpp` 和 `IRDebugInfo.cpp`
- `tests/unit/IR/CMakeLists.txt` — 需添加 `IRDebugInfoTest.cpp`

---

## 四、详细设计

### 4.1 IRDebugMetadata.h — 基础调试信息

**新增文件**：`include/blocktype/IR/IRDebugMetadata.h`

```cpp
#ifndef BLOCKTYPE_IR_IRDEBUGMETADATA_H
#define BLOCKTYPE_IR_IRDEBUGMETADATA_H

#include <cstdint>
#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

// ============================================================
// SourceLocation — 源码位置（基础类型，两套调试信息共用）
// ============================================================

struct SourceLocation {
  StringRef Filename;
  unsigned Line = 0;
  unsigned Column = 0;

  bool isValid() const { return !Filename.empty() && Line > 0; }

  bool operator==(const SourceLocation& Other) const {
    return Filename == Other.Filename &&
           Line == Other.Line &&
           Column == Other.Column;
  }
  bool operator!=(const SourceLocation& Other) const { return !(*this == Other); }
};

// ============================================================
// DebugMetadata — 调试元数据基类
// ============================================================

/// 调试元数据基类。继承自 IRMetadata，可存入 IRModule::Metadata 容器。
class DebugMetadata : public IRMetadata {
public:
  virtual ~DebugMetadata() = default;
  /// 调试元数据类型标识，用于 dyn_cast 等场景
  enum class DebugKind : uint8_t {
    CompileUnit = 0,
    Type = 1,
    Subprogram = 2,
    Location = 3,
  };
  DebugKind getDebugKind() const { return Kind_; }

protected:
  DebugKind Kind_;
  explicit DebugMetadata(DebugKind K) : Kind_(K) {}
};

// ============================================================
// DICompileUnit — 编译单元调试信息
// ============================================================

class DICompileUnit : public DebugMetadata {
  std::string SourceFile_;
  std::string Producer_;
  unsigned Language_;

public:
  explicit DICompileUnit(StringRef Source = "", StringRef Producer = "",
                         unsigned Lang = 0)
    : DebugMetadata(DebugKind::CompileUnit),
      SourceFile_(Source.str()), Producer_(Producer.str()), Language_(Lang) {}

  StringRef getSourceFile() const { return SourceFile_; }
  void setSourceFile(StringRef F) { SourceFile_ = F.str(); }

  StringRef getProducer() const { return Producer_; }
  void setProducer(StringRef P) { Producer_ = P.str(); }

  unsigned getLanguage() const { return Language_; }
  void setLanguage(unsigned L) { Language_ = L; }

  void print(raw_ostream& OS) const override;

  static bool classof(const DebugMetadata* M) {
    return M->getDebugKind() == DebugKind::CompileUnit;
  }
};

// ============================================================
// DIType — 基础类型调试信息
// ============================================================

class DIType : public DebugMetadata {
  std::string Name_;
  uint64_t SizeInBits_;
  uint64_t AlignInBits_;

public:
  explicit DIType(StringRef N = "", uint64_t Size = 0, uint64_t Align = 0)
    : DebugMetadata(DebugKind::Type),
      Name_(N.str()), SizeInBits_(Size), AlignInBits_(Align) {}

  StringRef getName() const { return Name_; }
  void setName(StringRef N) { Name_ = N.str(); }

  uint64_t getSizeInBits() const { return SizeInBits_; }
  void setSizeInBits(uint64_t S) { SizeInBits_ = S; }

  uint64_t getAlignInBits() const { return AlignInBits_; }
  void setAlignInBits(uint64_t A) { AlignInBits_ = A; }

  void print(raw_ostream& OS) const override;

  static bool classof(const DebugMetadata* M) {
    return M->getDebugKind() == DebugKind::Type;
  }
};

// ============================================================
// DISubprogram — 函数/子程序调试信息
// ============================================================

class DISubprogram : public DebugMetadata {
  std::string Name_;
  DICompileUnit* Unit_;
  DISubprogram* Linkage_;

public:
  explicit DISubprogram(StringRef N = "", DICompileUnit* U = nullptr,
                        DISubprogram* L = nullptr)
    : DebugMetadata(DebugKind::Subprogram),
      Name_(N.str()), Unit_(U), Linkage_(L) {}

  StringRef getName() const { return Name_; }
  void setName(StringRef N) { Name_ = N.str(); }

  DICompileUnit* getUnit() const { return Unit_; }
  void setUnit(DICompileUnit* U) { Unit_ = U; }

  DISubprogram* getLinkage() const { return Linkage_; }
  void setLinkage(DISubprogram* L) { Linkage_ = L; }

  void print(raw_ostream& OS) const override;

  static bool classof(const DebugMetadata* M) {
    return M->getDebugKind() == DebugKind::Subprogram;
  }
};

// ============================================================
// DILocation — 源码位置调试信息
// ============================================================

class DILocation : public DebugMetadata {
  unsigned Line_;
  unsigned Column_;
  DISubprogram* Scope_;

public:
  explicit DILocation(unsigned L = 0, unsigned C = 0,
                      DISubprogram* S = nullptr)
    : DebugMetadata(DebugKind::Location),
      Line_(L), Column_(C), Scope_(S) {}

  unsigned getLine() const { return Line_; }
  void setLine(unsigned L) { Line_ = L; }

  unsigned getColumn() const { return Column_; }
  void setColumn(unsigned C) { Column_ = C; }

  DISubprogram* getScope() const { return Scope_; }
  void setScope(DISubprogram* S) { Scope_ = S; }

  void print(raw_ostream& OS) const override;

  static bool classof(const DebugMetadata* M) {
    return M->getDebugKind() == DebugKind::Location;
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRDEBUGMETADATA_H
```

**设计要点**：
- `DebugMetadata` 继承 `IRMetadata`，可存入 `IRModule::Metadata` 容器 ✅
- `DebugKind` 枚举支持 `classof()` 模式（与 LLVM 风格一致）✅
- `SourceLocation` 使用 `StringRef`（非 `std::string`），避免文件名拷贝 ✅
- 所有 `DI*` 类使用 `std::string` 持有可变字段（与原始 spec 一致）✅
- getter/setter 成对提供，所有字段可通过 setter 修改 ✅

### 4.2 IRDebugInfo.h — 升级调试信息

**新增文件**：`include/blocktype/IR/IRDebugInfo.h`

```cpp
#ifndef BLOCKTYPE_IR_IRDEBUGINFO_H
#define BLOCKTYPE_IR_IRDEBUGINFO_H

#include <cstdint>
#include <optional>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRDebugMetadata.h"  // SourceLocation, DISubprogram

namespace blocktype {
namespace ir {
namespace debug {

// ============================================================
// DIType — 升级版类型分类（与 ir::DIType 共存）
// ============================================================

struct DIType {
  enum class Kind : uint8_t {
    Basic = 0, Pointer = 1, Array = 2, Struct = 3, Class = 4,
    Enum = 5, Function = 6, Template = 7, Union = 8,
    Modifier = 9, Subrange = 10, String = 11,
  };
};

// ============================================================
// IRInstructionDebugInfo — 指令级调试信息（完整版）
// ============================================================

/// 每条 IR 指令可携带的调试信息。
/// A-F4 中为空占位，A-F5 扩展为完整类型。
class IRInstructionDebugInfo {
public:
  /// 默认构造 — 无调试信息
  IRInstructionDebugInfo() = default;

  /// 带源码位置构造
  explicit IRInstructionDebugInfo(SourceLocation Loc)
    : Loc_(std::move(Loc)) {}

  // --- 源码位置 ---
  const SourceLocation& getLocation() const { return Loc_; }
  void setLocation(SourceLocation Loc) { Loc_ = std::move(Loc); }
  bool hasLocation() const { return Loc_.isValid(); }

  // --- 所属子程序 ---
  ir::DISubprogram* getSubprogram() const { return Subprogram_.value_or(nullptr); }
  void setSubprogram(ir::DISubprogram* SP) { Subprogram_ = SP; }
  bool hasSubprogram() const { return Subprogram_.has_value(); }
  void clearSubprogram() { Subprogram_.reset(); }

  // --- 人工生成标记 ---
  bool isArtificial() const { return IsArtificial_; }
  void setArtificial(bool V = true) { IsArtificial_ = V; }

  // --- 内联标记 ---
  bool isInlined() const { return IsInlined_; }
  void setInlined(bool V = true) { IsInlined_ = V; }

  // --- 内联位置 ---
  bool hasInlinedAt() const { return InlinedAt_.has_value(); }
  const SourceLocation& getInlinedAt() const { return InlinedAt_.value(); }
  void setInlinedAt(SourceLocation Loc) { InlinedAt_ = std::move(Loc); }
  void clearInlinedAt() { InlinedAt_.reset(); }

  // --- 调试类型分类（可选）---
  DIType::Kind getTypeKind() const { return TypeKind_.value_or(DIType::Kind::Basic); }
  void setTypeKind(DIType::Kind K) { TypeKind_ = K; }
  bool hasTypeKind() const { return TypeKind_.has_value(); }
  void clearTypeKind() { TypeKind_.reset(); }

private:
  SourceLocation Loc_;                              // 源码位置
  std::optional<ir::DISubprogram*> Subprogram_;     // 所属子程序
  bool IsArtificial_ = false;                       // 人工生成
  bool IsInlined_ = false;                          // 内联
  std::optional<SourceLocation> InlinedAt_;         // 内联源码位置
  std::optional<DIType::Kind> TypeKind_;            // 类型分类
};

// ============================================================
// DebugInfoEmitter — 调试信息发射器接口
// ============================================================

/// 调试信息发射器抽象基类。
/// 不同后端实现不同的发射格式（DWARF4/DWARF5/CodeView）。
class DebugInfoEmitter {
public:
  virtual ~DebugInfoEmitter() = default;

  /// 发射通用调试信息
  virtual void emitDebugInfo(const ir::IRModule& M, raw_ostream& OS) = 0;

  /// 发射 DWARF5 格式
  virtual void emitDWARF5(const ir::IRModule& M, raw_ostream& OS) = 0;

  /// 发射 DWARF4 格式
  virtual void emitDWARF4(const ir::IRModule& M, raw_ostream& OS) = 0;

  /// 发射 CodeView 格式（Windows）
  virtual void emitCodeView(const ir::IRModule& M, raw_ostream& OS) = 0;
};

} // namespace debug
} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRDEBUGINFO_H
```

**设计要点**：
- `IRInstructionDebugInfo` 使用 getter/setter 而非公开字段，与 IRInstruction 风格一致 ✅
- 所有 Optional 字段使用 `std::optional`（与项目惯例一致）✅
- `DISubprogram*` 存储为指针（非拥有），指向 `IRModule::Metadata` 中持有的对象 ✅
- `DebugInfoEmitter` 为纯抽象接口，不提供默认实现 ✅
- 额外添加了 `DIType::Kind TypeKind_` 字段（原始 spec 未提及但 `debug::DIType` 枚举暗示需要）

### 4.3 IRDebugMetadata.cpp — 基础调试信息实现

**新增文件**：`src/IR/IRDebugMetadata.cpp`

```cpp
#include "blocktype/IR/IRDebugMetadata.h"

namespace blocktype {
namespace ir {

void DICompileUnit::print(raw_ostream& OS) const {
  OS << "!DICompileUnit(";
  OS << "source: \"" << SourceFile_ << "\", ";
  OS << "producer: \"" << Producer_ << "\", ";
  OS << "language: " << Language_;
  OS << ")";
}

void DIType::print(raw_ostream& OS) const {
  OS << "!DIType(";
  OS << "name: \"" << Name_ << "\", ";
  OS << "size: " << SizeInBits_ << ", ";
  OS << "align: " << AlignInBits_;
  OS << ")";
}

void DISubprogram::print(raw_ostream& OS) const {
  OS << "!DISubprogram(";
  OS << "name: \"" << Name_ << "\"";
  if (Unit_) OS << ", unit: " << Unit_;
  if (Linkage_) OS << ", linkage: " << Linkage_;
  OS << ")";
}

void DILocation::print(raw_ostream& OS) const {
  OS << "!DILocation(";
  OS << "line: " << Line_ << ", ";
  OS << "column: " << Column_;
  if (Scope_) OS << ", scope: " << Scope_;
  OS << ")";
}

} // namespace ir
} // namespace blocktype
```

### 4.4 IRDebugInfo.cpp — 升级调试信息实现

**新增文件**：`src/IR/IRDebugInfo.cpp`

```cpp
#include "blocktype/IR/IRDebugInfo.h"

// IRInstructionDebugInfo 的所有方法均在头文件中 inline 定义。
// 此文件预留用于 DebugInfoEmitter 的具体实现（如 DWARF5Emitter 等）。
// 当前的 DebugInfoEmitter 是纯接口，无默认实现需要在此提供。
//
// 未来可在此文件中添加：
// - 默认的 DebugInfoEmitter 实现（如 NullDebugInfoEmitter）
// - IRInstructionDebugInfo 的序列化/反序列化辅助函数
```

**说明**：当前阶段 `IRDebugInfo.cpp` 为占位文件。`DebugInfoEmitter` 的具体实现（DWARF/CodeView）不在 A-F5 范围内。

---

## 五、现有文件修改规范

### 5.1 修改 IRDebugFwd.h

**文件**：`include/blocktype/IR/IRDebugFwd.h`

**操作**：替换全部内容为纯前向声明

**修改后完整内容**：

```cpp
#ifndef BLOCKTYPE_IR_IRDEBUGFWD_H
#define BLOCKTYPE_IR_IRDEBUGFWD_H

/// @file IRDebugFwd.h
/// 调试信息前向声明。
/// 如需完整类型定义，请直接 include IRDebugInfo.h 或 IRDebugMetadata.h。
/// 此文件仅提供前向声明，用于不需要完整类型的场景（如指针声明）。

namespace blocktype {
namespace ir {

// 基础调试信息前向声明（完整定义在 IRDebugMetadata.h）
class DebugMetadata;
class DICompileUnit;
class DIType;
class DISubprogram;
class DILocation;
struct SourceLocation;

namespace debug {

// 升级调试信息前向声明（完整定义在 IRDebugInfo.h）
class IRInstructionDebugInfo;
class DebugInfoEmitter;
struct DIType;

} // namespace debug
} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRDEBUGFWD_H
```

**注意**：此文件修改后，所有通过 `IRDebugFwd.h` 使用 `debug::IRInstructionDebugInfo` 的代码（如 `IRInstruction.h`）需要改为 include `IRDebugInfo.h`，因为 `std::optional<T>` 要求完整类型。

### 5.2 修改 IRInstruction.h

**文件**：`include/blocktype/IR/IRInstruction.h`

**操作**：将 `#include "blocktype/IR/IRDebugFwd.h"` 改为 `#include "blocktype/IR/IRDebugInfo.h"`

**具体修改**（第 10 行）：

```cpp
// 旧：
#include "blocktype/IR/IRDebugFwd.h"
// 新：
#include "blocktype/IR/IRDebugInfo.h"
```

**其余内容不变**：getter/setter 方法签名完全兼容，因为 `debug::IRInstructionDebugInfo` 的类型名和命名空间未变。

### 5.3 修改 src/IR/CMakeLists.txt

**文件**：`src/IR/CMakeLists.txt`

**操作**：在 `IRTelemetry.cpp` 之后追加两个新源文件

```cmake
  IRTelemetry.cpp
  IRDebugMetadata.cpp    # 新增
  IRDebugInfo.cpp        # 新增
```

### 5.4 修改 tests/unit/IR/CMakeLists.txt

**文件**：`tests/unit/IR/CMakeLists.txt`

**操作**：在 `IRTelemetryTest.cpp` 之后追加测试文件

```cmake
  IRTelemetryTest.cpp
  IRDebugInfoTest.cpp    # 新增
```

---

## 六、实现步骤（渐进式）

| 步骤 | 操作 | 可编译 |
|------|------|--------|
| 1 | 创建 `IRDebugMetadata.h`（基础类型 + SourceLocation） | ✅ 独立头文件 |
| 2 | 创建 `IRDebugInfo.h`（升级类型 + DebugInfoEmitter） | ✅ 依赖步骤 1 |
| 3 | 修改 `IRDebugFwd.h`（改为纯前向声明） | ✅ 但此时 IRInstruction.h 的 std::optional 会报错 |
| 4 | 修改 `IRInstruction.h`（include 改为 IRDebugInfo.h） | ✅ 全量编译通过 |
| 5 | 创建 `IRDebugMetadata.cpp`（print 实现） | ✅ |
| 6 | 创建 `IRDebugInfo.cpp`（占位） | ✅ |
| 7 | 修改两个 CMakeLists.txt | ✅ |
| 8 | 运行现有测试 | ✅ 全部通过 |
| 9 | 编写单元测试 | ✅ |

**关键路径**：步骤 3 和 4 必须在同一编辑中完成，否则 `IRInstruction.h` 会因 `std::optional` 需要完整类型而编译失败。

---

## 七、验收标准

### 7.1 V1：基础调试信息

```cpp
#include "blocktype/IR/IRDebugMetadata.h"

// DICompileUnit
ir::DICompileUnit CU;
CU.setSourceFile("test.cpp");
CU.setProducer("BlockType 1.0");
CU.setLanguage(33);  // DW_LANG_C_plus_plus
assert(CU.getSourceFile() == "test.cpp");
assert(CU.getLanguage() == 33);

// DIType
ir::DIType T("int", 32, 32);
assert(T.getName() == "int");
assert(T.getSizeInBits() == 32);

// DISubprogram
ir::DISubprogram SP("main", &CU);
assert(SP.getName() == "main");
assert(SP.getUnit() == &CU);

// DILocation
ir::DILocation Loc(10, 5, &SP);
assert(Loc.getLine() == 10);
assert(Loc.getColumn() == 5);
assert(Loc.getScope() == &SP);

// SourceLocation
ir::SourceLocation SL;
SL.Filename = "test.cpp";
SL.Line = 42;
SL.Column = 7;
assert(SL.isValid());
assert(!ir::SourceLocation{}.isValid());

// print
raw_string_ostream OS(Str);
CU.print(OS);
assert(!Str.empty());
```

### 7.2 V2：升级调试信息

```cpp
#include "blocktype/IR/IRDebugInfo.h"

// 默认构造
ir::debug::IRInstructionDebugInfo DI;
assert(!DI.isArtificial());
assert(!DI.isInlined());
assert(!DI.hasLocation());
assert(!DI.hasSubprogram());
assert(!DI.hasInlinedAt());

// 设置字段
ir::SourceLocation SL;
SL.Filename = "test.cpp";
SL.Line = 42;
SL.Column = 7;
DI.setLocation(SL);
assert(DI.hasLocation());
assert(DI.getLocation().Line == 42);

DI.setArtificial(true);
assert(DI.isArtificial());

DI.setInlined(true);
assert(DI.isInlined());

// 内联位置
ir::SourceLocation IL;
IL.Filename = "header.h";
IL.Line = 100;
DI.setInlinedAt(IL);
assert(DI.hasInlinedAt());
assert(DI.getInlinedAt().Filename == "header.h");
DI.clearInlinedAt();
assert(!DI.hasInlinedAt());
```

### 7.3 V3：IRInstruction 与调试信息集成

```cpp
#include "blocktype/IR/IRInstruction.h"

// 构造指令
ir::IRInstruction I(ir::Opcode::Add, /*type*/nullptr, 1);

// 默认无调试信息
assert(!I.hasDebugInfo());

// 设置调试信息
ir::debug::IRInstructionDebugInfo DI;
ir::SourceLocation SL;
SL.Filename = "test.cpp";
SL.Line = 42;
DI.setLocation(SL);
I.setDebugInfo(DI);

// 验证
assert(I.hasDebugInfo());
assert(I.getDebugInfo() != nullptr);
assert(I.getDebugInfo()->getLocation().Line == 42);

// 清除
I.clearDebugInfo();
assert(!I.hasDebugInfo());
assert(I.getDebugInfo() == nullptr);
```

### 7.4 V4：两套 DIType 共存不冲突

```cpp
#include "blocktype/IR/IRDebugMetadata.h"
#include "blocktype/IR/IRDebugInfo.h"

// 基础版 DIType（类）
ir::DIType BasicType("int", 32, 32);
BasicType.print(OS);

// 升级版 DIType（枚举结构体）
ir::debug::DIType::Kind K = ir::debug::DIType::Kind::Basic;
assert(K == ir::debug::DIType::Kind::Basic);

// 两者可在同一编译单元中使用，不冲突
```

### 7.5 V5：编译通过 + 现有测试不退化

```bash
cd build && cmake --build . --target blocktype-ir
# 零错误、零警告

cd build && ctest --output-on-failure -R blocktype
# 所有现有测试通过（包括 A-F4 的测试）
```

### 7.6 V6：DebugInfoEmitter 接口可编译

```cpp
#include "blocktype/IR/IRDebugInfo.h"

// 验证 DebugInfoEmitter 是抽象类，不可实例化
// 以下代码不应编译（纯虚函数）：
// ir::debug::DebugInfoEmitter E;  // 编译错误 ✅

// 可以创建派生类
class TestEmitter : public ir::debug::DebugInfoEmitter {
public:
  void emitDebugInfo(const ir::IRModule& M, raw_ostream& OS) override {}
  void emitDWARF5(const ir::IRModule& M, raw_ostream& OS) override {}
  void emitDWARF4(const ir::IRModule& M, raw_ostream& OS) override {}
  void emitCodeView(const ir::IRModule& M, raw_ostream& OS) override {}
};

TestEmitter TE;  // 应编译通过 ✅
```

---

## 八、sizeof 影响评估

### 8.1 A-F4 → A-F5 的 IRInstruction 大小变化

| 阶段 | DbgInfo 内容 | sizeof(std::optional<DbgInfo>) |
|------|-------------|-------------------------------|
| A-F4 | 空结构体 {} | **1 字节** |
| A-F5 | 完整类型 | **~64-96 字节**（估算） |

### 8.2 IRInstructionDebugInfo 字段大小估算

| 字段 | 类型 | 估算大小 |
|------|------|---------|
| `Loc_` | `SourceLocation`（StringRef + 2×unsigned） | ~24 字节 |
| `Subprogram_` | `std::optional<DISubprogram*>` | ~16 字节 |
| `IsArtificial_` | `bool` | 1 字节 |
| `IsInlined_` | `bool` | 1 字节 |
| `InlinedAt_` | `std::optional<SourceLocation>` | ~32 字节 |
| `TypeKind_` | `std::optional<DIType::Kind>` | ~2 字节 |
| **总计**（含对齐填充） | | **~64-96 字节** |

**结论**：A-F5 后 `IRInstruction` 大小将显著增加。如果有百万级指令，额外内存开销约 64-96 MB。如后续发现性能问题，可考虑将 `DbgInfo` 改为 `std::unique_ptr` 或 side table。

---

## 九、风险与注意事项

### 9.1 步骤 3+4 的原子性

修改 `IRDebugFwd.h`（删除占位定义）和修改 `IRInstruction.h`（改 include）**必须在同一次编译中完成**。单独修改其中任何一个都会导致编译失败。

### 9.2 DebugMetadata 的 classof 模式

基础调试信息类型使用 `DebugKind` 枚举 + `classof()` 静态方法，支持 LLVM 风格的 `dyn_cast<DICompileUnit>(metadata)` 模式。如果项目没有 `dyn_cast` 基础设施，可以暂时只使用 `getDebugKind()` 做类型判断。

### 9.3 SourceLocation 的 StringRef 生命周期

`SourceLocation::Filename` 是 `StringRef`（非拥有指针）。使用者必须确保底层字符串在 `SourceLocation` 使用期间保持有效。对于源文件名，通常由编译器的文件表（StringPool/StringMap）持有，生命周期覆盖整个编译过程。

### 9.4 DISubprogram 的 Linkage 指针

`DISubprogram::Linkage_` 是自引用指针（`DISubprogram*`），用于表示 C++ 中的外部链接函数。此指针不拥有所指向的对象，由 `IRModule::Metadata` 管理生命周期。

---

> **Git 提交提醒**：本 Task 完成后，立即执行：
> ```bash
> git add -A && git commit -m "feat(A): 完成 Task A-F5：IRDebugMetadata 基础类型定义 + 调试信息升级"
> ```

