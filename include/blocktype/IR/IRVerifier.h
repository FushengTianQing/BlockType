#ifndef BLOCKTYPE_IR_IRVERIFIER_H
#define BLOCKTYPE_IR_IRVERIFIER_H

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRPass.h"

namespace blocktype {
namespace ir {

class IRModule;
class IRFunction;
class IRBasicBlock;
class IRInstruction;
class IRType;

// ============================================================================
// 验证诊断
// ============================================================================

/// 验证检查的类别
enum class VerificationCategory : uint8_t {
  ModuleLevel    = 0,  // 模块级检查
  FunctionLevel  = 1,  // 函数级检查
  BasicBlockLevel = 2, // 基本块级检查
  InstructionLevel = 3, // 指令级检查
  TypeLevel      = 4,  // 类型级检查
};

/// 单条验证诊断信息
struct VerificationDiagnostic {
  VerificationCategory Category;
  std::string Message;

  VerificationDiagnostic(VerificationCategory Cat, const std::string& Msg)
    : Category(Cat), Message(Msg) {}
};

// ============================================================================
// VerifierPass
// ============================================================================

/// IR 验证 Pass — 检查 IRModule 的结构正确性。
///
/// 使用方式：
///   // 收集模式：收集所有错误
///   SmallVector<VerificationDiagnostic, 32> Errors;
///   VerifierPass VP(&Errors);
///   bool OK = VP.run(Module);
///
///   // 断言模式：验证失败直接 assert（Debug 构建）
///   VerifierPass VP;
///   bool OK = VP.run(Module);
///
/// run() 返回值：true = 验证通过，false = 发现错误。
class VerifierPass : public Pass {
public:
  /// 构造函数。
  /// @param Diag 可选的诊断收集器。传入 nullptr 则使用断言模式。
  explicit VerifierPass(SmallVector<VerificationDiagnostic, 32>* Diag = nullptr);

  StringRef getName() const override { return "verifier"; }

  /// 执行验证。true = 通过，false = 失败。
  bool run(IRModule& M) override;

  /// 独立验证函数 — 可不通过 Pass 框架直接调用。
  static bool verify(IRModule& M, SmallVector<VerificationDiagnostic, 32>* Diag = nullptr);

private:
  SmallVector<VerificationDiagnostic, 32>* Diagnostics;
  bool HasErrors = false;

  // ---- 当前验证上下文（用于生成带位置信息的错误消息）----
  std::string CurrentFuncName;
  std::string CurrentBBName;
  std::string CurrentInstInfo;

  // ---- 内部验证方法（均返回 true=通过，false=失败）----

  /// 模块级验证
  bool verifyModule(IRModule& M);

  /// 类型级验证
  bool verifyType(const IRType* T);
  bool verifyTypeComplete(const IRType* T);

  /// 函数级验证
  bool verifyFunction(IRFunction& F);

  /// 基本块级验证
  bool verifyBasicBlock(IRBasicBlock& BB);

  /// 指令级验证
  bool verifyInstruction(const IRInstruction& I);

  // ---- 按操作码分类的指令验证 ----
  bool verifyTerminator(const IRInstruction& I);
  bool verifyBinaryOp(const IRInstruction& I);
  bool verifyFloatBinaryOp(const IRInstruction& I);
  bool verifyBitwiseOp(const IRInstruction& I);
  bool verifyMemoryOp(const IRInstruction& I);
  bool verifyCastOp(const IRInstruction& I);
  bool verifyCmpOp(const IRInstruction& I);
  bool verifyCallOp(const IRInstruction& I);
  bool verifyPhiOp(const IRInstruction& I);
  bool verifySelectOp(const IRInstruction& I);
  bool verifyOtherOp(const IRInstruction& I);

  // ---- 辅助方法 ----

  /// 记录一条验证错误
  void reportError(VerificationCategory Cat, const std::string& Msg);

  /// 构建位置信息前缀
  std::string buildLocationPrefix(VerificationCategory Cat) const;

  /// 获取指令所在函数（通过 Parent BB 追溯）
  const IRFunction* getContainingFunction(const IRInstruction& I) const;

  /// 检查操作数索引是否有效
  bool hasOperand(const IRInstruction& I, unsigned Idx) const;

  /// 获取操作数的类型（带空指针检查）
  IRType* getOperandType(const IRInstruction& I, unsigned Idx) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRVERIFIER_H
