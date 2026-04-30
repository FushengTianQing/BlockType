#ifndef BLOCKTYPE_IR_AIAUTOTUNER_H
#define BLOCKTYPE_IR_AIAUTOTUNER_H

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRStatistics;

/// Pass 序列类型
using PassSequence = SmallVector<StringRef, 16>;

/// Pass 序列评估结果
struct PassSequenceEvaluation {
  PassSequence Sequence;
  double Score = 0.0;
  unsigned CompileTimeMs = 0;
  unsigned CodeSizeBytes = 0;
};

/// AI 自动调优器接口：根据 IR 统计特征推荐 Pass 序列。
class AIAutoTuner {
public:
  virtual ~AIAutoTuner() = default;

  /// 根据 IR 统计信息推荐 Pass 序列。
  virtual PassSequence recommendPassSequence(const IRStatistics& Stats) = 0;

  /// 记录 Pass 序列的执行反馈（为 RL 训练积累数据）。
  virtual void recordFeedback(const PassSequenceEvaluation& Eval) = 0;
};

/// 桩实现：返回固定的 -O2 标准 Pass 序列。
class StubAIAutoTuner : public AIAutoTuner {
  PassSequence DefaultSequence;
  SmallVector<PassSequenceEvaluation, 32> FeedbackHistory;

public:
  StubAIAutoTuner();

  PassSequence recommendPassSequence(const IRStatistics& Stats) override;
  void recordFeedback(const PassSequenceEvaluation& Eval) override;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_AIAUTOTUNER_H
