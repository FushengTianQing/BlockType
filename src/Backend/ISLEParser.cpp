#include "blocktype/Backend/InstructionSelector.h"
#include "blocktype/Backend/DeclRuleEngine.h"
#include "blocktype/IR/ADT/raw_ostream.h"

#include <fstream>
#include <sstream>

namespace blocktype::backend {

using ir::raw_ostream;
using ir::SmallVector;
using ir::StringRef;

// ============================================================================
// ISLE Parser — 独立的 .isle 文件解析辅助
// ============================================================================

// ISLEParseResult 和 parseISLEFile 声明在 DeclRuleEngine.h

/// 从字符串解析 Opcode 名
static ir::Opcode parseOpcodeName(const std::string& Name) {
  static const struct { const char* Name; ir::Opcode Op; } Mapping[] = {
    {"add", ir::Opcode::Add}, {"sub", ir::Opcode::Sub},
    {"mul", ir::Opcode::Mul}, {"udiv", ir::Opcode::UDiv},
    {"sdiv", ir::Opcode::SDiv}, {"urem", ir::Opcode::URem},
    {"srem", ir::Opcode::SRem}, {"fadd", ir::Opcode::FAdd},
    {"fsub", ir::Opcode::FSub}, {"fmul", ir::Opcode::FMul},
    {"fdiv", ir::Opcode::FDiv}, {"shl", ir::Opcode::Shl},
    {"lshr", ir::Opcode::LShr}, {"ashr", ir::Opcode::AShr},
    {"and", ir::Opcode::And}, {"or", ir::Opcode::Or},
    {"xor", ir::Opcode::Xor}, {"alloca", ir::Opcode::Alloca},
    {"load", ir::Opcode::Load}, {"store", ir::Opcode::Store},
    {"gep", ir::Opcode::GEP}, {"trunc", ir::Opcode::Trunc},
    {"zext", ir::Opcode::ZExt}, {"sext", ir::Opcode::SExt},
    {"icmp", ir::Opcode::ICmp}, {"fcmp", ir::Opcode::FCmp},
    {"call", ir::Opcode::Call}, {"ret", ir::Opcode::Ret},
    {"br", ir::Opcode::Br}, {"condbr", ir::Opcode::CondBr},
    {"select", ir::Opcode::Select}, {"phi", ir::Opcode::Phi},
    {"memcpy", ir::Opcode::Memcpy}, {"memset", ir::Opcode::Memset},
  };
  for (const auto& M : Mapping) {
    if (Name == M.Name) return M.Op;
  }
  return ir::Opcode::Ret;
}

/// Tokenize 一行 .isle 文本
static SmallVector<std::string, 16> tokenizeISLE(const std::string& Line) {
  SmallVector<std::string, 16> Tokens;
  std::string Current;
  bool InString = false;

  for (size_t i = 0; i < Line.size(); ++i) {
    char C = Line[i];
    if (C == '"') {
      InString = !InString;
      Current += C;
    } else if (InString) {
      Current += C;
    } else if (C == '(' || C == ')') {
      if (!Current.empty()) {
        Tokens.push_back(Current);
        Current.clear();
      }
      Tokens.push_back(std::string(1, C));
    } else if (C == ' ' || C == '\t' || C == '\n' || C == '\r') {
      if (!Current.empty()) {
        Tokens.push_back(Current);
        Current.clear();
      }
    } else {
      Current += C;
    }
  }
  if (!Current.empty()) {
    Tokens.push_back(Current);
  }
  return Tokens;
}

/// 解析 .isle 规则中的一段 S-expression，提取括号匹配的位置
static size_t findMatchingParen(const SmallVector<std::string, 16>& Tokens,
                                size_t Start) {
  int Depth = 0;
  for (size_t i = Start; i < Tokens.size(); ++i) {
    if (Tokens[i] == "(") ++Depth;
    else if (Tokens[i] == ")") {
      --Depth;
      if (Depth == 0) return i;
    }
  }
  return Tokens.size(); // 未找到匹配
}

/// 从 token 列表的指定区间提取内容（含首尾括号）
static SmallVector<std::string, 16> extractTokens(
    const SmallVector<std::string, 16>& Tokens,
    size_t Start, size_t End) {
  SmallVector<std::string, 16> Result;
  for (size_t i = Start; i <= End && i < Tokens.size(); ++i) {
    Result.push_back(Tokens[i]);
  }
  return Result;
}

/// 解析单条 .isle 规则
/// 格式: (rule (lower (opcode type %var1 %var2 ...)) (target_mnemonic args) :priority N)
static bool parseISLERule(const SmallVector<std::string, 16>& Tokens,
                          LoweringRule& OutRule) {
  size_t Pos = 0;

  // 查找 "(rule"
  while (Pos < Tokens.size() && Tokens[Pos] != "(") ++Pos;
  ++Pos;
  if (Pos >= Tokens.size() || Tokens[Pos] != "rule") return false;
  ++Pos;

  // 查找 "(lower"
  while (Pos < Tokens.size() && Tokens[Pos] != "(") ++Pos;
  size_t LowerStart = Pos;
  size_t LowerEnd = findMatchingParen(Tokens, LowerStart);
  if (LowerEnd >= Tokens.size()) return false;

  auto LowerTokens = extractTokens(Tokens, LowerStart, LowerEnd);

  // 解析 lower 内部: (lower (opcode type %vars...))
  size_t LPos = 0;
  while (LPos < LowerTokens.size() && LowerTokens[LPos] != "(") ++LPos;
  ++LPos; // skip first "("
  if (LPos >= LowerTokens.size() || LowerTokens[LPos] != "lower") return false;
  ++LPos;
  // 跳过 "(" before opcode
  while (LPos < LowerTokens.size() && LowerTokens[LPos] != "(") ++LPos;
  ++LPos;

  // 读取 opcode
  if (LPos >= LowerTokens.size()) return false;
  std::string OpcodeName = LowerTokens[LPos];
  OutRule.SourceOp = parseOpcodeName(OpcodeName);
  ++LPos;

  // 读取类型和变量
  std::string ConditionStr;
  while (LPos < LowerTokens.size() && LowerTokens[LPos] != ")") {
    if (LowerTokens[LPos] == "i32") {
      ConditionStr = "bitwidth==32";
    } else if (LowerTokens[LPos] == "i64") {
      ConditionStr = "bitwidth==64";
    } else if (LowerTokens[LPos] == "f32") {
      ConditionStr = "bitwidth==32";
    } else if (LowerTokens[LPos] == "f64") {
      ConditionStr = "bitwidth==64";
    }
    ++LPos;
  }
  OutRule.Condition = ConditionStr;

  Pos = LowerEnd + 1;

  // 查找 target pattern: (mnemonic args...)
  while (Pos < Tokens.size() && Tokens[Pos] != "(") ++Pos;
  size_t TargetStart = Pos;
  size_t TargetEnd = findMatchingParen(Tokens, TargetStart);
  if (TargetEnd >= Tokens.size()) return false;

  auto TargetTokens = extractTokens(Tokens, TargetStart, TargetEnd);
  // 构建目标模式字符串
  std::string TargetPattern;
  for (size_t i = 1; i < TargetTokens.size() - 1; ++i) {
    // 跳过首尾括号
    if (TargetTokens[i] == "(" || TargetTokens[i] == ")") {
      if (!TargetPattern.empty()) TargetPattern += " ";
      TargetPattern += TargetTokens[i];
    } else {
      if (!TargetPattern.empty() &&
          TargetPattern.back() != '(' &&
          TargetTokens[i] != ")") {
        TargetPattern += " ";
      }
      TargetPattern += TargetTokens[i];
    }
  }
  OutRule.TargetPattern = TargetPattern;

  Pos = TargetEnd + 1;

  // 查找 :priority
  OutRule.Priority = 0;
  for (size_t i = Pos; i < Tokens.size(); ++i) {
    if (Tokens[i] == ":priority" && i + 1 < Tokens.size()) {
      OutRule.Priority = std::stoi(Tokens[i + 1]);
      break;
    }
  }

  return true;
}

// ============================================================================
// parseISLEFile — 非 static，供 DeclRuleEngine.cpp 通过 extern 调用
// ============================================================================

/// 解析 .isle 文件内容，返回解析结果。
/// DeclRuleEngine.cpp 通过 extern 声明引用此函数。
ISLEParseResult parseISLEFile(StringRef Content) {
  ISLEParseResult Result;
  std::istringstream Stream(Content.str());
  std::string Line;
  unsigned LineNum = 0;

  while (std::getline(Stream, Line)) {
    ++LineNum;
    ++Result.NumLines;

    // 去除前后空白
    size_t Start = 0;
    while (Start < Line.size() && (Line[Start] == ' ' || Line[Start] == '\t'))
      ++Start;
    size_t End = Line.size();
    while (End > Start && (Line[End - 1] == ' ' || Line[End - 1] == '\t' ||
                           Line[End - 1] == '\n' || Line[End - 1] == '\r'))
      --End;
    std::string Trimmed = Line.substr(Start, End - Start);

    // 跳过空行
    if (Trimmed.empty()) continue;

    // 跳过注释行（以 ;; 开头）
    if (Trimmed.size() >= 2 && Trimmed[0] == ';' && Trimmed[1] == ';') continue;

    auto Tokens = tokenizeISLE(Trimmed);
    LoweringRule Rule;
    if (!parseISLERule(Tokens, Rule)) {
      ir::errs() << "ISLE parse error at line " << LineNum
                 << ": " << Trimmed << "\n";
      ++Result.NumErrors;
      continue;
    }

    Result.Rules.push_back(std::move(Rule));
  }

  return Result;
}

// ============================================================================
// loadISLERulesFromFile — 从文件加载 .isle 规则到 InstructionSelector
// ============================================================================

/// 从 .isle 文件加载规则，通过 dynamic_cast 到 DeclRuleEngine 添加规则。
bool loadISLERulesFromFile(InstructionSelector& Selector, StringRef FilePath) {
  std::ifstream File(FilePath.str());
  if (!File.is_open()) {
    return false;
  }

  std::string Content((std::istreambuf_iterator<char>(File)),
                       std::istreambuf_iterator<char>());

  auto Result = parseISLEFile(ir::StringRef(Content));

  // 尝试 dynamic_cast 到 DeclRuleEngine 以添加规则
  auto* Engine = dynamic_cast<DeclRuleEngine*>(&Selector);
  if (Engine) {
    for (auto& Rule : Result.Rules) {
      Engine->addRule(std::move(Rule));
    }
  }
  // 如果不是 DeclRuleEngine，规则仅解析不加载

  return Result.NumErrors == 0;
}

} // namespace blocktype::backend
