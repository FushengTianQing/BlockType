//===--- Preprocessor.h - Preprocessor Interface --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Preprocessor class which handles preprocessing
// directives, macro expansion, and file inclusion.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include "blocktype/Basic/SourceLocation.h"
#include "blocktype/Basic/Language.h"
#include "blocktype/Lex/Token.h"
#include <memory>
#include <vector>
#include <map>
#include <set>

namespace blocktype {

class Lexer;
class SourceManager;
class DiagnosticsEngine;
class HeaderSearch;
class MacroInfo;
class FileManager;  // Forward declaration
class FileEntry;    // Forward declaration

/// MacroInfo - Stores information about a macro definition.
class MacroInfo {
  SourceLocation DefinitionLocation;
  std::vector<Token> ReplacementTokens;
  std::vector<StringRef> ParameterList;
  bool IsFunctionLike : 1;
  bool IsVariadic : 1;
  bool IsPredefined : 1;
  bool IsUsed : 1;
  bool IsBeingExpanded : 1;  // Prevent recursive expansion

public:
  MacroInfo(SourceLocation Loc)
      : DefinitionLocation(Loc), IsFunctionLike(false), IsVariadic(false),
        IsPredefined(false), IsUsed(false), IsBeingExpanded(false) {}

  // Accessors
  SourceLocation getDefinitionLocation() const { return DefinitionLocation; }

  bool isFunctionLike() const { return IsFunctionLike; }
  void setFunctionLike(bool FL) { IsFunctionLike = FL; }

  bool isVariadic() const { return IsVariadic; }
  void setVariadic(bool V) { IsVariadic = V; }

  bool isPredefined() const { return IsPredefined; }
  void setPredefined(bool PD) { IsPredefined = PD; }

  bool isUsed() const { return IsUsed; }
  void setUsed(bool U) { IsUsed = U; }

  bool isBeingExpanded() const { return IsBeingExpanded; }
  void setBeingExpanded(bool BE) { IsBeingExpanded = BE; }

  // Replacement tokens
  const std::vector<Token> &getReplacementTokens() const { return ReplacementTokens; }
  void addToken(const Token &T) { ReplacementTokens.push_back(T); }
  void setReplacementTokens(ArrayRef<Token> Tokens) {
    ReplacementTokens.assign(Tokens.begin(), Tokens.end());
  }

  // Parameters (for function-like macros)
  unsigned getNumParameters() const { return static_cast<unsigned>(ParameterList.size()); }
  StringRef getParameterName(unsigned i) const { return ParameterList[i]; }
  void addParameter(StringRef Name) { ParameterList.push_back(Name); }
  bool isParameter(StringRef Name) const;

  // Utility
  bool isIdenticalTo(const MacroInfo *Other) const;
};

/// Preprocessor - Handles C++ preprocessing.
class Preprocessor {
  SourceManager &SM;
  DiagnosticsEngine &Diags;
  HeaderSearch *Headers;
  LanguageManager *LangMgr;
  FileManager *FileMgr;  // For reading included files

  // Include stack
  struct IncludeStackEntry {
    std::unique_ptr<Lexer> Lex;
    SourceLocation IncludeLoc;
    StringRef Filename;
  };
  std::vector<IncludeStackEntry> IncludeStack;
  Lexer *CurLexer = nullptr;

  // Macro table
  std::map<StringRef, std::unique_ptr<MacroInfo>> Macros;

  // Predefined macros
  std::set<StringRef> PredefinedMacros;

  // Conditional compilation stack
  struct ConditionalInfo {
    bool WasSkipping;      // Were we skipping before this #if?
    bool FoundNonSkip;     // Have we found a true branch?
    bool FoundElse;        // Have we seen #else?
    SourceLocation IfLoc;  // Location of the #if
  };
  std::vector<ConditionalInfo> ConditionalStack;

  // Skipping state
  bool Skipping = false;

  // Current language mode
  Language CurrentLang = Language::English;

  // Current file context for __FILE__ and __LINE__
  StringRef CurrentFilename;
  unsigned CurrentLine = 1;  // B3.7: Current line number for __LINE__

