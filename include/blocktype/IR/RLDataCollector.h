#ifndef BLOCKTYPE_IR_RLDATACOLLECTOR_H
#define BLOCKTYPE_IR_RLDATACOLLECTOR_H

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#include "blocktype/IR/AIAutoTuner.h"
#include "blocktype/IR/IRStatistics.h"

namespace blocktype {
namespace ir {

/// RL 训练数据收集管线：记录 (Stats, Sequence, Eval) 三元组，
/// 支持导出为 JSON 格式，数据量限制 ≤ 1GB/会话。
class RLDataCollector {
  uint64_t MaxDataSizeBytes = 1073741824; // 1GB
  uint64_t CurrentSizeBytes = 0;

public:
  struct DataPoint {
    IRStatistics Stats;
    PassSequence Sequence;
    PassSequenceEvaluation Eval;
  };

private:
  std::vector<DataPoint> Data;

  /// 估算一个 DataPoint 的大小（字节）。
  static uint64_t estimateDataPointSize(const DataPoint& DP) {
    uint64_t Size = sizeof(DataPoint);
    // 序列中的每个字符串引用
    for (auto& S : DP.Sequence) {
      Size += S.size();
    }
    for (auto& S : DP.Eval.Sequence) {
      Size += S.size();
    }
    return Size;
  }

public:
  /// 记录一条训练数据。
  void record(const DataPoint& DP) {
    if (isFull())
      return;
    uint64_t EstSize = estimateDataPointSize(DP);
    if (CurrentSizeBytes + EstSize > MaxDataSizeBytes)
      return;
    Data.push_back(DP);
    CurrentSizeBytes += EstSize;
  }

  /// 导出训练数据为 JSON 文件。
  bool exportData(StringRef Path) const {
    std::ofstream Out(Path.str());
    if (!Out.is_open())
      return false;
    Out << "[\n";
    for (size_t i = 0; i < Data.size(); ++i) {
      const auto& DP = Data[i];
      Out << "  {\n";
      Out << "    \"stats\": " << DP.Stats.toJSON() << ",\n";
      Out << "    \"sequence\": [";
      for (size_t j = 0; j < DP.Sequence.size(); ++j) {
        Out << "\"" << DP.Sequence[j].str() << "\"";
        if (j + 1 < DP.Sequence.size())
          Out << ", ";
      }
      Out << "],\n";
      Out << "    \"eval\": {\n";
      Out << "      \"score\": " << DP.Eval.Score << ",\n";
      Out << "      \"compile_time_ms\": " << DP.Eval.CompileTimeMs << ",\n";
      Out << "      \"code_size_bytes\": " << DP.Eval.CodeSizeBytes << ",\n";
      Out << "      \"sequence\": [";
      for (size_t j = 0; j < DP.Eval.Sequence.size(); ++j) {
        Out << "\"" << DP.Eval.Sequence[j].str() << "\"";
        if (j + 1 < DP.Eval.Sequence.size())
          Out << ", ";
      }
      Out << "]\n";
      Out << "    }\n";
      Out << "  }";
      if (i + 1 < Data.size())
        Out << ",";
      Out << "\n";
    }
    Out << "]\n";
    Out.close();
    return true;
  }

  uint64_t getDataSize() const { return CurrentSizeBytes; }
  bool isFull() const { return CurrentSizeBytes >= MaxDataSizeBytes; }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_RLDATACOLLECTOR_H
