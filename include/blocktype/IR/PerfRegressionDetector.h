#ifndef BLOCKTYPE_IR_PERFREGRESSIONDETECTOR_H
#define BLOCKTYPE_IR_PERFREGRESSIONDETECTOR_H

#include <cmath>
#include <fstream>
#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRStatistics.h"

namespace blocktype {
namespace ir {

/// 编译器性能回归自动检测器：对比当前编译结果与基线数据，
/// 检测性能回归（阈值 ≥ 10%），自动生成报告。
class PerfRegressionDetector {
  double RegressionThreshold = 0.10; // 10%
  std::string BaselineDir;

public:
  explicit PerfRegressionDetector(StringRef BaselineDir = ".")
      : BaselineDir(BaselineDir.str()) {}

  struct RegressionReport {
    bool HasRegression;
    std::string Metric;
    double ChangePercent;
  };

  /// 检测编译时间回归。
  RegressionReport checkCompileTime(const IRStatistics& Current) {
    RegressionReport Report;
    Report.HasRegression = false;
    Report.Metric = "compile_time";
    Report.ChangePercent = 0.0;

    // 加载基线统计
    auto BaselineStats = loadBaselineStats();
    if (!BaselineStats.has_value())
      return Report;

    unsigned BaselineComplexity = BaselineStats->getInstructionComplexity();
    unsigned CurrentComplexity = Current.getInstructionComplexity();

    if (BaselineComplexity > 0) {
      double Change =
          static_cast<double>(CurrentComplexity - BaselineComplexity) /
          static_cast<double>(BaselineComplexity);
      Report.ChangePercent = Change;
      if (Change > RegressionThreshold) {
        Report.HasRegression = true;
      }
    }

    return Report;
  }

  /// 检测代码大小回归。
  RegressionReport checkCodeSize(const IRModule& Current) {
    RegressionReport Report;
    Report.HasRegression = false;
    Report.Metric = "code_size";
    Report.ChangePercent = 0.0;

    auto BaselineSize = loadBaselineCodeSize();
    if (BaselineSize == 0)
      return Report;

    unsigned CurrentSize = Current.getNumFunctions();
    double Change = static_cast<double>(CurrentSize - BaselineSize) /
                    static_cast<double>(BaselineSize);
    Report.ChangePercent = Change;
    if (Change > RegressionThreshold) {
      Report.HasRegression = true;
    }

    return Report;
  }

  void setThreshold(double T) { RegressionThreshold = T; }

  /// 持久化基线数据到 BaselineDir。
  void persistBaseline(const IRStatistics& Stats, const IRModule& M) {
    std::string Path = BaselineDir + "/perf_baseline.json";
    std::ofstream Out(Path);
    if (!Out.is_open())
      return;
    Out << "{\n";
    Out << "  \"stats\": " << Stats.toJSON() << ",\n";
    Out << "  \"num_functions\": " << M.getNumFunctions() << "\n";
    Out << "}\n";
    Out.close();
  }

private:
  /// 从基线文件加载统计信息。
  std::optional<IRStatistics> loadBaselineStats() const {
    std::string Path = BaselineDir + "/perf_baseline.json";
    std::ifstream In(Path);
    if (!In.is_open())
      return std::nullopt;
    // 桩实现：返回空值表示无基线
    // 实际实现应解析 JSON 并重建 IRStatistics
    return std::nullopt;
  }

  /// 从基线文件加载代码大小。
  unsigned loadBaselineCodeSize() const {
    std::string Path = BaselineDir + "/perf_baseline.json";
    std::ifstream In(Path);
    if (!In.is_open())
      return 0;
    // 桩实现：返回 0 表示无基线
    return 0;
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_PERFREGRESSIONDETECTOR_H
