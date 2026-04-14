//===--- Preprocessor.cpp - Preprocessor Implementation -------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Preprocessor class.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include <unordered_map>

namespace blocktype {

//===----------------------------------------------------------------------===//
// MacroInfo Implementation
//===----------------------------------------------------------------------===//

bool MacroInfo::isParameter(StringRef Name) const {
  for (StringRef Param : ParameterList) {
    if (Param == Name)
      return true;
  }
  return false;
}

bool MacroInfo::isIdenticalTo(const MacroInfo *Other) const {
  if (IsFunctionLike != Other->IsFunctionLike)
    return false;
  if (IsVariadic != Other->IsVariadic)
    return false;
  if (ParameterList.size() != Other->ParameterList.size())
    return false;
  if (ReplacementTokens.size() != Other->ReplacementTokens.size())
    return false;

  for (size_t i = 0; i < ParameterList.size(); ++i) {
    if (ParameterList[i] != Other->ParameterList[i])
      return false;
  }

  for (size_t i = 0; i < ReplacementTokens.size(); ++i) {
    if (ReplacementTokens[i].getKind() != Other->ReplacementTokens[i].getKind())
      return false;
    if (ReplacementTokens[i].getText() != Other->ReplacementTokens[i].getText())
      return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Chinese Directive Mapping
//===----------------------------------------------------------------------===//

static const std::unordered_map<std::string, std::string> ChineseDirectiveMap = {
    {"包含", "include"},
    {"定义", "define"},
    {"取消定义", "undef"},
    {"如果", "if"},
    {"如果定义", "ifdef"},
    {"如果未定义", "ifndef"},
    {"否则", "else"},
    {"否则如果", "elif"},
    {"结束如果", "endif"},
    {"错误", "error"},
    {"警告", "warning"},
    {"编译指示", "pragma"},
    {"嵌入", "embed"},
};

//===----------------------------------------------------------------------===//
// Preprocessor Implementation
//===----------------------------------------------------------------------===//

Preprocessor::Preprocessor(SourceManager &SM, DiagnosticsEngine &Diags,
                           HeaderSearch *HS, LanguageManager *LM)
    : SM(SM), Diags(Diags), Headers(HS), LangMgr(LM) {
  initializePredefinedMacros();
}

Preprocessor::~Preprocessor() = default;

void Preprocessor::initializePredefinedMacros() {
  definePredefinedMacro("__cplusplus", "202602L");
  definePredefinedMacro("__cpp_static_assert", "202306L");
  definePredefinedMacro("__cpp_reflexpr", "202502L");
  definePredefinedMacro("__cpp_contracts", "202502L");
  definePredefinedMacro("__cpp_pack_indexing", "202411L");
  definePredefinedMacro("__STDC_HOSTED__", "1");
  definePredefinedMacro("__BLOCKTYPE__", "1");
}

void Preprocessor::definePredefinedMacro(StringRef Name, StringRef Value) {
  auto MI = std::make_unique<MacroInfo>(SourceLocation());
  MI->setPredefined(true);

  Token T;
  T.setKind(TokenKind::numeric_constant);
  T.setLiteralData(Value.data());
  T.setLength(static_cast<unsigned>(Value.size()));
  MI->addToken(T);

  PredefinedMacros.insert(Name);
  Macros[Name] = std::move(MI);
}

//===----------------------------------------------------------------------===//
// Main entry points
//===----------------------------------------------------------------------===//

bool Preprocessor::lexToken(Token &Result) {
  while (true) {
    if (!CurLexer) {
      Result.setKind(TokenKind::eof);
      return false;
    }

    if (!lexFromLexer(Result)) {
      if (!IncludeStack.empty()) {
        IncludeStack.pop_back();
        CurLexer = IncludeStack.empty() ? nullptr : IncludeStack.back().Lex.get();
        continue;
      }
      Result.setKind(TokenKind::eof);
      return false;
    }

    if (Result.is(TokenKind::hash) && CurLexer->isAtStartOfLine()) {
      handleDirective(Result);
      continue;
    }

    if (Skipping) {
      continue;
    }

    if (Result.is(TokenKind::identifier)) {
      StringRef Name = Result.getText();
      if (MacroInfo *MI = getMacroInfo(Name)) {
        if (expandMacro(Result, Name, MI)) {
          return true;  // Return the expanded token
        }
      }
    }

    return true;
  }
}

bool Preprocessor::lexFromLexer(Token &Result) {
  return CurLexer->lexToken(Result);
}

void Preprocessor::enterSourceFile(StringRef Filename, StringRef Content) {
  auto Lex = std::make_unique<Lexer>(SM, Diags, Content, SM.createMainFileID(Filename, Content));

  IncludeStackEntry Entry;
  Entry.Lex = std::move(Lex);
  Entry.Filename = Filename;
  IncludeStack.push_back(std::move(Entry));
  CurLexer = IncludeStack.back().Lex.get();
}

bool Preprocessor::isEOF() const {
  return !CurLexer && IncludeStack.empty();
}

//===----------------------------------------------------------------------===//
// Macro management
//===----------------------------------------------------------------------===//

void Preprocessor::defineMacro(StringRef Name, StringRef Body) {
  auto MI = std::make_unique<MacroInfo>(SourceLocation());

  Token T;
  T.setKind(TokenKind::identifier);
  T.setLiteralData(Body.data());
  T.setLength(static_cast<unsigned>(Body.size()));
  MI->addToken(T);

  Macros[Name] = std::move(MI);
}

void Preprocessor::defineMacro(StringRef Name, std::unique_ptr<MacroInfo> MI) {
  Macros[Name] = std::move(MI);
}

void Preprocessor::undefMacro(StringRef Name) {
  Macros.erase(Name);
}

bool Preprocessor::isMacroDefined(StringRef Name) const {
  return Macros.find(Name) != Macros.end();
}

MacroInfo *Preprocessor::getMacroInfo(StringRef Name) const {
  auto It = Macros.find(Name);
  return It != Macros.end() ? It->second.get() : nullptr;
}

std::vector<StringRef> Preprocessor::getMacroNames() const {
  std::vector<StringRef> Names;
  for (const auto &Pair : Macros) {
    Names.push_back(Pair.first);
  }
  return Names;
}

//===----------------------------------------------------------------------===//
// Directive handling
//===----------------------------------------------------------------------===//

void Preprocessor::handleDirective(Token &HashTok) {
  // Set preprocessor directive mode
  CurLexer->setInPreprocessorDirective(true);

  Token DirectiveTok;
  if (!lexFromLexer(DirectiveTok)) {
    CurLexer->setInPreprocessorDirective(false);
    Diags.report(HashTok.getLocation(), DiagLevel::Error, "unexpected end of file in directive");
    return;
  }

  StringRef DirectiveName = getDirectiveName(DirectiveTok);

  // Check if it's a Chinese directive
  if (ChineseDirectiveMap.find(DirectiveName.str()) != ChineseDirectiveMap.end()) {
    handleBilingualDirective(DirectiveTok);
    return;
  }

  if (DirectiveName == "include") {
    handleIncludeDirective(DirectiveTok, false);
  } else if (DirectiveName == "define") {
    handleDefineDirective(DirectiveTok);
  } else if (DirectiveName == "undef") {
    handleUndefDirective(DirectiveTok);
  } else if (DirectiveName == "if") {
    handleIfDirective(DirectiveTok);
  } else if (DirectiveName == "ifdef") {
    handleIfdefDirective(DirectiveTok);
  } else if (DirectiveName == "ifndef") {
    handleIfndefDirective(DirectiveTok);
  } else if (DirectiveName == "elif") {
    handleElifDirective(DirectiveTok);
  } else if (DirectiveName == "else") {
    handleElseDirective(DirectiveTok);
  } else if (DirectiveName == "endif") {
    handleEndifDirective(DirectiveTok);
  } else if (DirectiveName == "error") {
    handleErrorDirective(DirectiveTok);
  } else if (DirectiveName == "warning") {
    handleWarningDirective(DirectiveTok);
  } else if (DirectiveName == "pragma") {
    handlePragmaDirective(DirectiveTok);
  } else if (DirectiveName == "line") {
    handleLineDirective(DirectiveTok);
  } else if (DirectiveName == "embed") {
    handleEmbedDirective(DirectiveTok);
  } else {
    Diags.report(DirectiveTok.getLocation(), DiagLevel::Error, "unknown preprocessor directive");
  }
}

StringRef Preprocessor::getDirectiveName(Token &Tok) {
  if (Tok.is(TokenKind::identifier)) {
    return Tok.getText();
  }
  if (Tok.isKeyword()) {
    return Tok.getText();
  }
  return StringRef();
}

StringRef Preprocessor::mapChineseDirective(StringRef ZhDirective) {
  auto It = ChineseDirectiveMap.find(ZhDirective.str());
  if (It != ChineseDirectiveMap.end()) {
    return It->second;
  }
  return ZhDirective;
}

void Preprocessor::handleBilingualDirective(Token &DirectiveTok) {
  StringRef ZhName = DirectiveTok.getText();
  StringRef EnName = mapChineseDirective(ZhName);

  if (EnName == "include") {
    handleIncludeDirective(DirectiveTok, false);
  } else if (EnName == "define") {
    handleDefineDirective(DirectiveTok);
  } else if (EnName == "undef") {
    handleUndefDirective(DirectiveTok);
  } else if (EnName == "if") {
    handleIfDirective(DirectiveTok);
  } else if (EnName == "ifdef") {
    handleIfdefDirective(DirectiveTok);
  } else if (EnName == "ifndef") {
    handleIfndefDirective(DirectiveTok);
  } else if (EnName == "elif") {
    handleElifDirective(DirectiveTok);
  } else if (EnName == "else") {
    handleElseDirective(DirectiveTok);
  } else if (EnName == "endif") {
    handleEndifDirective(DirectiveTok);
  } else if (EnName == "error") {
    handleErrorDirective(DirectiveTok);
  } else if (EnName == "warning") {
    handleWarningDirective(DirectiveTok);
  } else if (EnName == "pragma") {
    handlePragmaDirective(DirectiveTok);
  } else if (EnName == "embed") {
    handleEmbedDirective(DirectiveTok);
  } else {
    Diags.report(DirectiveTok.getLocation(), DiagLevel::Error, "unknown Chinese preprocessor directive");
  }
}

//===----------------------------------------------------------------------===//
// #define and #undef
//===----------------------------------------------------------------------===//

void Preprocessor::handleDefineDirective(Token &DefineTok) {
  Token MacroNameTok;
  if (!lexFromLexer(MacroNameTok)) {
    Diags.report(DefineTok.getLocation(), DiagLevel::Error, "expected macro name after #define");
    return;
  }

  if (!MacroNameTok.is(TokenKind::identifier)) {
    Diags.report(MacroNameTok.getLocation(), DiagLevel::Error, "macro name must be an identifier");
    return;
  }

  StringRef MacroName = MacroNameTok.getText();
  auto MI = parseMacroDefinition(MacroNameTok);
  if (MI) {
    Macros[MacroName] = std::move(MI);
  }
}

std::unique_ptr<MacroInfo> Preprocessor::parseMacroDefinition(Token &MacroNameTok) {
  auto MI = std::make_unique<MacroInfo>(MacroNameTok.getLocation());

  Token NextTok;
  if (CurLexer->peekNextToken().is(TokenKind::l_paren)) {
    MI->setFunctionLike(true);
    lexFromLexer(NextTok);

    while (true) {
      if (!lexFromLexer(NextTok)) {
        Diags.report(MacroNameTok.getLocation(), DiagLevel::Error, "unterminated macro parameter list");
        return nullptr;
      }

      if (NextTok.is(TokenKind::r_paren)) {
        break;
      }

      if (NextTok.is(TokenKind::identifier)) {
        MI->addParameter(NextTok.getText());
      } else if (NextTok.is(TokenKind::ellipsis)) {
        MI->setVariadic(true);
      }

      if (CurLexer->peekNextToken().is(TokenKind::comma)) {
        lexFromLexer(NextTok);
      }
    }
  }

  while (true) {
    Token Tok;
    if (!lexFromLexer(Tok)) {
      break;
    }

    if (Tok.is(TokenKind::eod) || Tok.is(TokenKind::eof)) {
      break;
    }

    MI->addToken(Tok);
  }

  return MI;
}

void Preprocessor::handleUndefDirective(Token &UndefTok) {
  Token MacroNameTok;
  if (!lexFromLexer(MacroNameTok)) {
    Diags.report(UndefTok.getLocation(), DiagLevel::Error, "expected macro name after #undef");
    return;
  }

  if (!MacroNameTok.is(TokenKind::identifier)) {
    Diags.report(MacroNameTok.getLocation(), DiagLevel::Error, "macro name must be an identifier");
    return;
  }

  undefMacro(MacroNameTok.getText());
}

//===----------------------------------------------------------------------===//
// #include
//===----------------------------------------------------------------------===//

void Preprocessor::handleIncludeDirective(Token &IncludeTok, bool IsAngled) {
  Token FilenameTok;
  if (!lexFromLexer(FilenameTok)) {
    Diags.report(IncludeTok.getLocation(), DiagLevel::Error, "expected filename after #include");
    return;
  }

  StringRef Filename;
  if (FilenameTok.isStringLiteral()) {
    Filename = FilenameTok.getText();
    if (Filename.size() >= 2) {
      Filename = Filename.substr(1, Filename.size() - 2);
    }
  } else {
    Diags.report(FilenameTok.getLocation(), DiagLevel::Error, "expected filename string");
    return;
  }

  Diags.report(IncludeTok.getLocation(), DiagLevel::Warning, "#include not fully implemented");
}

//===----------------------------------------------------------------------===//
// #embed (C++26)
//===----------------------------------------------------------------------===//

void Preprocessor::handleEmbedDirective(Token &EmbedTok) {
  Token FilenameTok;
  if (!lexFromLexer(FilenameTok)) {
    Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "expected filename after #embed");
    return;
  }

  Diags.report(EmbedTok.getLocation(), DiagLevel::Warning, "#embed not implemented yet");
}

//===----------------------------------------------------------------------===//
// Conditional compilation
//===----------------------------------------------------------------------===//

void Preprocessor::handleIfDirective(Token &IfTok) {
  std::vector<Token> ConditionTokens;
  while (true) {
    Token Tok;
    if (!lexFromLexer(Tok) || Tok.is(TokenKind::eod) || Tok.is(TokenKind::eof)) {
      break;
    }
    ConditionTokens.push_back(Tok);
  }

  bool Condition = evaluateCondition(ConditionTokens);

  ConditionalInfo CI;
  CI.WasSkipping = Skipping;
  CI.FoundNonSkip = Condition;
  CI.FoundElse = false;
  CI.IfLoc = IfTok.getLocation();

  Skipping = Skipping || !Condition;
  ConditionalStack.push_back(CI);
}

void Preprocessor::handleIfdefDirective(Token &IfdefTok) {
  Token MacroNameTok;
  if (!lexFromLexer(MacroNameTok)) {
    Diags.report(IfdefTok.getLocation(), DiagLevel::Error, "expected macro name after #ifdef");
    return;
  }

  bool IsDefined = isMacroDefined(MacroNameTok.getText());

  // Skip to end of directive
  Token Tok;
  while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}

  ConditionalInfo CI;
  CI.WasSkipping = Skipping;
  CI.FoundNonSkip = IsDefined;
  CI.FoundElse = false;
  CI.IfLoc = IfdefTok.getLocation();

  Skipping = Skipping || !IsDefined;
  ConditionalStack.push_back(CI);
}

void Preprocessor::handleIfndefDirective(Token &IfndefTok) {
  Token MacroNameTok;
  if (!lexFromLexer(MacroNameTok)) {
    Diags.report(IfndefTok.getLocation(), DiagLevel::Error, "expected macro name after #ifndef");
    return;
  }

  bool IsNotDefined = !isMacroDefined(MacroNameTok.getText());

  // Skip to end of directive
  Token Tok;
  while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}

