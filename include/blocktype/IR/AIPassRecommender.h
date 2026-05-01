#ifndef BLOCKTYPE_IR_AIPASSRECOMMENDER_H
#define BLOCKTYPE_IR_AIPASSRECOMMENDER_H

#include "blocktype/IR/AIAutoTuner.h"

namespace blocktype {
namespace ir {

/// AI Pass 序列推荐引擎：根据 IR 统计特征推荐最优 Pass 序列。
/// 推荐仅作为建议，不自动执行。无有效推荐时回退到默认基线。
class AIPassRecommender {
  AIAutoTuner& Tuner;
  PassSequence DefaultBaseline;

public:
  explicit AIPassRecommender(AIAutoTuner& T) : Tuner(T) {
    // 初始化默认基线为标准 -O2 序列
    DefaultBaseline = {"dce",      "simplifycfg", "mem2reg",   "gvn",
                       "licm",     "instcombine", "reassociate", "loop-unroll",
                       "tailcallelim", "adce"};
  }

  /// 根据 IR 统计特征推荐 Pass 序列。
  /// 如果推荐结果无效则回退到默认基线。
  PassSequence recommend(const IRStatistics& Stats) {
    auto Seq = Tuner.recommendPassSequence(Stats);
    if (!isRecommendationValid(Seq)) {
      return DefaultBaseline;
    }
    return Seq;
  }

  /// 设置默认基线 Pass 序列。
  void setDefaultBaseline(PassSequence Seq) { DefaultBaseline = std::move(Seq); }

  /// 检查推荐的 Pass 序列是否有效（Pass 名称非空且序列非空）。
  bool isRecommendationValid(const PassSequence& Seq) const {
    if (Seq.empty())
      return false;
    for (auto& P : Seq) {
      if (P.empty())
        return false;
    }
    return true;
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_AIPASSRECOMMENDER_H
