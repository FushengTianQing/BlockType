#ifndef BLOCKTYPE_IR_AIMODELINTEGRATION_H
#define BLOCKTYPE_IR_AIMODELINTEGRATION_H

#include "blocktype/IR/AIAutoTuner.h"

namespace blocktype {
namespace ir {

/// AI 调优模型集成接口：ML 模型接口抽象（不依赖具体框架）。
/// 模型加载可选，加载失败回退到规则引擎。不自动下载模型。
class AIModelIntegration {
  AIAutoTuner& Tuner;
  bool ModelLoaded = false;

public:
  explicit AIModelIntegration(AIAutoTuner& T) : Tuner(T) {}

  /// 加载 ML 模型（桩实现：标记为已加载）。
  bool loadModel(StringRef /*ModelPath*/) {
    // 桩实现：假设加载成功。实际实现中会检查文件是否存在、格式是否正确。
    ModelLoaded = true;
    return true;
  }

  bool isModelLoaded() const { return ModelLoaded; }

  /// 根据 IR 统计信息预测最优 Pass 序列。
  /// 如果模型未加载，自动回退到规则引擎。
  PassSequence predict(const IRStatistics& Stats) {
    if (!ModelLoaded) {
      fallbackToRuleEngine();
    }
    // 始终通过 Tuner 获取推荐（模型加载后可能使用 ML 推理，
    // 未加载时使用规则引擎）
    return Tuner.recommendPassSequence(Stats);
  }

  /// 回退到规则引擎模式。
  void fallbackToRuleEngine() {
    // 标记模型未加载，后续调用将使用规则引擎
    ModelLoaded = false;
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_AIMODELINTEGRATION_H
