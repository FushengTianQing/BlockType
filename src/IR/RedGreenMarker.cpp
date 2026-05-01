#include "blocktype/IR/RedGreenMarker.h"

namespace blocktype {
namespace ir {

RedGreenMarker::RedGreenMarker(QueryContext& Q)
    : QC(Q), DG(Q.getDependencyGraph()) {}

MarkColor RedGreenMarker::tryMarkGreen(QueryID ID) {
  // 1. 如果 ID 已标记为 Green → 返回 Green
  if (Marks.contains(ID)) {
    MarkColor Cur = Marks[ID];
    if (Cur == MarkColor::Green)
      return MarkColor::Green;
    if (Cur == MarkColor::Red)
      return MarkColor::Red;
  }

  // 2. 获取 ID 的所有依赖 Deps
  auto Deps = DG.getDependencies(ID);

  // 3. 对每个 Dep 递归 tryMarkGreen
  for (auto Dep : Deps) {
    if (Marks.contains(Dep)) {
      MarkColor DepColor = Marks[Dep];
      // a. 如果 Dep 为 Red → 标记 ID 为 Red，返回 Red
      if (DepColor == MarkColor::Red) {
        Marks[ID] = MarkColor::Red;
        return MarkColor::Red;
      }
      // b. 如果 Dep 为 Unknown → 递归 tryMarkGreen(Dep)
      //    (Green 已跳过，Unknown 需要递归)
      if (DepColor == MarkColor::Unknown) {
        MarkColor RecResult = tryMarkGreen(Dep);
        if (RecResult == MarkColor::Red) {
          Marks[ID] = MarkColor::Red;
          return MarkColor::Red;
        }
      }
    } else {
      // 未标记 → 递归 tryMarkGreen(Dep)
      MarkColor RecResult = tryMarkGreen(Dep);
      if (RecResult == MarkColor::Red) {
        Marks[ID] = MarkColor::Red;
        return MarkColor::Red;
      }
    }
  }

  // 4. 所有依赖都为 Green：
  //    a. 计算当前指纹 FP_current
  //    b. 对比缓存指纹 FP_cached
  //    c. 假阳性优化：重新计算对比结果
  //    d. 如果结果一致 → 标记 Green，返回 Green
  //    e. 如果不一致 → 标记 Red，返回 Red
  Fingerprint FP_current = DG.getFingerprint(ID);

  if (!DG.hasFingerprintChanged(ID, FP_current)) {
    // 指纹一致 → Green
    Marks[ID] = MarkColor::Green;
    return MarkColor::Green;
  }

  // 指纹不一致 → Red
  Marks[ID] = MarkColor::Red;
  return MarkColor::Red;
}

MarkColor RedGreenMarker::getMark(QueryID ID) const {
  auto It = Marks.find(ID);
  if (It != Marks.end())
    return (*It).second;
  return MarkColor::Unknown;
}

void RedGreenMarker::markRed(QueryID ID) {
  Marks[ID] = MarkColor::Red;
}

void RedGreenMarker::reset() {
  Marks.clear();
}

size_t RedGreenMarker::getGreenCount() const {
  size_t Count = 0;
  for (auto Pair : Marks) {
    if (Pair.second == MarkColor::Green)
      ++Count;
  }
  return Count;
}

size_t RedGreenMarker::getRedCount() const {
  size_t Count = 0;
  for (auto Pair : Marks) {
    if (Pair.second == MarkColor::Red)
      ++Count;
  }
  return Count;
}

} // namespace ir
} // namespace blocktype