  ConditionalInfo CI;
  CI.WasSkipping = Skipping;
  CI.FoundNonSkip = IsNotDefined;
  CI.FoundElse = false;
  CI.IfLoc = IfndefTok.getLocation();

  Skipping = Skipping || !IsNotDefined;
  ConditionalStack.push_back(CI);
}

void Preprocessor::handleElifDirective(Token &ElifTok) {
  if (ConditionalStack.empty()) {
    Diags.report(ElifTok.getLocation(), DiagLevel::Error, "#elif without #if");
    return;
  }

  ConditionalInfo &CI = ConditionalStack.back();
  if (CI.FoundElse) {
    Diags.report(ElifTok.getLocation(), DiagLevel::Error, "#elif after #else");
    return;
  }

  if (CI.WasSkipping || CI.FoundNonSkip) {
    Skipping = true;
  } else {
    std::vector<Token> ConditionTokens;
    while (true) {
      Token Tok;
      if (!lexFromLexer(Tok) || Tok.is(TokenKind::eod) || Tok.is(TokenKind::eof)) {
        break;
      }
      ConditionTokens.push_back(Tok);
    }

    bool Condition = evaluateCondition(ConditionTokens);
    CI.FoundNonSkip = Condition;
    Skipping = !Condition;
  }
}

