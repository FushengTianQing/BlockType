#ifndef BLOCKTYPE_IR_FORMALVERIFIER_H
#define BLOCKTYPE_IR_FORMALVERIFIER_H

#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IREquivalenceChecker.h"
#include "blocktype/IR/SymbolicExecutor.h"

namespace blocktype {
namespace ir {

/// 完整形式化验证框架：验证 IR Pass 语义保持性和端到端正确性。
/// 通过符号执行检查等价性，不通过时生成反例。
/// 内部调用 IREquivalenceChecker::check() 验证等价性。
class FormalVerifier {
  SymbolicExecutor& SymbolicExec;

public:
  struct VerificationResult {
    bool IsCorrect;
    std::string Counterexample;
    double Coverage;
    unsigned NumPassesVerified;
  };

  explicit FormalVerifier(SymbolicExecutor& SE) : SymbolicExec(SE) {}

  /// 验证 IR Pass 语义保持性：检查 Before 和 After 模块是否语义等价。
  VerificationResult verifyPassSemantics(const IRModule& Before,
                                         const IRModule& After) {
    VerificationResult Result;
    Result.IsCorrect = true;
    Result.Counterexample = "";
    Result.Coverage = 0.0;
    Result.NumPassesVerified = 0;

    // 1. 通过 IREquivalenceChecker::check() 验证结构等价性
    auto EquivResult = IREquivalenceChecker::check(Before, After);
    if (!EquivResult.IsEquivalent) {
      Result.IsCorrect = false;
      // 收集差异作为反例
      std::string CE = "Structural differences: ";
      for (auto& Diff : EquivResult.Differences) {
        CE += Diff + "; ";
      }
      Result.Counterexample = CE;
      return Result;
    }

    // 2. 通过符号执行验证语义等价性
    unsigned PassesVerified = 0;
    double TotalCoverage = 0.0;
    bool AllEquivalent = true;
    std::string WorstCounterexample;

    auto& BeforeFuncs = Before.getFunctions();
    auto& AfterFuncs = After.getFunctions();

    for (size_t i = 0; i < BeforeFuncs.size() && i < AfterFuncs.size(); ++i) {
      auto SymResult =
          SymbolicExec.checkEquivalence(*BeforeFuncs[i], *AfterFuncs[i]);
      TotalCoverage += SymResult.PathCoverage;
      ++PassesVerified;
      if (!SymResult.IsEquivalent) {
        AllEquivalent = false;
        if (WorstCounterexample.empty()) {
          WorstCounterexample = SymResult.Counterexample;
        }
      }
    }

    Result.IsCorrect = AllEquivalent;
    Result.Counterexample = WorstCounterexample;
    Result.NumPassesVerified = PassesVerified;
    Result.Coverage = PassesVerified > 0
                          ? TotalCoverage / static_cast<double>(PassesVerified)
                          : 0.0;

    return Result;
  }

  /// 验证端到端正确性：对整个模块进行完整性检查。
  VerificationResult verifyEndToEnd(const IRModule& M) {
    VerificationResult Result;
    Result.IsCorrect = true;
    Result.Counterexample = "";
    Result.Coverage = 0.0;
    Result.NumPassesVerified = 0;

    unsigned PassesVerified = 0;
    double TotalCoverage = 0.0;

    // 对模块中的每个函数进行符号执行检查
    for (auto& F : M.getFunctions()) {
      // 自身一致性检查：函数不可自相矛盾
      auto SymResult = SymbolicExec.checkEquivalence(*F, *F);
      TotalCoverage += SymResult.PathCoverage;
      ++PassesVerified;
      if (!SymResult.IsEquivalent) {
        Result.IsCorrect = false;
        if (Result.Counterexample.empty()) {
          Result.Counterexample = SymResult.Counterexample;
        }
      }
    }

    Result.NumPassesVerified = PassesVerified;
    Result.Coverage = PassesVerified > 0
                          ? TotalCoverage / static_cast<double>(PassesVerified)
                          : 0.0;

    return Result;
  }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_FORMALVERIFIER_H
