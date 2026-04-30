#include "blocktype/IR/AIAutoTuner.h"
#include "blocktype/IR/IRStatistics.h"

namespace blocktype {
namespace ir {

StubAIAutoTuner::StubAIAutoTuner() {
  // -O2 标准 Pass 序列
  DefaultSequence.push_back("mem2reg");
  DefaultSequence.push_back("instcombine");
  DefaultSequence.push_back("reassociate");
  DefaultSequence.push_back("loop-simplify");
  DefaultSequence.push_back("licm");
  DefaultSequence.push_back("simplifycfg");
  DefaultSequence.push_back("gvn");
  DefaultSequence.push_back("instcombine");
  DefaultSequence.push_back("tailcallelim");
  DefaultSequence.push_back("adce");
  DefaultSequence.push_back("dse");
  DefaultSequence.push_back("simplifycfg");
}

PassSequence StubAIAutoTuner::recommendPassSequence(const IRStatistics& Stats) {
  // 桩实现：忽略统计信息，直接返回默认 -O2 序列
  (void)Stats;
  return DefaultSequence;
}

void StubAIAutoTuner::recordFeedback(const PassSequenceEvaluation& Eval) {
  // 存储但不使用，为 RL 训练积累数据
  FeedbackHistory.push_back(Eval);
}

} // namespace ir
} // namespace blocktype
