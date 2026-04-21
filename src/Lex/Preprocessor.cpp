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
#include "blocktype/Lex/HeaderSearch.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/FileManager.h"
#include "blocktype/Basic/FileEntry.h"
#include <unordered_map>
#include <ctime>
#include <sstream>

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
    {"否则如果定义", "elifdef"},
    {"否则如果未定义", "elifndef"},
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
                           HeaderSearch *HS, LanguageManager *LM,
                           FileManager *FM)
    : SM(SM), Diags(Diags), Headers(HS), LangMgr(LM), FileMgr(FM) {
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

  // B3.8: __DATE__ and __TIME__ predefined macros
  // Get current time
  std::time_t Now = std::time(nullptr);
  std::tm *Tm = std::localtime(&Now);

  // __DATE__: "Mmm dd yyyy"
  char DateBuf[16];
  std::strftime(DateBuf, sizeof(DateBuf), "\"%b %d %Y\"", Tm);
  definePredefinedMacro("__DATE__", DateBuf);  // Include quotes

  // __TIME__: "hh:mm:ss"
  char TimeBuf[16];
  std::strftime(TimeBuf, sizeof(TimeBuf), "\"%H:%M:%S\"", Tm);
  definePredefinedMacro("__TIME__", TimeBuf);  // Include quotes

  // Register __FILE__ and __LINE__ as special macros
  // They are handled specially in expandMacro()
  auto FileMI = std::make_unique<MacroInfo>(SourceLocation());
  FileMI->setPredefined(true);
  Macros["__FILE__"] = std::move(FileMI);

  auto LineMI = std::make_unique<MacroInfo>(SourceLocation());
  LineMI->setPredefined(true);
  Macros["__LINE__"] = std::move(LineMI);

  // A3.4/A3.5: Register __has_include and __has_embed as function-like macros
  auto HasIncludeMI = std::make_unique<MacroInfo>(SourceLocation());
  HasIncludeMI->setPredefined(true);
  HasIncludeMI->setFunctionLike(true);
  Macros["__has_include"] = std::move(HasIncludeMI);

  auto HasEmbedMI = std::make_unique<MacroInfo>(SourceLocation());
  HasEmbedMI->setPredefined(true);
  HasEmbedMI->setFunctionLike(true);
  Macros["__has_embed"] = std::move(HasEmbedMI);
}

