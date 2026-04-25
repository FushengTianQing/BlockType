#ifndef BLOCKTYPE_IR_IRDEBUGINFO_H
#define BLOCKTYPE_IR_IRDEBUGINFO_H

#include <cstdint>
#include <optional>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRDebugMetadata.h"

namespace blocktype {
namespace ir {

class IRModule; // forward declaration for DebugInfoEmitter

namespace debug {

// ============================================================
// DIType — 升级版类型分类（与 ir::DIType 共存）
// ============================================================

/// 升级版类型分类。与 ir::DIType（基础调试信息的类型描述类）在不同命名空间，不冲突。
/// 基础版（ir::DIType）：Phase A-D 使用，简单 DWARF 风格。
/// 升级版（debug::DIType）：Phase E+ 替代基础版，更丰富的类型分类。
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
