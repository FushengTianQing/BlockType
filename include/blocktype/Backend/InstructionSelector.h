#pragma once
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRValue.h"       // ir::Opcode
#include "blocktype/IR/IRType.h"        // ir::dialect::DialectID
#include "blocktype/IR/IRInstruction.h" // ir::IRInstruction
#include <string>

namespace blocktype::backend {

/// 降级规则：描述一个 IR Opcode 如何映射到目标指令模式
struct LoweringRule {
  ir::Opcode SourceOp = ir::Opcode::Ret;
  ir::dialect::DialectID SourceDialect = ir::dialect::DialectID::Core;
  std::string TargetPattern;
  std::string Condition;
  int Priority = 0;
};

/// 目标指令：一条 machine-level 指令的抽象表示
class TargetInstruction {
  std::string Mnemonic;
  ir::SmallVector<unsigned, 4> UsedRegs;
  ir::SmallVector<unsigned, 4> DefRegs;
  ir::SmallVector<ir::IRValue*, 2> IROperands;

public:
  TargetInstruction() = default;

  /// 获取指令助记符（如 "add", "mov"）
  ir::StringRef getMnemonic() const { return Mnemonic; }
  void setMnemonic(ir::StringRef M) { Mnemonic = M.str(); }

  /// 获取使用的寄存器列表
  ir::ArrayRef<unsigned> getUsedRegs() const { return UsedRegs; }
  void addUsedReg(unsigned Reg) { UsedRegs.push_back(Reg); }

  /// 获取定义的寄存器列表
  ir::ArrayRef<unsigned> getDefRegs() const { return DefRegs; }
  void addDefReg(unsigned Reg) { DefRegs.push_back(Reg); }

  /// 获取 IR 操作数（指针仅在 IRModule 生命周期内有效）
  ir::ArrayRef<ir::IRValue*> getIROperands() const { return IROperands; }
  void addIROperand(ir::IRValue* V) { IROperands.push_back(V); }
};

/// 目标指令列表
using TargetInstructionList = ir::SmallVector<std::unique_ptr<TargetInstruction>, 8>;

/// 指令选择器抽象基类
class InstructionSelector {
public:
  virtual ~InstructionSelector() = default;

  /// 为一条 IR 指令选择目标指令序列
  virtual bool select(const ir::IRInstruction& I,
                      TargetInstructionList& Output) = 0;

  /// 从文件加载降级规则表
  virtual bool loadRules(ir::StringRef RuleFile) = 0;

  /// 验证规则表完整性（所有 Opcode 都有对应规则）
  virtual bool verifyCompleteness() = 0;
};

} // namespace blocktype::backend