void Preprocessor::definePredefinedMacro(StringRef Name, StringRef Value) {
  auto MI = std::make_unique<MacroInfo>(SourceLocation());
  MI->setPredefined(true);

  Token T;
  // Determine if value is a string literal (starts with ")
  if (Value.startswith("\"")) {
    T.setKind(TokenKind::string_literal);
  } else {
    T.setKind(TokenKind::numeric_constant);
  }
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

//===----------------------------------------------------------------------===//
// Token buffer operations
//===----------------------------------------------------------------------===//

Token Preprocessor::peekToken(unsigned N) {
  // Fill buffer if needed
  while (TokenBuffer.size() - TokenBufferIndex <= N) {
    Token Tok;
    if (!lexFromLexer(Tok)) {
      Tok.setKind(TokenKind::eof);
    }
    TokenBuffer.push_back(Tok);
    if (Tok.is(TokenKind::eof)) {
      break;
    }
  }
  
  size_t Index = TokenBufferIndex + N;
  if (Index < TokenBuffer.size()) {
    return TokenBuffer[Index];
  }
  
  // Return EOF if out of range
  Token EofTok;
  EofTok.setKind(TokenKind::eof);
  return EofTok;
}

bool Preprocessor::consumeToken(Token &Result) {
  // Fill buffer if empty - this ensures TentativeParsingAction can work
  if (TokenBufferIndex >= TokenBuffer.size()) {
    Token Tok;
    if (!lexFromLexer(Tok)) {
      Tok.setKind(TokenKind::eof);
    }
    TokenBuffer.push_back(Tok);
  }

  // Return from buffer
  Result = TokenBuffer[TokenBufferIndex++];

  // Note: Don't clear the buffer here! TentativeParsingAction needs to restore
  // the TokenBufferIndex, so we need to keep the tokens in the buffer.
  // The buffer will be cleared when we know we won't need to backtrack.

  return !Result.is(TokenKind::eof);
}

void Preprocessor::restoreTokenBufferState(size_t SavedIndex) {
  // Restore the token buffer index to the saved state
  // This allows backtracking during tentative parsing
  if (SavedIndex <= TokenBuffer.size()) {
    TokenBufferIndex = SavedIndex;
  }
}

void Preprocessor::clearTokenBuffer() {
  // Clear all pending tokens from the buffer
  TokenBuffer.clear();
  TokenBufferIndex = 0;
}

void Preprocessor::reset() {
  // Clear include stack
  IncludeStack.clear();
  CurLexer = nullptr;
  
  // Clear token buffer
  clearTokenBuffer();
  
  // Reset conditional compilation state
  ConditionalStack.clear();
  Skipping = false;
  
  // Reset file context
  CurrentFilename = StringRef();
  CurrentLine = 1;
  CurrentFileID = static_cast<unsigned>(-1);
  
  // Note: We preserve Macros because macro definitions should be shared
  // across multiple files in multi-file compilation.
  // If you need to clear macros, call undefMacro() explicitly.
  
  // Clear include guards (file-specific)
  FileIncludeGuards.clear();
  PendingIncludeGuard = StringRef();
  
  // Clear pragma once tracking (file-specific)
  PragmaOnceFiles.clear();
}

void Preprocessor::enterSourceFile(StringRef Filename, StringRef Content) {
  auto Lex = std::make_unique<Lexer>(SM, Diags, Content, SM.createMainFileID(Filename, Content));

  IncludeStackEntry Entry;
  Entry.Lex = std::move(Lex);
  Entry.Filename = Filename;
  IncludeStack.push_back(std::move(Entry));
  CurLexer = IncludeStack.back().Lex.get();
  
  // Update current filename for __FILE__
  CurrentFilename = Filename;
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
    CurLexer->setInPreprocessorDirective(false);
    return;
  }

  if (DirectiveName == "include") {
    handleIncludeDirective(DirectiveTok, false);
  } else if (DirectiveName == "include_next") {
    handleIncludeNextDirective(DirectiveTok, false);
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
  } else if (DirectiveName == "elifdef") {
    handleElifdefDirective(DirectiveTok);
  } else if (DirectiveName == "elifndef") {
    handleElifndefDirective(DirectiveTok);
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

  // Reset preprocessor directive mode
  CurLexer->setInPreprocessorDirective(false);
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
  } else if (EnName == "elifdef") {
    handleElifdefDirective(DirectiveTok);
  } else if (EnName == "elifndef") {
    handleElifndefDirective(DirectiveTok);
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

  // D11: Check if this is part of an include guard pattern
  if (!PendingIncludeGuard.empty() && MacroName == PendingIncludeGuard &&
      CurrentFileID != static_cast<unsigned>(-1)) {
    auto It = FileIncludeGuards.find(CurrentFileID);
    if (It != FileIncludeGuards.end() && It->second.IsValid && !It->second.SawDefine) {
      It->second.SawDefine = true;
      // Mark this file as having an include guard
      if (Headers) {
        Headers->markHasIncludeGuard(CurrentFilename);
      }
    }
    PendingIncludeGuard = StringRef();
  }

  // Check for predefined macros
  if (PredefinedMacros.count(MacroName)) {
    Diags.report(MacroNameTok.getLocation(), DiagLevel::Warning,
                 "redefining predefined macro '" + MacroName.str() + "'");
  }

  // Check for existing macro definition
  auto ExistingIt = Macros.find(MacroName);
  if (ExistingIt != Macros.end()) {
    // Parse new definition
    auto NewMI = parseMacroDefinition(MacroNameTok);
    if (!NewMI) return;

    // Compare with existing definition
    MacroInfo *Existing = ExistingIt->second.get();
    bool IsDifferent = false;

    // Check if function-like status differs
    if (Existing->isFunctionLike() != NewMI->isFunctionLike()) {
      IsDifferent = true;
    }
    // Check if parameter count differs
    else if (Existing->isFunctionLike() &&
             Existing->getNumParameters() != NewMI->getNumParameters()) {
      IsDifferent = true;
    }
    // Check if replacement tokens differ
    else if (Existing->getReplacementTokens().size() != NewMI->getReplacementTokens().size()) {
      IsDifferent = true;
    }
    else {
      // Compare each token
      const auto& ExistingTokens = Existing->getReplacementTokens();
      const auto& NewTokens = NewMI->getReplacementTokens();
      for (unsigned I = 0; I < ExistingTokens.size(); ++I) {
        if (ExistingTokens[I].getText() != NewTokens[I].getText()) {
          IsDifferent = true;
          break;
        }
      }
    }

    if (IsDifferent) {
      Diags.report(MacroNameTok.getLocation(), DiagLevel::Warning,
                   "macro '" + MacroName.str() + "' redefined");
    }

    // Update the macro
    Macros[MacroName] = std::move(NewMI);
  } else {
    // Parse and add new macro
    auto MI = parseMacroDefinition(MacroNameTok);
    if (MI) {
      Macros[MacroName] = std::move(MI);
    }
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
    // Handle: #include "file.h"
    Filename = FilenameTok.getText();
    if (Filename.size() >= 2) {
      Filename = Filename.substr(1, Filename.size() - 2);
    }
  } else if (FilenameTok.is(TokenKind::less)) {
    // P7.4.3: Handle: #include <file.h>
    // Collect tokens until '>'
    std::string AngledName;
    Token NextTok;
    while (lexFromLexer(NextTok)) {
      if (NextTok.is(TokenKind::greater)) {
        break;
      }
      AngledName += NextTok.getText().str();
    }
    Filename = AngledName;
  } else {
    Diags.report(FilenameTok.getLocation(), DiagLevel::Error, "expected filename string");
    return;
  }

  // Check for circular inclusion
  for (const auto &Entry : IncludeStack) {
    if (Entry.Filename == Filename) {
      Diags.report(IncludeTok.getLocation(), DiagLevel::Error,
                   "circular inclusion detected: " + Filename.str());
      return;
    }
  }

  // Try to find the file
  if (!Headers || !FileMgr) {
    Diags.report(IncludeTok.getLocation(), DiagLevel::Warning,
                 "#include not fully implemented: missing HeaderSearch or FileManager");
    return;
  }

  // B3.5: Try relative path first (from current file directory)
  const FileEntry *FE = nullptr;
  if (!IsAngled && !IncludeStack.empty()) {
    StringRef CurrentFile = IncludeStack.back().Filename;
    // Find directory part
    size_t LastSlash = CurrentFile.rfind('/');
    if (LastSlash != StringRef::npos) {
      StringRef IncludeDir = CurrentFile.substr(0, LastSlash);
      FE = Headers->lookupHeader(Filename, false, IncludeDir);
    }
  }

  // Try system search paths
  if (!FE) {
    FE = Headers->lookupHeader(Filename, IsAngled);
  }

  if (!FE) {
    Diags.report(IncludeTok.getLocation(), DiagLevel::Error,
                 "file not found: " + Filename.str());
    return;
  }

  // Check if this file has been included with #pragma once
  const FileInfo *FI = SM.getFileInfo(FE->getPath());
  if (FI) {
    unsigned FileID = FI->getFileID();
    if (PragmaOnceFiles.count(FileID) > 0) {
      // File has #pragma once and was already included, skip
      return;
    }
  }

  // Read the file content
  auto Buffer = FileMgr->getBuffer(FE->getPath());
  if (!Buffer) {
    Diags.report(IncludeTok.getLocation(), DiagLevel::Error,
                 "cannot read file: " + Filename.str());
    return;
  }

  // Create a new lexer for the included file
  StringRef Content = Buffer->getBuffer();
  SourceLocation IncludeLoc = SM.createFileID(Filename, Content);
  auto Lex = std::make_unique<Lexer>(SM, Diags, Content, IncludeLoc);

  // Push onto include stack
  IncludeStackEntry Entry;
  Entry.Lex = std::move(Lex);
  Entry.IncludeLoc = IncludeTok.getLocation();
  Entry.Filename = Filename;
  IncludeStack.push_back(std::move(Entry));
  CurLexer = IncludeStack.back().Lex.get();

  // Update current filename for __FILE__
  CurrentFilename = Filename;
  CurrentLine = 1;  // Reset line number
  CurrentFileID = IncludeLoc.getFileID();  // Track current file ID for #pragma once

  // Mark as included for #pragma once
  Headers->markIncluded(Filename);
}

// A3.1: #include_next (GNU extension)
void Preprocessor::handleIncludeNextDirective(Token &IncludeTok, bool IsAngled) {
  Token FilenameTok;
  if (!lexFromLexer(FilenameTok)) {
    Diags.report(IncludeTok.getLocation(), DiagLevel::Error, "expected filename after #include_next");
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

  if (!Headers || !FileMgr) {
    Diags.report(IncludeTok.getLocation(), DiagLevel::Warning,
                 "#include_next not fully implemented: missing HeaderSearch or FileManager");
    return;
  }

  // #include_next starts searching from the next directory in the search path
  // after the one where the current file was found
  const FileEntry *FE = Headers->lookupHeaderNext(Filename, CurrentFilename);
  if (!FE) {
    Diags.report(IncludeTok.getLocation(), DiagLevel::Error,
                 "file not found: " + Filename.str());
    return;
  }

  auto Buffer = FileMgr->getBuffer(FE->getPath());
  if (!Buffer) {
    Diags.report(IncludeTok.getLocation(), DiagLevel::Error,
                 "cannot read file: " + Filename.str());
    return;
  }

  StringRef Content = Buffer->getBuffer();
  SourceLocation IncludeLoc = SM.createFileID(Filename, Content);
  auto Lex = std::make_unique<Lexer>(SM, Diags, Content, IncludeLoc);

  IncludeStackEntry Entry;
  Entry.Lex = std::move(Lex);
  Entry.IncludeLoc = IncludeTok.getLocation();
  Entry.Filename = Filename;
  IncludeStack.push_back(std::move(Entry));
  CurLexer = IncludeStack.back().Lex.get();

  CurrentFilename = Filename;
  CurrentLine = 1;
  Headers->markIncluded(Filename);
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

  // A3.2: Parse optional parameters: limit(n), suffix(s)
  unsigned Limit = 0;  // 0 means no limit
  std::string Suffix;

  while (true) {
    Token Tok;
    if (!lexFromLexer(Tok) || Tok.is(TokenKind::eod) || Tok.is(TokenKind::eof)) {
      break;
    }

    if (Tok.is(TokenKind::identifier)) {
      StringRef ParamName = Tok.getText();

      if (ParamName == "limit") {
        // Parse limit(n)
        if (!lexFromLexer(Tok) || !Tok.is(TokenKind::l_paren)) {
          Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "expected '(' after limit");
          return;
        }
        if (!lexFromLexer(Tok) || !Tok.is(TokenKind::numeric_constant)) {
          Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "expected number in limit()");
          return;
        }
        Limit = std::stoul(Tok.getText().str());
        if (!lexFromLexer(Tok) || !Tok.is(TokenKind::r_paren)) {
          Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "expected ')' after limit value");
          return;
        }
      } else if (ParamName == "suffix") {
        // Parse suffix(s)
        if (!lexFromLexer(Tok) || !Tok.is(TokenKind::l_paren)) {
          Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "expected '(' after suffix");
          return;
        }
        if (!lexFromLexer(Tok) || !Tok.isStringLiteral()) {
          Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "expected string in suffix()");
          return;
        }
        Suffix = Tok.getText().str();
        if (Suffix.size() >= 2) {
          Suffix = Suffix.substr(1, Suffix.size() - 2);  // Remove quotes
        }
        if (!lexFromLexer(Tok) || !Tok.is(TokenKind::r_paren)) {
          Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "expected ')' after suffix value");
          return;
        }
      }
    }
  }

  // Find and read the file
  if (!Headers || !FileMgr) {
    Diags.report(EmbedTok.getLocation(), DiagLevel::Warning,
                 "#embed not fully implemented: missing HeaderSearch or FileManager");
    return;
  }

  const FileEntry *FE = Headers->lookupHeader(Filename, false);
  if (!FE) {
    Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "file not found: " + Filename.str());
    return;
  }

  auto Buffer = FileMgr->getBuffer(FE->getPath());
  if (!Buffer) {
    Diags.report(EmbedTok.getLocation(), DiagLevel::Error, "cannot read file: " + Filename.str());
    return;
  }

  StringRef Content = Buffer->getBuffer();
  if (Limit > 0 && Content.size() > Limit) {
    Content = Content.substr(0, Limit);
  }

  // Generate braced-init-list of bytes
  // e.g., #embed "data.bin" -> { 0x48, 0x65, 0x6c, 0x6c, 0x6f }
  std::string EmbedData = "{ ";
  for (size_t i = 0; i < Content.size(); ++i) {
    if (i > 0) EmbedData += ", ";
    EmbedData += "0x" + std::to_string(static_cast<unsigned char>(Content[i]));
  }
  EmbedData += " }";

  // Append suffix if specified
  if (!Suffix.empty()) {
    EmbedData += " " + Suffix;
  }

  // Store the embed data as a token sequence
  // Create a temporary lexer to tokenize the generated braced-init-list
  // and push it onto the include stack
  
  // Create a copy of the embed data string (needs to persist)
  std::string *EmbedDataCopy = new std::string(std::move(EmbedData));
  
  // Create a virtual file ID for the embed data
  SourceLocation EmbedLoc = EmbedTok.getLocation();
  
  // Create a new Lexer for the embed data
  auto EmbedLexer = std::make_unique<Lexer>(SM, Diags, *EmbedDataCopy, 
                                              SM.createFileID("<embed>", *EmbedDataCopy));
  
  // Push current lexer onto stack
  if (CurLexer) {
    IncludeStackEntry Entry;
    Entry.Lex = std::unique_ptr<Lexer>(CurLexer);
    Entry.IncludeLoc = EmbedLoc;
    Entry.Filename = "<embed>";
    IncludeStack.push_back(std::move(Entry));
  }
  
  // Set the embed lexer as current
  CurLexer = EmbedLexer.release();
  
  // Store the embed data string for cleanup
  // (In a production implementation, we'd use a proper memory management system)
  
  Diags.report(EmbedLoc, DiagLevel::Warning,
               "#embed: generated " + std::to_string(Content.size()) + " bytes as braced-init-list");
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

  StringRef MacroName = MacroNameTok.getText();
  bool IsNotDefined = !isMacroDefined(MacroName);

  // D11: Include guard detection - check if this is first #ifndef in file
  if (!Skipping && ConditionalStack.empty() && CurrentFileID != static_cast<unsigned>(-1)) {
    // Check if we're at the start of the file (no tokens processed yet)
    // This is a potential include guard
    PendingIncludeGuard = MacroName;
    FileIncludeGuards[CurrentFileID] = IncludeGuardInfo{
      MacroName,
      IfndefTok.getLocation(),
      true,
      false
    };
  }

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

