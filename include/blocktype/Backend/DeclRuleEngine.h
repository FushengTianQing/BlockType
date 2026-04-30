#ifndef BLOCKTYPE_BACKEND_DECLRULEENGINE_H
#define BLOCKTYPE_BACKEND_DECLRULEENGINE_H

#include "blocktype/Backend/InstructionSelector.h"
#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace backend {

/// ISLE 解析结果
struct ISLEParseResult {
  ir::SmallVector<LoweringRule, 64> Rules;
  unsigned NumErrors = 0;
  unsigned NumLines = 0;
};

/// 解析 .isle 文件内容（实现在 ISLEParser.cpp）
ISLEParseResult parseISLEFile(ir::StringRef Content);

/// 从 .isle 文件加载规则到 InstructionSelector（实现在 ISLEParser.cpp）
bool loadISLERulesFromFile(InstructionSelector& Selector, ir::StringRef FilePath);

/// 声明式规则引擎：通过声明式规则（.isle 文件或程序化 API）驱动指令选择。
/// 多规则匹配时选择最高优先级，支持条件表达式过滤。
class DeclRuleEngine : public InstructionSelector {
  ir::SmallVector<LoweringRule, 64> Rules;
  ir::DenseMap<unsigned, ir::SmallVector<unsigned, 4>> OpcodeToRuleIndices;

  void rebuildIndex();

public:
  bool select(const ir::IRInstruction& I,
              TargetInstructionList& Output) override;
  bool loadRules(ir::StringRef RuleFile) override;
  bool verifyCompleteness() override;

  /// 添加单条降低规则。
  void addRule(LoweringRule R);

  /// 返回已加载的规则总数。
  size_t getNumRules() const { return Rules.size(); }
};

} // namespace backend
} // namespace blocktype

#endif // BLOCKTYPE_BACKEND_DECLRULEENGINE_H
