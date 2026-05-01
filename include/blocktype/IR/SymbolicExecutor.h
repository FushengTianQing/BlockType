#ifndef BLOCKTYPE_IR_SYMBOLICEXECUTOR_H
#define BLOCKTYPE_IR_SYMBOLICEXECUTOR_H

#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRFunction;

class SymbolicExecutor {
  unsigned MaxPaths = 1000;
  unsigned TimeoutMs = 30000;
public:
  struct SymbolicEquivalenceResult {
    bool IsEquivalent;
    std::string Counterexample;
    double PathCoverage;
  };
  SymbolicEquivalenceResult checkEquivalence(const IRFunction& F1, const IRFunction& F2);
  void setMaxPaths(unsigned N) { MaxPaths = N; }
  void setTimeout(unsigned Ms) { TimeoutMs = Ms; }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_SYMBOLICEXECUTOR_H
