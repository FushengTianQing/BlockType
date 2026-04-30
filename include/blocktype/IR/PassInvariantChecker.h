#ifndef BLOCKTYPE_IR_PASSINVARIANTCHECKER_H
#define BLOCKTYPE_IR_PASSINVARIANTCHECKER_H

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRPass.h"

namespace blocktype {
namespace ir {

class IRValue;
class IRInstruction;
class IRBasicBlock;
class IRFunction;
class IRModule;
class IRType;

/// 不变量类型枚举
enum class InvariantKind : uint8_t {
  // SSA 不变量
  SSA_SingleDefinition    = 0,  // 每个 Value 唯一定义
  SSA_Dominance           = 1,  // 定义支配所有使用
  SSA_PhiNodeConsistency  = 2,  // Phi 节点前驱数量与 BB 前驱数量一致
  
  // 类型不变量
  Type_Completeness       = 10, // 无 OpaqueType 残留
  Type_Consistency        = 11, // 操作数类型与指令要求一致
  Type_FunctionSignature  = 12, // 函数调用参数数量和类型匹配
  
  // 控制流不变量
  CF_SingleTerminator     = 20, // 每个 BB 恰好一个终结指令
  CF_EntryBlock           = 21, // 函数有入口 BB
  CF_Reachability         = 22, // 所有 BB 可达（从入口）
  
  // 内存不变量
  Mem_DefUseChain         = 30, // Use-Def 链双向正确
  Mem_AllocaOwner         = 31, // Alloca 指令在函数入口 BB
  
  // 数据不变量
  Data_NoUndef            = 40, // 无 Undef 值（可选）
  Data_ConstantFoldable   = 41, // 常量表达式已折叠（可选）
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
/// 在每个 Pass 运行前后检查 IR 不变量是否保持
class PassInvariantChecker : public Pass {
  SmallVector<InvariantViolation, 16> Violations;
  bool CheckBefore;
  bool CheckAfter;
  bool FailFast;  // 发现违规立即中止
  
public:
  explicit PassInvariantChecker(bool Before = true, bool After = true, bool FailFast = false)
    : CheckBefore(Before), CheckAfter(After), FailFast(FailFast) {}
  
  StringRef getName() const override { return "invariant-checker"; }
  
  bool run(IRModule& M) override;
  
  // === 不变量检查接口 ===
  
  /// 检查所有不变量
  bool checkAllInvariants(const IRModule& M);
  
  /// 检查 SSA 不变量
  bool checkSSAInvariants(const IRModule& M);
  
  /// 检查类型不变量
  bool checkTypeInvariants(const IRModule& M);
  
  /// 检查控制流不变量
  bool checkControlFlowInvariants(const IRModule& M);
  
  /// 检查内存不变量
  bool checkMemoryInvariants(const IRModule& M);
  
  // === 单个不变量检查 ===
  
  bool checkSingleDefinition(const IRFunction& Fn);
  bool checkSingleTerminator(const IRFunction& Fn);
  bool checkEntryBlock(const IRFunction& Fn);
  
  // === 结果查询 ===
  
  const SmallVector<InvariantViolation, 16>& getViolations() const { return Violations; }
  
  bool hasViolations() const { return !Violations.empty(); }
  
  void clearViolations() { Violations.clear(); }
  
  /// 输出违规报告
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