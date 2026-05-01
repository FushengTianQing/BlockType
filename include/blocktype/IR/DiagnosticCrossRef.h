#ifndef BLOCKTYPE_IR_DIAGNOSTICCROSSREF_H
#define BLOCKTYPE_IR_DIAGNOSTICCROSSREF_H

#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRDiagnostic.h"

namespace blocktype {
namespace diag {

class DiagnosticCrossRef {
public:
  /// 两个诊断之间的链接关系
  struct DiagLink {
    const StructuredDiagnostic* Source;
    const StructuredDiagnostic* Target;
    std::string Relation;  // 如 "caused by"、"related to"
  };

private:
  /// 所有已注册的诊断指针（不拥有所有权）
  ir::SmallVector<const StructuredDiagnostic*, 32> RegisteredDiags;
  /// 链接关系表
  ir::SmallVector<DiagLink, 16> Links;
  /// 最大链深度
  static constexpr unsigned MaxChainDepth = 5;

public:
  DiagnosticCrossRef() = default;

  /// 注册一条诊断（用于跟踪所有权）
  void registerDiag(const StructuredDiagnostic& D);

  /// 添加一条链接关系：Source → Target，附带关系描述
  void addLink(const StructuredDiagnostic& Source,
               const StructuredDiagnostic& Target,
               ir::StringRef Relation);

  /// 获取从指定诊断出发的交叉引用链（BFS，深度 ≤ MaxChainDepth）
  ir::SmallVector<DiagLink, 8> getChain(const StructuredDiagnostic& Root) const;

  /// 检测是否存在循环引用
  bool hasCycle() const;

  /// 将交叉引用链序列化为 JSON
  std::string toJSON() const;
};

} // namespace diag
} // namespace blocktype

#endif // BLOCKTYPE_IR_DIAGNOSTICCROSSREF_H
