#ifndef BLOCKTYPE_FRONTEND_STRUCTUREDDIAGNOSTIC_H
#define BLOCKTYPE_FRONTEND_STRUCTUREDDIAGNOSTIC_H

#include <optional>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRDiagnostic.h"
#include "blocktype/IR/IRType.h"       // ir::dialect::DialectID
#include "blocktype/IR/IRValue.h"      // ir::Opcode
#include "blocktype/Basic/SourceLocation.h"

namespace blocktype {
namespace diag {

// ============================================================
// FixItHint — 修复提示
// ============================================================

struct FixItHint {
  blocktype::SourceRange Range;
  std::string Replacement;
  std::string Description;
};

// ============================================================
// DiagnosticNote — 带位置的附加说明
// ============================================================

struct DiagnosticNote {
  blocktype::SourceLocation Loc;
  std::string Message;
};

// ============================================================
// DetailedStructuredDiagnostic — 扩展诊断结构
// ============================================================
//
// 继承自现有的 StructuredDiagnostic（IR/IRDiagnostic.h），
// 新增 FixIt、SARIF、IR 关联等字段。
// 现有字段（Level/Group/Code/Message/Loc/Notes）保持不变。

struct DetailedStructuredDiagnostic : public StructuredDiagnostic {
  // 主定位（使用 Basic::SourceLocation 编码形式）
  blocktype::SourceLocation PrimaryLoc;

  // 高亮范围
  ir::SmallVector<blocktype::SourceRange, 4> Ranges;

  // 关联位置
  ir::SmallVector<blocktype::SourceRange, 2> RelatedLocs;

  // 分类标签（如 "type-error"、"syntax"）
  std::string Category;

  // 对应的警告标志名（如 "-Wir"、"-Wdialect"）
  std::string FlagName;

  // FixIt 提示列表
  ir::SmallVector<diag::FixItHint, 2> FixIts;

  // 带位置的详细说明（替代基类 Notes 的纯字符串）
  ir::SmallVector<DiagnosticNote, 4> DetailedNotes;

  // IR 关联信息
  std::optional<ir::dialect::DialectID> IRRelatedDialect;
  std::optional<ir::Opcode> IRRelatedOpcode;
};

// ============================================================
// DetailedDiagEmitter — 扩展诊断发射器
// ============================================================
//
// 继承自 StructuredDiagEmitter，新增 SARIF/JSON/Text 输出。

class DetailedDiagEmitter : public StructuredDiagEmitter {
public:
  ~DetailedDiagEmitter() override = default;

  /// 发射诊断（实现基类纯虚方法）
  void emit(const StructuredDiagnostic& D) override;

  /// 将最近发射的诊断以 SARIF 格式输出
  void emitSARIF(ir::raw_ostream& OS) const;

  /// 将最近发射的诊断以 JSON 格式输出
  void emitJSON(ir::raw_ostream& OS) const;

  /// 将最近发射的诊断以纯文本格式输出
  void emitText(ir::raw_ostream& OS) const;

private:
  // 缓存最近一次诊断（用于格式化输出）
  DetailedStructuredDiagnostic LastDiag_;
  bool HasLast_ = false;
};

} // namespace diag
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_STRUCTUREDDIAGNOSTIC_H
