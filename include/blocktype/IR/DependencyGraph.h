#ifndef BLOCKTYPE_IR_DEPENDENCYGRAPH_H
#define BLOCKTYPE_IR_DEPENDENCYGRAPH_H

#include <cstdint>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRModule;
class IRFunction;

using QueryID = uint64_t;
using Fingerprint = uint64_t;

/// 依赖图：追踪查询之间的依赖关系，支持传递依赖和循环检测。
/// 使用 DAG 表示，Dependencies[X] = X 直接依赖的节点集合，
/// Dependents[X] = 直接依赖 X 的节点集合。
class DependencyGraph {
  DenseMap<QueryID, SmallVector<QueryID, 4>> Dependencies;
  DenseMap<QueryID, SmallVector<QueryID, 4>> Dependents;
  DenseMap<QueryID, Fingerprint> Fingerprints;

public:
  /// 记录一个依赖关系：Dependent 依赖于 Dependencies 列表中的所有节点。
  void recordDependency(QueryID Dependent, ArrayRef<QueryID> Deps);

  /// 获取给定节点直接依赖的节点列表。
  SmallVector<QueryID, 4> getDependencies(QueryID ID) const;

  /// 获取直接依赖于给定节点的节点列表。
  SmallVector<QueryID, 4> getDependents(QueryID ID) const;

  /// 设置节点的指纹值。
  void setFingerprint(QueryID ID, Fingerprint FP);

  /// 获取节点的指纹值。
  Fingerprint getFingerprint(QueryID ID) const;

  /// 检查指纹是否发生变化。
  bool hasFingerprintChanged(QueryID ID, Fingerprint CurrentFP) const;

  /// 获取所有传递依赖者（BFS 遍历 Dependents 图）。
  SmallVector<QueryID, 16> getTransitiveDependents(QueryID ID) const;

  /// 检测是否存在循环依赖（DFS 三色标记法）。
  bool hasCycle() const;

  /// 返回图中节点数量。
  size_t size() const { return Dependencies.size(); }
};

/// FNV-1a 64-bit 哈希，用于计算指纹。
Fingerprint computeFingerprint(StringRef Data);

/// 计算 IRModule 的指纹。
Fingerprint computeFingerprint(const IRModule& M);

/// 计算 IRFunction 的指纹。
Fingerprint computeFingerprint(const IRFunction& F);

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_DEPENDENCYGRAPH_H