void Preprocessor::handleElifdefDirective(Token &ElifdefTok) {
  if (ConditionalStack.empty()) {
    Diags.report(ElifdefTok.getLocation(), DiagLevel::Error, "#elifdef without #if");
    return;
  }

  ConditionalInfo &CI = ConditionalStack.back();
  if (CI.FoundElse) {
    Diags.report(ElifdefTok.getLocation(), DiagLevel::Error, "#elifdef after #else");
    return;
  }

  // Lex the macro name
  Token MacroNameTok;
  if (!lexFromLexer(MacroNameTok)) {
    Diags.report(ElifdefTok.getLocation(), DiagLevel::Error,
                 "expected macro name after #elifdef");
    return;
  }

  bool IsDefined = isMacroDefined(MacroNameTok.getText());

  // Skip to end of directive
  Token Tok;
  while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}

  if (CI.WasSkipping || CI.FoundNonSkip) {
    Skipping = true;
  } else {
    CI.FoundNonSkip = IsDefined;
    Skipping = !IsDefined;
  }
}

void Preprocessor::handleElifndefDirective(Token &ElifndefTok) {
  if (ConditionalStack.empty()) {
    Diags.report(ElifndefTok.getLocation(), DiagLevel::Error, "#elifndef without #if");
    return;
  }

  ConditionalInfo &CI = ConditionalStack.back();
  if (CI.FoundElse) {
    Diags.report(ElifndefTok.getLocation(), DiagLevel::Error, "#elifndef after #else");
    return;
  }

  // Lex the macro name
  Token MacroNameTok;
  if (!lexFromLexer(MacroNameTok)) {
    Diags.report(ElifndefTok.getLocation(), DiagLevel::Error,
                 "expected macro name after #elifndef");
    return;
  }

  bool IsNotDefined = !isMacroDefined(MacroNameTok.getText());

  // Skip to end of directive
  Token Tok;
  while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}

  if (CI.WasSkipping || CI.FoundNonSkip) {
    Skipping = true;
  } else {
    CI.FoundNonSkip = IsNotDefined;
    Skipping = !IsNotDefined;
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

//===----------------------------------------------------------------------===//
// Preprocessor Expression Evaluation
//===----------------------------------------------------------------------===//

namespace {

/// PPExprEvaluator - Recursive descent parser for preprocessor expressions.
///
/// Supports C++ preprocessor expression syntax including:
/// - Integer literals (decimal, hex, octal, binary)
/// - defined operator
/// - Unary operators: !, ~, -, +
/// - Binary operators: *, /, %, +, -, <<, >>, <, >, <=, >=, ==, !=, &, ^, |, &&, ||
/// - Ternary conditional: ?:
/// - Parentheses for grouping
class PPExprEvaluator {
  ArrayRef<Token> Tokens;
  size_t Pos = 0;
  Preprocessor &PP;

public:
  PPExprEvaluator(ArrayRef<Token> Toks, Preprocessor &P)
      : Tokens(Toks), PP(P) {}

  /// Evaluates the full expression.
  long long evaluate() {
    long long Result = parseConditionalExpr();
    return Result;
  }

private:
  /// Returns true if at end of tokens.
  bool isAtEnd() const { return Pos >= Tokens.size(); }

  /// Returns the current token.
  const Token &peek() const {
    static Token EmptyToken;
    return isAtEnd() ? EmptyToken : Tokens[Pos];
  }

  /// Consumes and returns the current token.
  Token consume() {
    if (isAtEnd()) return Token();
    return Tokens[Pos++];
  }

  /// Checks if current token matches the given kind.
  bool check(TokenKind Kind) const {
    return !isAtEnd() && peek().is(Kind);
  }

  /// Consumes token if it matches, returns true if consumed.
  bool match(TokenKind Kind) {
    if (check(Kind)) {
      consume();
      return true;
    }
    return false;
  }

  /// Parses a conditional expression (?: operator).
  long long parseConditionalExpr() {
    long long Cond = parseLogicalOrExpr();
    if (match(TokenKind::question)) {
      long long TrueVal = parseConditionalExpr();
      if (!match(TokenKind::colon)) {
        PP.getDiagnostics().report(peek().getLocation(), DiagLevel::Error,
                                   "expected ':' in conditional expression");
        return 0;
      }
      long long FalseVal = parseConditionalExpr();
      return Cond ? TrueVal : FalseVal;
    }
    return Cond;
  }

  /// Parses logical OR expression (||).
  long long parseLogicalOrExpr() {
    long long Left = parseLogicalAndExpr();
    while (match(TokenKind::pipepipe)) {
      long long Right = parseLogicalAndExpr();
      Left = Left || Right;
    }
    return Left;
  }

  /// Parses logical AND expression (&&).
  long long parseLogicalAndExpr() {
    long long Left = parseBitwiseOrExpr();
    while (match(TokenKind::ampamp)) {
      long long Right = parseBitwiseOrExpr();
      Left = Left && Right;
    }
    return Left;
  }

  /// Parses bitwise OR expression (|).
  long long parseBitwiseOrExpr() {
    long long Left = parseBitwiseXorExpr();
    while (match(TokenKind::pipe)) {
      long long Right = parseBitwiseXorExpr();
      Left = Left | Right;
    }
    return Left;
  }

  /// Parses bitwise XOR expression (^).
  long long parseBitwiseXorExpr() {
    long long Left = parseBitwiseAndExpr();
    while (match(TokenKind::caret)) {
      long long Right = parseBitwiseAndExpr();
      Left = Left ^ Right;
    }
    return Left;
  }

  /// Parses bitwise AND expression (&).
  long long parseBitwiseAndExpr() {
    long long Left = parseEqualityExpr();
    while (match(TokenKind::amp)) {
      long long Right = parseEqualityExpr();
      Left = Left & Right;
    }
    return Left;
  }

  /// Parses equality expression (==, !=).
  long long parseEqualityExpr() {
    long long Left = parseRelationalExpr();
    while (true) {
      if (match(TokenKind::equalequal)) {
        long long Right = parseRelationalExpr();
        Left = Left == Right;
      } else if (match(TokenKind::exclaimequal)) {
        long long Right = parseRelationalExpr();
        Left = Left != Right;
      } else {
        break;
      }
    }
    return Left;
  }

  /// Parses relational expression (<, >, <=, >=).
  long long parseRelationalExpr() {
    long long Left = parseShiftExpr();
    while (true) {
      if (match(TokenKind::less)) {
        long long Right = parseShiftExpr();
        Left = Left < Right;
      } else if (match(TokenKind::greater)) {
        long long Right = parseShiftExpr();
        Left = Left > Right;
      } else if (match(TokenKind::lessequal)) {
        long long Right = parseShiftExpr();
        Left = Left <= Right;
      } else if (match(TokenKind::greaterequal)) {
        long long Right = parseShiftExpr();
        Left = Left >= Right;
      } else {
        break;
      }
    }
    return Left;
  }

  /// Parses shift expression (<<, >>).
  long long parseShiftExpr() {
    long long Left = parseAdditiveExpr();
    while (true) {
      if (match(TokenKind::lessless)) {
        long long Right = parseAdditiveExpr();
        Left = Left << Right;
      } else if (match(TokenKind::greatergreater)) {
        long long Right = parseAdditiveExpr();
        Left = Left >> Right;
      } else {
        break;
      }
    }
    return Left;
  }

  /// Parses additive expression (+, -).
  long long parseAdditiveExpr() {
    long long Left = parseMultiplicativeExpr();
    while (true) {
      if (match(TokenKind::plus)) {
        long long Right = parseMultiplicativeExpr();
        Left = Left + Right;
      } else if (match(TokenKind::minus)) {
        long long Right = parseMultiplicativeExpr();
        Left = Left - Right;
      } else {
        break;
      }
    }
    return Left;
  }

  /// Parses multiplicative expression (*, /, %).
  long long parseMultiplicativeExpr() {
    long long Left = parseUnaryExpr();
    while (true) {
      if (match(TokenKind::star)) {
        long long Right = parseUnaryExpr();
        Left = Left * Right;
      } else if (match(TokenKind::slash)) {
        long long Right = parseUnaryExpr();
        if (Right == 0) {
          PP.getDiagnostics().report(peek().getLocation(), DiagLevel::Error,
                                     "division by zero in preprocessor expression");
          return 0;
        }
        Left = Left / Right;
      } else if (match(TokenKind::percent)) {
        long long Right = parseUnaryExpr();
        if (Right == 0) {
          PP.getDiagnostics().report(peek().getLocation(), DiagLevel::Error,
                                     "modulo by zero in preprocessor expression");
          return 0;
        }
        Left = Left % Right;
      } else {
        break;
      }
    }
    return Left;
  }

  /// Parses unary expression (!, ~, -, +).
  long long parseUnaryExpr() {
    if (match(TokenKind::exclaim)) {
      return !parseUnaryExpr();
    }
    if (match(TokenKind::tilde)) {
      return ~parseUnaryExpr();
    }
    if (match(TokenKind::minus)) {
      return -parseUnaryExpr();
    }
    if (match(TokenKind::plus)) {
      return parseUnaryExpr();
    }
    return parsePrimaryExpr();
  }

  /// Parses primary expression (numbers, identifiers, defined, parentheses).
  long long parsePrimaryExpr() {
    // Parenthesized expression
    if (match(TokenKind::l_paren)) {
      long long Val = parseConditionalExpr();
      if (!match(TokenKind::r_paren)) {
        PP.getDiagnostics().report(peek().getLocation(), DiagLevel::Error,
                                   "expected ')' in expression");
        return 0;
      }
      return Val;
    }

    // defined operator
    if (check(TokenKind::identifier) && peek().getText() == "defined") {
      consume(); // consume 'defined'
      if (match(TokenKind::l_paren)) {
        if (!check(TokenKind::identifier)) {
          PP.getDiagnostics().report(peek().getLocation(), DiagLevel::Error,
                                     "expected identifier after 'defined('");
          return 0;
        }
        StringRef MacroName = consume().getText();
        if (!match(TokenKind::r_paren)) {
          PP.getDiagnostics().report(peek().getLocation(), DiagLevel::Error,
                                     "expected ')' after 'defined'");
          return 0;
        }
        return PP.isMacroDefined(MacroName) ? 1 : 0;
      } else if (check(TokenKind::identifier)) {
        StringRef MacroName = consume().getText();
        return PP.isMacroDefined(MacroName) ? 1 : 0;
      } else {
        PP.getDiagnostics().report(peek().getLocation(), DiagLevel::Error,
                                   "expected identifier after 'defined'");
        return 0;
      }
    }

    // Identifier (undefined macro evaluates to 0)
    if (check(TokenKind::identifier)) {
      StringRef Name = consume().getText();
      return PP.isMacroDefined(Name) ? 1 : 0;
    }

    // Numeric constant
    if (check(TokenKind::numeric_constant)) {
      return parseIntegerLiteral();
    }

    // Character constant
    if (check(TokenKind::char_constant) || check(TokenKind::utf8_char_constant)) {
      Token Tok = consume();
      StringRef Text = Tok.getText();
      // Remove quotes
      if (Text.size() >= 3) {
        Text = Text.substr(1, Text.size() - 2);
      }
      // Simple character literal evaluation
      if (!Text.empty()) {
        return static_cast<long long>(Text[0]);
      }
      return 0;
    }

    // Unknown token
    if (!isAtEnd()) {
      PP.getDiagnostics().report(peek().getLocation(), DiagLevel::Error,
                                 "invalid token in preprocessor expression");
      consume();
    }
    return 0;
  }

  /// Parses an integer literal.
  long long parseIntegerLiteral() {
    Token Tok = consume();
    StringRef Text = Tok.getText();

    // Remove suffix (u, l, ul, ll, ull, etc.)
    while (!Text.empty() && (Text.back() == 'u' || Text.back() == 'U' ||
                             Text.back() == 'l' || Text.back() == 'L')) {
      Text = Text.drop_back();
    }

    long long Value = 0;

    // Hexadecimal: 0x or 0X
    if (Text.startswith("0x") || Text.startswith("0X")) {
      Text.getAsInteger(16, Value);
    }
    // Binary: 0b or 0B
    else if (Text.startswith("0b") || Text.startswith("0B")) {
      Text.getAsInteger(2, Value);
    }
    // Octal: starts with 0
    else if (Text.startswith("0") && Text.size() > 1) {
      Text.getAsInteger(8, Value);
    }
    // Decimal
    else {
      Text.getAsInteger(10, Value);
    }

    return Value;
  }
};

} // anonymous namespace

bool Preprocessor::evaluateCondition(ArrayRef<Token> Tokens) {
  if (Tokens.empty()) {
    return false;
  }

  PPExprEvaluator Eval(Tokens, *this);
  return Eval.evaluate() != 0;
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
  // Get the pragma directive name
  Token PragmaNameTok;
  if (!lexFromLexer(PragmaNameTok) || PragmaNameTok.is(TokenKind::eod) || PragmaNameTok.is(TokenKind::eof)) {
    return;
  }

  StringRef PragmaName = PragmaNameTok.getText();

  // #pragma once - mark file as "include once"
  if (PragmaName == "once") {
    // Mark current file as "pragma once" protected
    if (CurrentFileID != static_cast<unsigned>(-1)) {
      PragmaOnceFiles.insert(CurrentFileID);
    }
    return;
  }

  // #pragma message("text")
  if (PragmaName == "message") {
    Token Tok;
    if (lexFromLexer(Tok) && Tok.is(TokenKind::l_paren)) {
      std::string Message;
      while (lexFromLexer(Tok) && !Tok.is(TokenKind::r_paren) &&
             !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {
        if (!Message.empty()) Message += " ";
        Message += Tok.getText().str();
      }
      Diags.report(PragmaTok.getLocation(), DiagLevel::Warning, Message);
    }
    return;
  }

  // #pragma warning - for compatibility, just skip
  if (PragmaName == "warning") {
    // Skip to end of directive
    Token Tok;
    while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}
    return;
  }

  // #pragma GCC / #pragma clang - for compatibility, just skip
  if (PragmaName == "GCC" || PragmaName == "clang") {
    // Skip to end of directive
    Token Tok;
    while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}
    return;
  }

  // #pragma region / #pragma endregion - for editor compatibility
  if (PragmaName == "region" || PragmaName == "endregion") {
    // Skip to end of directive
    Token Tok;
    while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}
    return;
  }

  // Unknown pragma - skip with warning
  Token Tok;
  while (lexFromLexer(Tok) && !Tok.is(TokenKind::eod) && !Tok.is(TokenKind::eof)) {}
}