void Preprocessor::handleElseDirective(Token &ElseTok) {
  if (ConditionalStack.empty()) {
    Diags.report(ElseTok.getLocation(), DiagLevel::Error, "#else without #if");
    return;
  }

  ConditionalInfo &CI = ConditionalStack.back();
  if (CI.FoundElse) {
    Diags.report(ElseTok.getLocation(), DiagLevel::Error, "duplicate #else");
    return;
  }

  CI.FoundElse = true;
  Skipping = CI.WasSkipping || CI.FoundNonSkip;

  // Skip to end of directive
  Token Tok;
  while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}
}

void Preprocessor::handleEndifDirective(Token &EndifTok) {
  if (ConditionalStack.empty()) {
    Diags.report(EndifTok.getLocation(), DiagLevel::Error, "#endif without #if");
    return;
  }

  ConditionalInfo CI = ConditionalStack.back();
  ConditionalStack.pop_back();
  Skipping = CI.WasSkipping;

  // Skip to end of directive
  Token Tok;
  while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}
}

bool Preprocessor::evaluateCondition(ArrayRef<Token> Tokens) {
  if (Tokens.empty()) {
    return false;
  }

  if (Tokens.size() >= 3 && Tokens[0].is(TokenKind::identifier) &&
      Tokens[0].getText() == "defined") {
    if (Tokens[1].is(TokenKind::l_paren) && Tokens[2].is(TokenKind::identifier)) {
      return isMacroDefined(Tokens[2].getText());
    }
    if (Tokens[1].is(TokenKind::identifier)) {
      return isMacroDefined(Tokens[1].getText());
    }
  }

  if (Tokens.size() == 1 && Tokens[0].is(TokenKind::identifier)) {
    StringRef Name = Tokens[0].getText();
    if (isMacroDefined(Name)) {
      return true;
    }
    return false;
  }

  if (Tokens.size() == 1 && Tokens[0].isNumericConstant()) {
    StringRef Text = Tokens[0].getText();
    unsigned long long Value = 0;
    if (!Text.getAsInteger(10, Value)) {
      return Value != 0;
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Other directives
//===----------------------------------------------------------------------===//

void Preprocessor::handleErrorDirective(Token &ErrorTok) {
  std::string Message;
  while (true) {
    Token Tok;
    if (!lexFromLexer(Tok) || Tok.is(TokenKind::eod) || Tok.is(TokenKind::eof)) {
      break;
    }
    if (!Message.empty()) {
      Message += " ";
    }
    Message += Tok.getText().str();
  }

  Diags.report(ErrorTok.getLocation(), DiagLevel::Error, Message);
}

void Preprocessor::handleWarningDirective(Token &WarningTok) {
  std::string Message;
  while (true) {
    Token Tok;
    if (!lexFromLexer(Tok) || Tok.is(TokenKind::eod) || Tok.is(TokenKind::eof)) {
      break;
    }
    if (!Message.empty()) {
      Message += " ";
    }
    Message += Tok.getText().str();
  }

  Diags.report(WarningTok.getLocation(), DiagLevel::Warning, Message);
}

void Preprocessor::handlePragmaDirective(Token &PragmaTok) {
  while (true) {
    Token Tok;
    if (!lexFromLexer(Tok) || Tok.is(TokenKind::eod) || Tok.is(TokenKind::eof)) {
      break;
    }
  }
}

void Preprocessor::handleLineDirective(Token &LineTok) {
  while (true) {
    Token Tok;
    if (!lexFromLexer(Tok) || Tok.is(TokenKind::eod) || Tok.is(TokenKind::eof)) {
      break;
    }
  }
}

//===----------------------------------------------------------------------===//
// Macro expansion
//===----------------------------------------------------------------------===//

bool Preprocessor::expandMacro(Token &Result, StringRef MacroName, MacroInfo *MI) {
  MI->setUsed(true);

  if (MI->isFunctionLike()) {
    Token NextTok = CurLexer->peekNextToken();
    if (!NextTok.is(TokenKind::l_paren)) {
      return false;
    }

    auto Args = parseMacroArguments(MI);
    std::vector<Token> Replacement = MI->getReplacementTokens();
    substituteParameters(Replacement, MI, Args);

    if (!Replacement.empty()) {
      Result = Replacement[0];
    }
  } else {
    const auto &Tokens = MI->getReplacementTokens();
    if (!Tokens.empty()) {
      Result = Tokens[0];
    }
  }

  return true;
}

std::vector<std::vector<Token>> Preprocessor::parseMacroArguments(MacroInfo *MI) {
  std::vector<std::vector<Token>> Args;

  Token Tok;
  lexFromLexer(Tok);

  unsigned ParenDepth = 1;
  std::vector<Token> CurrentArg;

  while (ParenDepth > 0) {
    if (!lexFromLexer(Tok)) {
      Diags.report(SourceLocation(), DiagLevel::Error, "unterminated macro argument list");
      break;
    }

    if (Tok.is(TokenKind::l_paren)) {
      ++ParenDepth;
      CurrentArg.push_back(Tok);
    } else if (Tok.is(TokenKind::r_paren)) {
      --ParenDepth;
      if (ParenDepth > 0) {
        CurrentArg.push_back(Tok);
      } else {
        if (!CurrentArg.empty()) {
          Args.push_back(CurrentArg);
        }
      }
    } else if (Tok.is(TokenKind::comma) && ParenDepth == 1) {
      Args.push_back(CurrentArg);
      CurrentArg.clear();
    } else {
      CurrentArg.push_back(Tok);
    }
  }

  return Args;
}

void Preprocessor::substituteParameters(std::vector<Token> &Tokens, MacroInfo *MI,
                                        const std::vector<std::vector<Token>> &Args) {
  for (auto &Tok : Tokens) {
    if (Tok.is(TokenKind::identifier)) {
      StringRef Name = Tok.getText();
      for (unsigned i = 0; i < MI->getNumParameters(); ++i) {
        if (Name == MI->getParameterName(i) && i < Args.size()) {
          if (!Args[i].empty()) {
            Tok = Args[i][0];
          }
          break;
        }
      }
    }
  }
}

Token Preprocessor::stringifyToken(const Token &T) {
  Token Result;
  Result.setKind(TokenKind::string_literal);

  std::string Str = "\"";
  StringRef Text = T.getText();
  for (char C : Text) {
    if (C == '"' || C == '\\') {
      Str += '\\';
    }
    Str += C;
  }
  Str += "\"";

  return Result;
}

Token Preprocessor::concatenateTokens(const Token &T1, const Token &T2) {
  Token Result;
  std::string Combined = T1.getText().str() + T2.getText().str();

  Result.setKind(TokenKind::identifier);
  Result.setLiteralData(Combined.c_str());
  Result.setLength(static_cast<unsigned>(Combined.size()));

  return Result;
}

} // namespace blocktype
