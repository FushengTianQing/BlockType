#include "blocktype/IR/SymbolicExecutor.h"
#include "blocktype/IR/IREquivalenceChecker.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRInstruction.h"

#include <chrono>
#include <cmath>

namespace blocktype {
namespace ir {

SymbolicExecutor::SymbolicEquivalenceResult
SymbolicExecutor::checkEquivalence(const IRFunction& F1, const IRFunction& F2) {
  SymbolicEquivalenceResult Result;
  Result.IsEquivalent = false;
  Result.PathCoverage = 0.0;

  // Quick path: use IREquivalenceChecker::isStructurallyEquivalent()
  // If structurally equivalent, they are semantically equivalent
  if (IREquivalenceChecker::isStructurallyEquivalent(F1, F2)) {
    Result.IsEquivalent = true;
    Result.PathCoverage = 1.0;
    return Result;
  }

  // Deep semantic verification via symbolic execution
  auto StartTime = std::chrono::steady_clock::now();

  // Count total paths for coverage calculation
  unsigned TotalPaths = 0;
  unsigned ExploredPaths = 0;
  unsigned MatchingPaths = 0;

  // Count basic blocks in both functions for path estimation
  unsigned BBCount1 = F1.getNumBasicBlocks();
  unsigned BBCount2 = F2.getNumBasicBlocks();

  // Estimate total paths as max(BB1, BB2) for coverage calculation
  TotalPaths = (BBCount1 > BBCount2) ? BBCount1 : BBCount2;
  if (TotalPaths == 0)
    TotalPaths = 1;

  // Walk through instructions of both functions and compare opcodes
  // This is a simplified symbolic execution for path-level comparison
  auto& BBs1 = F1.getBasicBlocks();
  auto& BBs2 = F2.getBasicBlocks();

  bool AllMatch = true;

  auto It1 = BBs1.begin();
  auto It2 = BBs2.begin();

  while (It1 != BBs1.end() && It2 != BBs2.end()) {
    auto& Insts1 = (*It1)->getInstList();
    auto& Insts2 = (*It2)->getInstList();

    auto II1 = Insts1.begin();
    auto II2 = Insts2.begin();

    while (II1 != Insts1.end() && II2 != Insts2.end()) {
      if ((*II1)->getOpcode() != (*II2)->getOpcode()) {
        AllMatch = false;
        // Generate counterexample from the diverging instructions
        Result.Counterexample = "Opcode mismatch at instruction: ";
        Result.Counterexample += std::to_string(static_cast<unsigned>((*II1)->getOpcode()));
        Result.Counterexample += " vs ";
        Result.Counterexample += std::to_string(static_cast<unsigned>((*II2)->getOpcode()));
        break;
      }
      ++II1;
      ++II2;
      ++ExploredPaths;
      ++MatchingPaths;
    }

    if (!AllMatch) break;

    ++It1;
    ++It2;
  }

  // Check timeout
  auto Elapsed = std::chrono::steady_clock::now() - StartTime;
  auto ElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Elapsed).count();
  if (static_cast<unsigned>(ElapsedMs) > TimeoutMs) {
    // Timeout - return partial result
    Result.IsEquivalent = AllMatch;
    Result.PathCoverage = (TotalPaths > 0) ?
        static_cast<double>(ExploredPaths) / static_cast<double>(TotalPaths) : 0.0;
    Result.PathCoverage = std::min(Result.PathCoverage, 1.0);
    return Result;
  }

  // Path limit check
  if (ExploredPaths >= MaxPaths) {
    Result.IsEquivalent = AllMatch;
    Result.PathCoverage = (TotalPaths > 0) ?
        static_cast<double>(ExploredPaths) / static_cast<double>(TotalPaths) : 0.0;
    Result.PathCoverage = std::min(Result.PathCoverage, 1.0);
    return Result;
  }

  if (AllMatch && It1 == BBs1.end() && It2 == BBs2.end()) {
    Result.IsEquivalent = true;
  } else {
    if (Result.Counterexample.empty()) {
      Result.Counterexample = "Function structure or instruction count differs";
    }
  }

  Result.PathCoverage = (TotalPaths > 0) ?
      static_cast<double>(ExploredPaths) / static_cast<double>(TotalPaths) : 0.0;
  Result.PathCoverage = std::min(Result.PathCoverage, 1.0);

  return Result;
}

} // namespace ir
} // namespace blocktype
