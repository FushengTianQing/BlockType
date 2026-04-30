#ifndef BLOCKTYPE_IR_IRSTATISTICS_H
#define BLOCKTYPE_IR_IRSTATISTICS_H

#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRModule;

/// IR 统计特征提取器：对 IRModule 进行只读统计操作。
class IRStatistics {
  unsigned NumFunctions = 0;
  unsigned NumBasicBlocks = 0;
  unsigned NumInstructions = 0;
  unsigned NumGlobalVars = 0;
  unsigned NumIntegerOps = 0;
  unsigned NumFloatOps = 0;
  unsigned NumMemoryOps = 0;
  unsigned NumCallOps = 0;
  unsigned NumBranchOps = 0;
  unsigned MaxFunctionSize = 0;
  double AvgFunctionSize = 0.0;
  unsigned NumRecursiveFunctions = 0;
  unsigned InstructionComplexity = 0;

public:
  /// 对 IRModule 进行统计分析（只读操作，O(n) 复杂度）。
  static IRStatistics compute(const IRModule& M);

  unsigned getNumFunctions() const { return NumFunctions; }
  unsigned getNumBasicBlocks() const { return NumBasicBlocks; }
  unsigned getNumInstructions() const { return NumInstructions; }
  unsigned getNumGlobalVars() const { return NumGlobalVars; }
  unsigned getNumIntegerOps() const { return NumIntegerOps; }
  unsigned getNumFloatOps() const { return NumFloatOps; }
  unsigned getNumMemoryOps() const { return NumMemoryOps; }
  unsigned getNumCallOps() const { return NumCallOps; }
  unsigned getNumBranchOps() const { return NumBranchOps; }
  unsigned getMaxFunctionSize() const { return MaxFunctionSize; }
  double getAvgFunctionSize() const { return AvgFunctionSize; }
  unsigned getNumRecursiveFunctions() const { return NumRecursiveFunctions; }
  unsigned getInstructionComplexity() const { return InstructionComplexity; }

  /// 输出 JSON 格式统计信息。
  std::string toJSON() const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRSTATISTICS_H