  // Include search paths for relative includes (B3.5)
  std::vector<std::string> IncludeSearchPaths;

  // Token buffer for lookahead (simple implementation)
  std::vector<Token> TokenBuffer;
  size_t TokenBufferIndex = 0;

  // #pragma once tracking
  std::set<unsigned> PragmaOnceFiles;

  // Current file ID for #pragma once
  unsigned CurrentFileID = static_cast<unsigned>(-1);

  // Include guard detection
  struct IncludeGuardInfo {
    StringRef MacroName;       // The macro name used for the guard
    SourceLocation IfLoc;      // Location of the #ifndef
    bool IsValid = false;      // Is this a valid include guard?
    bool SawDefine = false;    // Did we see the matching #define?
  };
  std::map<unsigned, IncludeGuardInfo> FileIncludeGuards;  // FileID -> IncludeGuardInfo
  StringRef PendingIncludeGuard;  // Potential include guard macro name

public:
  Preprocessor(SourceManager &SM, DiagnosticsEngine &Diags,
               HeaderSearch *HS = nullptr, LanguageManager *LM = nullptr,
               FileManager *FM = nullptr);
  ~Preprocessor();

  // Non-copyable
  Preprocessor(const Preprocessor &) = delete;
  Preprocessor &operator=(const Preprocessor &) = delete;

  //===--------------------------------------------------------------------===//
  // Main entry points
  //===--------------------------------------------------------------------===//

  /// Lexes the next token from the preprocessed stream.
  bool lexToken(Token &Result);

  /// Enters a source file for preprocessing.
  void enterSourceFile(StringRef Filename, StringRef Content);

  /// Returns true if we've reached the end of all files.
  bool isEOF() const;

  //===--------------------------------------------------------------------===//
  // Macro management
  //===--------------------------------------------------------------------===//

  /// Defines a macro.
  void defineMacro(StringRef Name, StringRef Body);

  /// Defines a macro with MacroInfo.
  void defineMacro(StringRef Name, std::unique_ptr<MacroInfo> MI);

  /// Undefines a macro.
  void undefMacro(StringRef Name);

  /// Returns true if a macro is defined.
  bool isMacroDefined(StringRef Name) const;

  /// Returns the macro info for a macro, or nullptr.
  MacroInfo *getMacroInfo(StringRef Name) const;

  /// Returns all defined macro names.
  std::vector<StringRef> getMacroNames() const;

  //===--------------------------------------------------------------------===//
  // Predefined macros
  //===--------------------------------------------------------------------===//

  /// Initializes predefined macros.
  void initializePredefinedMacros();

  /// Defines a predefined macro.
  void definePredefinedMacro(StringRef Name, StringRef Value);

  //===--------------------------------------------------------------------===//
  // Include handling
  //===--------------------------------------------------------------------===//

  /// Handles #include directive.
  void handleIncludeDirective(Token &IncludeTok, bool IsAngled);

  /// Handles #include_next directive (GNU extension).
  void handleIncludeNextDirective(Token &IncludeTok, bool IsAngled);

  /// Handles #embed directive (C++26).
  void handleEmbedDirective(Token &EmbedTok);

  /// Handles __has_include expression.
  void handleHasInclude(Token &Result);

  /// Handles __has_embed expression (C++26).
  void handleHasEmbed(Token &Result);

  //===--------------------------------------------------------------------===//
  // Conditional compilation
  //===--------------------------------------------------------------------===//

  /// Returns true if we're skipping tokens.
  bool isSkipping() const { return Skipping; }

  /// Handles #if directive.
  void handleIfDirective(Token &IfTok);

  /// Handles #ifdef directive.
  void handleIfdefDirective(Token &IfdefTok);

  /// Handles #ifndef directive.
  void handleIfndefDirective(Token &IfndefTok);

  /// Handles #elif directive.
  void handleElifDirective(Token &ElifTok);

  /// Handles #else directive.
  void handleElseDirective(Token &ElseTok);