void Preprocessor::handleLineDirective(Token &LineTok) {
  // Parse line number
  Token LineNumberTok;
  if (!lexFromLexer(LineNumberTok) || LineNumberTok.is(TokenKind::eod) || LineNumberTok.is(TokenKind::eof)) {
    Diags.report(LineTok.getLocation(), DiagLevel::Error, "expected line number after #line");
    return;
  }

  if (!LineNumberTok.isNumericConstant()) {
    Diags.report(LineNumberTok.getLocation(), DiagLevel::Error, "line number must be a numeric constant");
    return;
  }

  // Parse line number value
  StringRef LineText = LineNumberTok.getText();
  unsigned long long LineNum = 0;
  if (LineText.getAsInteger(10, LineNum)) {
    Diags.report(LineNumberTok.getLocation(), DiagLevel::Error, "invalid line number");
    return;
  }

  // Update current line number
  CurrentLine = static_cast<unsigned>(LineNum);

  // Parse optional filename
  Token FilenameTok;
  if (lexFromLexer(FilenameTok) && !FilenameTok.is(TokenKind::eod) && !FilenameTok.is(TokenKind::eof)) {
    if (FilenameTok.isStringLiteral()) {
      StringRef Filename = FilenameTok.getText();
      // Remove quotes
      if (Filename.size() >= 2) {
        Filename = Filename.substr(1, Filename.size() - 2);
      }
      // Update current filename for __FILE__
      CurrentFilename = Filename;
    } else {
      Diags.report(FilenameTok.getLocation(), DiagLevel::Error, "expected filename string after line number");
    }
  }
}

