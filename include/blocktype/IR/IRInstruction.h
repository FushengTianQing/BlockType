#ifndef BLOCKTYPE_IR_IRINSTRUCTION_H
#define BLOCKTYPE_IR_IRINSTRUCTION_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRDebugInfo.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRValue.h"

namespace blocktype {
namespace ir {

class IRInstruction : public User {
  Opcode Op;
  dialect::DialectID DialectID_;
  uint8_t Pred_ = 0;    // ICmpPred 或 FCmpPred 的原始值；非比较指令为 0
  IRBasicBlock* Parent;
  std::optional<debug::IRInstructionDebugInfo> DbgInfo;

public:
  IRInstruction(Opcode O, IRType* Ty, unsigned ID,
                dialect::DialectID D = dialect::DialectID::Core, StringRef N = "")
    : User(ValueKind::InstructionResult, Ty, ID, N),
      Op(O), DialectID_(D), Pred_(0), Parent(nullptr) {}

  Opcode getOpcode() const { return Op; }
  dialect::DialectID getDialect() const { return DialectID_; }
  IRBasicBlock* getParent() const { return Parent; }
  void setParent(IRBasicBlock* BB) { Parent = BB; }
  bool isTerminator() const;
  bool isBinaryOp() const;
  bool isCast() const;
  bool isMemoryOp() const;
  bool isComparison() const;
  void eraseFromParent();
  void print(raw_ostream& OS) const override;

  /// 获取 predicate 原始值（仅对 ICmp/FCmp 指令有意义）。
  uint8_t getPredicate() const { return Pred_; }
  /// 设置 predicate 原始值。
  void setPredicate(uint8_t P) { Pred_ = P; }
  /// 获取 ICmp predicate（仅对 Opcode::ICmp 有效）。
  ICmpPred getICmpPredicate() const { return static_cast<ICmpPred>(Pred_); }
  /// 获取 FCmp predicate（仅对 Opcode::FCmp 有效）。
  FCmpPred getFCmpPredicate() const { return static_cast<FCmpPred>(Pred_); }

  /// 设置调试信息
  void setDebugInfo(const debug::IRInstructionDebugInfo& DI) { DbgInfo = DI; }
  /// 获取调试信息（无调试信息时返回 nullptr）
  const debug::IRInstructionDebugInfo* getDebugInfo() const {
    return DbgInfo ? &*DbgInfo : nullptr;
  }
  /// 查询是否有调试信息
  bool hasDebugInfo() const { return DbgInfo.has_value(); }
  /// 清除调试信息
  void clearDebugInfo() { DbgInfo.reset(); }
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRINSTRUCTION_H
