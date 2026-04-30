#ifndef BLOCKTYPE_IR_PASSINVARIANTCHECKER_H
#define BLOCKTYPE_IR_PASSINVARIANTCHECKER_H

#include "blocktype/IR/IRPass.h"
#include "blocktype/IR/ADT.h"
#include <string>

namespace blocktype {
namespace ir {

// Forward declarations
class IRValue;
class IRInstruction;
class IRBasicBlock;
class IRFunction;

/// 不变量类型枚举
enum class InvariantKind : uint8_t {
  // SSA 不变量
  SSA_SingleDefinition    = 0,
  SSA_Dominance           = 1,
  SSA_PhiNodeConsistency  = 2,

  // 类型不变量
  Type_Completeness       = 10,
  Type_Consistency        = 11,
  Type_FunctionSignature  = 12,

  // 控制流不变量
  CF_SingleTerminator     = 20,
  CF_EntryBlock           = 21,
  CF_Reachability         = 22,

  // 内存不变量
  Mem_DefUseChain         = 30,
  Mem_AllocaOwner         = 31,

  // 数据不变量
  Data_NoUndef            = 40,
  Data_ConstantFoldable   = 41,
};

/// 不变量检查结果
struct InvariantViolation {
  InvariantKind Kind;
  std::string Description;
  const IRValue* Value = nullptr;
  const IRInstruction* Inst = nullptr;
  const IRBasicBlock* BB = nullptr;
  const IRFunction* Fn = nullptr;
};

/// Pass 不变量检查器
class PassInvariantChecker : public Pass {
  SmallVector<InvariantViolation, 16> Violations;
  bool CheckBefore = true;
  bool CheckAfter = true;
  bool FailFast = false;

public:
  explicit PassInvariantChecker(bool Before = true, bool After = true, bool FailFast = false)
    : CheckBefore(Before), CheckAfter(After), FailFast(FailFast) {}

  StringRef getName() const override { return "invariant-checker"; }

  bool run(IRModule& M) override;

  // === 不变量检查接口 ===
  bool checkAllInvariants(const IRModule& M);
  bool checkSSAInvariants(const IRModule& M);
  bool checkTypeInvariants(const IRModule& M);
  bool checkControlFlowInvariants(const IRModule& M);
  bool checkMemoryInvariants(const IRModule& M);

  // === 单个不变量检查 ===
  bool checkSingleDefinition(const IRFunction& Fn);
  bool checkDominance(const IRFunction& Fn);
  bool checkPhiNodeConsistency(const IRFunction& Fn);
  bool checkTypeCompleteness(const IRModule& M);
  bool checkTypeConsistency(const IRModule& M);
  bool checkFunctionSignature(const IRModule& M);
  bool checkSingleTerminator(const IRFunction& Fn);
  bool checkEntryBlock(const IRFunction& Fn);
  bool checkReachability(const IRFunction& Fn);
  bool checkDefUseChain(const IRModule& M);
  bool checkAllocaOwner(const IRFunction& Fn);

  // === 结果查询 ===
  const SmallVector<InvariantViolation, 16>& getViolations() const { return Violations; }
  bool hasViolations() const { return !Violations.empty(); }
  void clearViolations() { Violations.clear(); }

  void printViolations(raw_ostream& OS) const;

private:
  void reportViolation(InvariantKind Kind, StringRef Desc,
                       const IRValue* V = nullptr,
                       const IRInstruction* I = nullptr,
                       const IRBasicBlock* B = nullptr,
                       const IRFunction* F = nullptr);
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_PASSINVARIANTCHECKER_H