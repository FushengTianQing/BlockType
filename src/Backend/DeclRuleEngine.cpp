#include "blocktype/Backend/DeclRuleEngine.h"
#include "blocktype/Backend/InstructionSelector.h"
#include "blocktype/IR/IRInstruction.h"
#include "blocktype/IR/IRValue.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/ADT/raw_ostream.h"

#include <fstream>
#include <sstream>

namespace blocktype::backend {

using ir::DenseMap;
using ir::IRFloatType;
using ir::IRInstruction;
using ir::IRIntegerType;
using ir::IRType;
using ir::Opcode;
using ir::raw_ostream;
using ir::SmallVector;
using ir::StringRef;

// ============================================================================
// DeclRuleEngine 辅助函数
// ============================================================================

/// 解析 Condition 字符串，支持简单表达式如 "bitwidth==32", "isSigned==false"
static bool evaluateCondition(const std::string& Condition,
                              const ir::IRInstruction& I) {
  if (Condition.empty()) return true;

  // 解析 "key==value" 或 "key!=value"
  auto EqPos = Condition.find("==");
  auto NePos = Condition.find("!=");

  std::string Key, Value;
  bool IsEqual = true;

  if (EqPos != std::string::npos) {
    Key = Condition.substr(0, EqPos);
    Value = Condition.substr(EqPos + 2);
    IsEqual = true;
  } else if (NePos != std::string::npos) {
    Key = Condition.substr(0, NePos);
    Value = Condition.substr(NePos + 2);
    IsEqual = false;
  } else {
    return true; // 无法解析的条件，默认通过
  }

  // 去除空格
  auto trimStr = [](std::string& S) {
    while (!S.empty() && (S.front() == ' ' || S.front() == '\t'))
      S.erase(S.begin());
    while (!S.empty() && (S.back() == ' ' || S.back() == '\t'))
      S.pop_back();
  };
  trimStr(Key);
  trimStr(Value);

  // 评估条件
  if (Key == "bitwidth") {
    ir::IRType* Ty = I.getType();
    if (Ty->isInteger()) {
      auto* IntTy = static_cast<ir::IRIntegerType*>(Ty);
      unsigned BW = IntTy->getBitWidth();
      bool Match = (std::to_string(BW) == Value);
      return IsEqual ? Match : !Match;
    } else if (Ty->isFloat()) {
      auto* FloatTy = static_cast<ir::IRFloatType*>(Ty);
      unsigned BW = FloatTy->getBitWidth();
      bool Match = (std::to_string(BW) == Value);
      return IsEqual ? Match : !Match;
    }
    return !IsEqual; // 类型不匹配
  }

  if (Key == "isSigned") {
    bool IsSigned = false;
    switch (I.getOpcode()) {
      case ir::Opcode::SDiv: case ir::Opcode::SRem:
      case ir::Opcode::SExt: case ir::Opcode::SIToFP:
      case ir::Opcode::FPToSI:
        IsSigned = true;
        break;
      default:
        break;
    }
    bool Expected = (Value == "true");
    bool Match = (IsSigned == Expected);
    return IsEqual ? Match : !Match;
  }

  if (Key == "numOperands") {
    unsigned N = I.getNumOperands();
    bool Match = (std::to_string(N) == Value);
    return IsEqual ? Match : !Match;
  }

  // 未知条件键，默认通过
  return true;
}

// parseISLEFile 和 ISLEParseResult 声明在 DeclRuleEngine.h 中

// ============================================================================
// DeclRuleEngine 成员函数实现
// ============================================================================

void DeclRuleEngine::rebuildIndex() {
  OpcodeToRuleIndices.clear();
  for (unsigned i = 0; i < Rules.size(); ++i) {
    unsigned OpCodeVal = static_cast<unsigned>(Rules[i].SourceOp);
    OpcodeToRuleIndices[OpCodeVal].push_back(i);
  }
}

bool DeclRuleEngine::select(const ir::IRInstruction& I,
                            TargetInstructionList& Output) {
  unsigned OpCodeVal = static_cast<unsigned>(I.getOpcode());
  auto It = OpcodeToRuleIndices.find(OpCodeVal);
  if (It == OpcodeToRuleIndices.end()) {
    return false; // 没有匹配的规则
  }

  const auto& CandidateIndices = (*It).second;
  int BestPriority = -1;
  int BestIndex = -1;

  // 遍历所有候选规则，选择最高优先级且条件匹配的
  for (unsigned Idx : CandidateIndices) {
    const LoweringRule& R = Rules[Idx];
    if (R.Priority > BestPriority || (R.Priority == BestPriority && BestIndex == -1)) {
      if (evaluateCondition(R.Condition, I)) {
        BestPriority = R.Priority;
        BestIndex = static_cast<int>(Idx);
      }
    }
  }

  if (BestIndex < 0) {
    return false; // 所有规则条件不满足
  }

  const LoweringRule& BestRule = Rules[BestIndex];

  // 创建目标指令
  auto TargetInst = std::make_unique<TargetInstruction>();
  TargetInst->setMnemonic(StringRef(BestRule.TargetPattern));

  // 将 IR 操作数映射到目标指令
  for (unsigned i = 0; i < I.getNumOperands(); ++i) {
    TargetInst->addIROperand(I.getOperand(i));
  }

  Output.push_back(std::move(TargetInst));
  return true;
}

bool DeclRuleEngine::loadRules(StringRef RuleFile) {
  std::string FilePath = RuleFile.str();
  std::ifstream File(FilePath);
  if (!File.is_open()) {
    return false;
  }

  std::string Content((std::istreambuf_iterator<char>(File)),
                       std::istreambuf_iterator<char>());

  // 使用 ISLEParser.cpp 中的解析函数
  auto Result = parseISLEFile(ir::StringRef(Content));

  for (auto& Rule : Result.Rules) {
    addRule(std::move(Rule));
  }

  return Result.NumErrors == 0;
}

bool DeclRuleEngine::verifyCompleteness() {
  // 检查所有 Opcode 都有至少一条规则
  for (uint16_t OpVal = 0; OpVal <= 211; ++OpVal) {
    bool IsValid = false;
    switch (static_cast<ir::Opcode>(OpVal)) {
      case ir::Opcode::Ret: case ir::Opcode::Br: case ir::Opcode::CondBr:
      case ir::Opcode::Switch: case ir::Opcode::Invoke:
      case ir::Opcode::Unreachable: case ir::Opcode::Resume:
      case ir::Opcode::Add: case ir::Opcode::Sub: case ir::Opcode::Mul:
      case ir::Opcode::UDiv: case ir::Opcode::SDiv:
      case ir::Opcode::URem: case ir::Opcode::SRem:
      case ir::Opcode::FAdd: case ir::Opcode::FSub:
      case ir::Opcode::FMul: case ir::Opcode::FDiv:
      case ir::Opcode::FRem:
      case ir::Opcode::Shl: case ir::Opcode::LShr: case ir::Opcode::AShr:
      case ir::Opcode::And: case ir::Opcode::Or: case ir::Opcode::Xor:
      case ir::Opcode::Alloca: case ir::Opcode::Load:
      case ir::Opcode::Store: case ir::Opcode::GEP:
      case ir::Opcode::Memcpy: case ir::Opcode::Memset:
      case ir::Opcode::Trunc: case ir::Opcode::ZExt: case ir::Opcode::SExt:
      case ir::Opcode::FPTrunc: case ir::Opcode::FPExt:
      case ir::Opcode::FPToSI: case ir::Opcode::FPToUI:
      case ir::Opcode::SIToFP: case ir::Opcode::UIToFP:
      case ir::Opcode::PtrToInt: case ir::Opcode::IntToPtr:
      case ir::Opcode::BitCast:
      case ir::Opcode::ICmp: case ir::Opcode::FCmp:
      case ir::Opcode::Call:
      case ir::Opcode::Phi: case ir::Opcode::Select:
      case ir::Opcode::ExtractValue: case ir::Opcode::InsertValue:
      case ir::Opcode::ExtractElement: case ir::Opcode::InsertElement:
      case ir::Opcode::ShuffleVector:
      case ir::Opcode::DbgDeclare: case ir::Opcode::DbgValue:
      case ir::Opcode::DbgLabel:
      case ir::Opcode::FFICall: case ir::Opcode::FFICheck:
      case ir::Opcode::FFICoerce: case ir::Opcode::FFIUnwind:
      case ir::Opcode::AtomicLoad: case ir::Opcode::AtomicStore:
      case ir::Opcode::AtomicRMW: case ir::Opcode::AtomicCmpXchg:
      case ir::Opcode::Fence:
      case ir::Opcode::DynamicCast: case ir::Opcode::VtableDispatch:
      case ir::Opcode::RTTITypeid:
      case ir::Opcode::TargetIntrinsic:
      case ir::Opcode::MetaInlineAlways: case ir::Opcode::MetaInlineNever:
      case ir::Opcode::MetaHot: case ir::Opcode::MetaCold:
        IsValid = true;
        break;
      default:
        break;
    }
    if (!IsValid) continue;

    if (!OpcodeToRuleIndices.contains(static_cast<unsigned>(OpVal))) {
      return false; // 存在没有规则的 Opcode
    }
  }
  return true;
}

void DeclRuleEngine::addRule(LoweringRule R) {
  unsigned OpCodeVal = static_cast<unsigned>(R.SourceOp);
  unsigned Idx = static_cast<unsigned>(Rules.size());
  Rules.push_back(std::move(R));
  OpcodeToRuleIndices[OpCodeVal].push_back(Idx);
}

} // namespace blocktype::backend