  /// Handles #endif directive.
  void handleEndifDirective(Token &EndifTok);

  //===--------------------------------------------------------------------===//
  // Other directives
  //===--------------------------------------------------------------------===//

  /// Handles #define directive.
  void handleDefineDirective(Token &DefineTok);

  /// Handles #undef directive.
  void handleUndefDirective(Token &UndefTok);

  /// Handles #error directive.
  void handleErrorDirective(Token &ErrorTok);

  /// Handles #warning directive.
  void handleWarningDirective(Token &WarningTok);

  /// Handles #pragma directive.
  void handlePragmaDirective(Token &PragmaTok);

  /// Handles #line directive.
  void handleLineDirective(Token &LineTok);

  //===--------------------------------------------------------------------===//
  // Bilingual support
  //===--------------------------------------------------------------------===//

  /// Handles bilingual (Chinese) preprocessor directive.
  void handleBilingualDirective(Token &DirectiveTok);

  /// Returns the current language mode.
  Language getCurrentLanguage() const { return CurrentLang; }

  /// Sets the current language mode.
  void setCurrentLanguage(Language L) { CurrentLang = L; }

  //===--------------------------------------------------------------------===//
  // Token buffer operations
  //===--------------------------------------------------------------------===//

  /// Peeks at the Nth token ahead without consuming it.
  Token peekToken(unsigned N = 0);

  /// Consumes and returns the next token.
  bool consumeToken(Token &Result);

  /// Returns the number of tokens in the buffer.
  size_t tokenBufferSize() const { return TokenBuffer.size() - TokenBufferIndex; }

  /// Saves the current token buffer state for tentative parsing.
  /// Returns the current TokenBufferIndex that can be restored later.
  size_t saveTokenBufferState() const { return TokenBufferIndex; }

  /// Restores the token buffer to a previously saved state.
  /// This is used for backtracking during tentative parsing.
  void restoreTokenBufferState(size_t SavedIndex);

  /// Clears the token buffer, removing all pending tokens.
  /// This is called after a successful tentative parse to commit the tokens.
  void clearTokenBuffer();

  //===--------------------------------------------------------------------===//
  // Accessors
  //===--------------------------------------------------------------------===//

  SourceManager &getSourceManager() const { return SM; }
  DiagnosticsEngine &getDiagnostics() const { return Diags; }
  HeaderSearch *getHeaderSearch() const { return Headers; }

private:
  //===--------------------------------------------------------------------===//
  // Internal helpers
  //===--------------------------------------------------------------------===//

  /// Lexes a token from the current lexer.
  bool lexFromLexer(Token &Result);

  /// Handles a preprocessor directive.
  void handleDirective(Token &HashTok);

  /// Expands a macro.
  bool expandMacro(Token &Result, StringRef MacroName, MacroInfo *MI);

  /// Parses a macro definition.
  std::unique_ptr<MacroInfo> parseMacroDefinition(Token &MacroNameTok);

  /// Parses macro arguments.
  std::vector<std::vector<Token>> parseMacroArguments(MacroInfo *MI);

  /// Substitutes macro parameters.
  void substituteParameters(std::vector<Token> &Tokens, MacroInfo *MI,
                            const std::vector<std::vector<Token>> &Args);

  /// Stringifies a token (# operator).
  Token stringifyToken(const Token &T);

  /// Concatenates two tokens (## operator).
  Token concatenateTokens(const Token &T1, const Token &T2);

  /// Evaluates a preprocessor expression.
  bool evaluateCondition(ArrayRef<Token> Tokens);

  /// Handles #elifdef directive (C++23).
  void handleElifdefDirective(Token &ElifdefTok);

  /// Handles #elifndef directive (C++23).
  void handleElifndefDirective(Token &ElifndefTok);

  /// Skips until #endif, #else, or #elif.
  void skipConditionalBlock();

  /// Returns the directive name from a token.
  StringRef getDirectiveName(Token &Tok);

  /// Maps Chinese directive to English.
  StringRef mapChineseDirective(StringRef ZhDirective);
};

} // namespace blocktype
