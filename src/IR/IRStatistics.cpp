#include "blocktype/IR/IRStatistics.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRBasicBlock.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRValue.h"

#include <sstream>

namespace blocktype {
namespace ir {

// ============================================================================
// 指令分类辅助函数
// ============================================================================

static bool isIntegerOp(Opcode Op) {
  switch (Op) {
    case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
    case Opcode::UDiv: case Opcode::SDiv: case Opcode::URem: case Opcode::SRem:
    case Opcode::Shl: case Opcode::LShr: case Opcode::AShr:
    case Opcode::And: case Opcode::Or: case Opcode::Xor:
    case Opcode::ICmp:
      return true;
    default:
      return false;
  }
}

static bool isFloatOp(Opcode Op) {
  switch (Op) {
    case Opcode::FAdd: case Opcode::FSub: case Opcode::FMul:
    case Opcode::FDiv: case Opcode::FRem:
    case Opcode::FCmp:
      return true;
    default:
      return false;
  }
}

static bool isMemoryOp(Opcode Op) {
  switch (Op) {
    case Opcode::Alloca: case Opcode::Load: case Opcode::Store:
    case Opcode::GEP: case Opcode::Memcpy: case Opcode::Memset:
    case Opcode::AtomicLoad: case Opcode::AtomicStore:
    case Opcode::AtomicRMW: case Opcode::AtomicCmpXchg: case Opcode::Fence:
      return true;
    default:
      return false;
  }
}

static bool isCallOp(Opcode Op) {
  switch (Op) {
    case Opcode::Call: case Opcode::FFICall:
    case Opcode::FFICheck: case Opcode::FFICoerce: case Opcode::FFIUnwind:
      return true;
    default:
      return false;
  }
}

static bool isBranchOp(Opcode Op) {
  switch (Op) {
    case Opcode::Br: case Opcode::CondBr: case Opcode::Switch:
    case Opcode::Invoke: case Opcode::Resume: case Opcode::Unreachable:
      return true;
    default:
      return false;
  }
}

/// 每种指令的复杂度权重
static unsigned computeInstComplexity(Opcode Op) {
  switch (Op) {
    case Opcode::Call: case Opcode::Invoke: case Opcode::FFICall:
      return 5; // 调用开销较大
    case Opcode::AtomicLoad: case Opcode::AtomicStore:
    case Opcode::AtomicRMW: case Opcode::AtomicCmpXchg: case Opcode::Fence:
      return 3; // 原子操作中等开销
    case Opcode::Alloca: case Opcode::Load: case Opcode::Store:
    case Opcode::GEP: case Opcode::Memcpy: case Opcode::Memset:
      return 2; // 内存操作
    case Opcode::Ret: case Opcode::Br: case Opcode::CondBr:
      return 1; // 简单控制流
    default:
      return 1; // 默认
  }
}

// ============================================================================
// compute 实现
// ============================================================================

IRStatistics IRStatistics::compute(const IRModule& M) {
  IRStatistics Stats;

  Stats.NumFunctions = static_cast<unsigned>(M.getFunctions().size());
  Stats.NumGlobalVars = static_cast<unsigned>(M.getGlobals().size());

  unsigned TotalInstructions = 0;
  unsigned MaxSize = 0;

  for (const auto& Fn : M.getFunctions()) {
    unsigned FnInstCount = 0;
    unsigned FnBBCount = 0;

    for (const auto& BB : Fn->getBasicBlocks()) {
      ++FnBBCount;
      for (const auto& Inst : BB->getInstList()) {
        ++FnInstCount;
        Opcode Op = Inst->getOpcode();

        if (isIntegerOp(Op)) ++Stats.NumIntegerOps;
        else if (isFloatOp(Op)) ++Stats.NumFloatOps;
        else if (isMemoryOp(Op)) ++Stats.NumMemoryOps;
        else if (isCallOp(Op)) ++Stats.NumCallOps;
        else if (isBranchOp(Op)) ++Stats.NumBranchOps;

        Stats.InstructionComplexity += computeInstComplexity(Op);
      }
    }

    Stats.NumBasicBlocks += FnBBCount;
    TotalInstructions += FnInstCount;

    if (FnInstCount > MaxSize) {
      MaxSize = FnInstCount;
    }
  }

  Stats.NumInstructions = TotalInstructions;
  Stats.MaxFunctionSize = MaxSize;

  if (Stats.NumFunctions > 0) {
    Stats.AvgFunctionSize = static_cast<double>(TotalInstructions) /
                             static_cast<double>(Stats.NumFunctions);
  }

  // 检测递归函数：函数体中 Call 指令的目标是自己
  // 由于 IRInstruction 的 Call 指令操作数可能引用函数自身，
  // 这里做简单检测：遍历每个函数中的 Call 指令，看是否有调用自身的
  for (const auto& Fn : M.getFunctions()) {
    bool IsRecursive = false;
    for (const auto& BB : Fn->getBasicBlocks()) {
      for (const auto& Inst : BB->getInstList()) {
        if (Inst->getOpcode() == Opcode::Call) {
          // 检查 Call 的第一个操作数是否引用当前函数
          if (Inst->getNumOperands() > 0) {
            auto* Callee = Inst->getOperand(0);
            if (auto* FnRef = dynamic_cast<IRConstantFunctionRef*>(
                Callee)) {
              if (FnRef->getFunction() == Fn.get()) {
                IsRecursive = true;
                break;
              }
            }
          }
        }
      }
      if (IsRecursive) break;
    }
    if (IsRecursive) ++Stats.NumRecursiveFunctions;
  }

  return Stats;
}

// ============================================================================
// toJSON 实现
// ============================================================================

std::string IRStatistics::toJSON() const {
  std::string Result;
  raw_string_ostream OS(Result);

  OS << "{\n";
  OS << "  \"NumFunctions\": " << NumFunctions << ",\n";
  OS << "  \"NumBasicBlocks\": " << NumBasicBlocks << ",\n";
  OS << "  \"NumInstructions\": " << NumInstructions << ",\n";
  OS << "  \"NumGlobalVars\": " << NumGlobalVars << ",\n";
  OS << "  \"NumIntegerOps\": " << NumIntegerOps << ",\n";
  OS << "  \"NumFloatOps\": " << NumFloatOps << ",\n";
  OS << "  \"NumMemoryOps\": " << NumMemoryOps << ",\n";
  OS << "  \"NumCallOps\": " << NumCallOps << ",\n";
  OS << "  \"NumBranchOps\": " << NumBranchOps << ",\n";
  OS << "  \"MaxFunctionSize\": " << MaxFunctionSize << ",\n";
  OS << "  \"AvgFunctionSize\": " << AvgFunctionSize << ",\n";
  OS << "  \"NumRecursiveFunctions\": " << NumRecursiveFunctions << ",\n";
  OS << "  \"InstructionComplexity\": " << InstructionComplexity << "\n";
  OS << "}";

  return Result;
}

} // namespace ir
} // namespace blocktype