//===----------------------------------------------------------------------===//
// Macro expansion
//===----------------------------------------------------------------------===//

bool Preprocessor::expandMacro(Token &Result, StringRef MacroName, MacroInfo *MI) {
  // E2: Record macro expansion location
  SourceLocation ExpansionLoc = Result.getLocation();
  SourceLocation SpellingLoc = MI->getDefinitionLocation();

  // Handle special predefined macros
  if (MacroName == "__FILE__") {
    Result.setKind(TokenKind::string_literal);
    std::string Filename = CurrentFilename.empty() ? "<unknown>" : CurrentFilename.str();
    // Create a string literal token
    std::string QuotedFilename = "\"" + Filename + "\"";
    static std::string FileBuffer;  // Keep buffer alive
    FileBuffer = QuotedFilename;
    Result.setLiteralData(FileBuffer.c_str());
    Result.setLength(static_cast<unsigned>(FileBuffer.size()));

    // Record macro expansion
    SM.addMacroExpansion(Result.getLocation(), SpellingLoc, ExpansionLoc, MacroName);
    return true;
  }

  if (MacroName == "__LINE__") {
    Result.setKind(TokenKind::numeric_constant);
    // B3.7: Get line number from SourceManager
    auto LineAndCol = SM.getLineAndColumn(Result.getLocation());
    unsigned Line = LineAndCol.first;
    if (Line == 0) Line = CurrentLine;  // Fallback
    static std::string LineBuffer;
    LineBuffer = std::to_string(Line);
    Result.setLiteralData(LineBuffer.c_str());
    Result.setLength(static_cast<unsigned>(LineBuffer.size()));

    // Record macro expansion
    SM.addMacroExpansion(Result.getLocation(), SpellingLoc, ExpansionLoc, MacroName);
    return true;
  }

  // A3.4: __has_include(filename)
  if (MacroName == "__has_include") {
    Token NextTok = CurLexer->peekNextToken();
    if (!NextTok.is(TokenKind::l_paren)) {
      MI->setBeingExpanded(false);
      return false;
    }

    auto Args = parseMacroArguments(MI);
    bool HasInclude = false;

    if (!Args.empty() && Args[0].size() >= 1) {
      Token &ArgTok = Args[0][0];
      if (ArgTok.isStringLiteral()) {
        StringRef Filename = ArgTok.getText();
        if (Filename.size() >= 2) {
          Filename = Filename.substr(1, Filename.size() - 2);
        }
        if (Headers) {
          HasInclude = (Headers->lookupHeader(Filename, false) != nullptr);
        }
      }
    }

    Result.setKind(TokenKind::numeric_constant);
    static std::string HasIncludeBuffer;
    HasIncludeBuffer = HasInclude ? "1" : "0";
    Result.setLiteralData(HasIncludeBuffer.c_str());
    Result.setLength(static_cast<unsigned>(HasIncludeBuffer.size()));
    return true;
  }

  // A3.5: __has_embed(filename) (C++26)
  if (MacroName == "__has_embed") {
    Token NextTok = CurLexer->peekNextToken();
    if (!NextTok.is(TokenKind::l_paren)) {
      MI->setBeingExpanded(false);
      return false;
    }

    auto Args = parseMacroArguments(MI);
    bool HasEmbed = false;

    if (!Args.empty() && Args[0].size() >= 1) {
      Token &ArgTok = Args[0][0];
      if (ArgTok.isStringLiteral()) {
        StringRef Filename = ArgTok.getText();
        if (Filename.size() >= 2) {
          Filename = Filename.substr(1, Filename.size() - 2);
        }
        if (Headers && FileMgr) {
          const FileEntry *FE = Headers->lookupHeader(Filename, false);
          HasEmbed = (FE != nullptr);
        }
      }
    }

    Result.setKind(TokenKind::numeric_constant);
    static std::string HasEmbedBuffer;
    HasEmbedBuffer = HasEmbed ? "1" : "0";
    Result.setLiteralData(HasEmbedBuffer.c_str());
    Result.setLength(static_cast<unsigned>(HasEmbedBuffer.size()));
    return true;
  }

  // Prevent recursive expansion
  if (MI->isBeingExpanded()) {
    return false;  // Don't expand, return the identifier as-is
  }

  MI->setUsed(true);
  MI->setBeingExpanded(true);

  bool Success = false;

  if (MI->isFunctionLike()) {
    Token NextTok = CurLexer->peekNextToken();
    if (!NextTok.is(TokenKind::l_paren)) {
      MI->setBeingExpanded(false);
      return false;
    }

    auto Args = parseMacroArguments(MI);
    std::vector<Token> Replacement = MI->getReplacementTokens();
    substituteParameters(Replacement, MI, Args);

    if (!Replacement.empty()) {
      Result = Replacement[0];
      // Try to expand the result if it's a macro
      if (Result.is(TokenKind::identifier)) {
        if (MacroInfo *InnerMI = getMacroInfo(Result.getText())) {
          if (InnerMI->isBeingExpanded()) {
            // Inner macro is being expanded - this would cause recursion
            // Return false to stop expansion and keep the identifier as-is
            MI->setBeingExpanded(false);
            return false;
          }
          // Save result before recursive expansion
          Token SavedResult = Result;
          // Try to expand inner macro
          bool innerSuccess = expandMacro(Result, Result.getText(), InnerMI);
          if (innerSuccess) {
            MI->setBeingExpanded(false);
            return true;
          }
          // Inner expansion failed, restore saved result
          Result = SavedResult;
        }
      }
      Success = true;
    }
  } else {
    const auto &Tokens = MI->getReplacementTokens();
    if (!Tokens.empty()) {
      Result = Tokens[0];
      // Try to expand the result if it's a macro
      if (Result.is(TokenKind::identifier)) {
        if (MacroInfo *InnerMI = getMacroInfo(Result.getText())) {
          if (InnerMI->isBeingExpanded()) {
            // Inner macro is being expanded - this would cause recursion
            MI->setBeingExpanded(false);
            return false;
          }
          // Save result before recursive expansion
          Token SavedResult = Result;
          // Try to expand inner macro
          bool innerSuccess = expandMacro(Result, Result.getText(), InnerMI);
          if (innerSuccess) {
            MI->setBeingExpanded(false);
            return true;
          }
          // Inner expansion failed, restore saved result
          Result = SavedResult;
        }
      }
      Success = true;
    }
  }

  MI->setBeingExpanded(false);

  // E2: Record macro expansion location if successful
  if (Success) {
    SM.addMacroExpansion(Result.getLocation(), SpellingLoc, ExpansionLoc, MacroName);
  }

  return Success;
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
  // Find variadic parameter index (if any)
  int VariadicIndex = -1;
  bool IsVariadic = MI->isVariadic();
  if (IsVariadic && MI->getNumParameters() > 0) {
    // Last parameter might be variadic
    VariadicIndex = static_cast<int>(MI->getNumParameters()) - 1;
  }
  
  std::vector<Token> Result;
  Result.reserve(Tokens.size());
  
  for (size_t i = 0; i < Tokens.size(); ++i) {
    Token &Tok = Tokens[i];
    
    if (Tok.is(TokenKind::identifier)) {
      StringRef Name = Tok.getText();
      
      // Handle __VA_ARGS__
      if (Name == "__VA_ARGS__" && IsVariadic && VariadicIndex >= 0) {
        // Replace with all variadic arguments
        if (static_cast<size_t>(VariadicIndex) < Args.size()) {
          // Insert all variadic arguments separated by commas
          for (size_t j = VariadicIndex; j < Args.size(); ++j) {
            if (j > static_cast<size_t>(VariadicIndex)) {
              // Add comma token between variadic args
              Token CommaTok;
              CommaTok.setKind(TokenKind::comma);
              Result.push_back(CommaTok);
            }
            for (const auto &ArgTok : Args[j]) {
              Result.push_back(ArgTok);
            }
          }
        }
        continue;
      }
      
      // Handle __VA_OPT__
      if (Name == "__VA_OPT__" && IsVariadic) {
        // Check if followed by '('
        if (i + 1 < Tokens.size() && Tokens[i + 1].is(TokenKind::l_paren)) {
          // Find matching ')'
          size_t Start = i + 2;
          size_t End = Start;
          unsigned ParenDepth = 1;
          while (End < Tokens.size() && ParenDepth > 0) {
            if (Tokens[End].is(TokenKind::l_paren)) {
              ++ParenDepth;
            } else if (Tokens[End].is(TokenKind::r_paren)) {
              --ParenDepth;
            }
            if (ParenDepth > 0) {
              ++End;
            }
          }
          
          // D9: Check if there are variadic arguments
          // Variadic arguments are those at and after VariadicIndex
          bool HasVariadicArgs = false;
          if (VariadicIndex >= 0 && static_cast<size_t>(VariadicIndex) < Args.size()) {
            // Check if any variadic argument is non-empty
            for (size_t k = static_cast<size_t>(VariadicIndex); k < Args.size(); ++k) {
              if (!Args[k].empty()) {
                HasVariadicArgs = true;
                break;
              }
            }
          }
          
          if (HasVariadicArgs) {
            // D9: Extract tokens between parentheses and recursively substitute parameters
            std::vector<Token> VaOptContent;
            for (size_t j = Start; j < End; ++j) {
              VaOptContent.push_back(Tokens[j]);
            }
            // Recursively substitute parameters in __VA_OPT__ content
            substituteParameters(VaOptContent, MI, Args);
            // Insert the substituted content
            for (const auto &T : VaOptContent) {
              Result.push_back(T);
            }
          }
          
          // Skip past __VA_OPT__(...)
          i = End;
          continue;
        }
      }
      
      // Regular parameter substitution
      bool Found = false;
      for (unsigned p = 0; p < MI->getNumParameters(); ++p) {
        if (Name == MI->getParameterName(p)) {
          if (p < Args.size() && !Args[p].empty()) {
            // Replace with argument tokens
            for (const auto &ArgTok : Args[p]) {
              Result.push_back(ArgTok);
            }
          }
          Found = true;
          break;
        }
      }
      
      if (!Found) {
        Result.push_back(Tok);
      }
    } else {
      Result.push_back(Tok);
    }
  }
  
  Tokens = std::move(Result);
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
