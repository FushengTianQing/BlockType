//===--- Lexer.h - Lexer Interface -----------------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Lexer class which tokenizes source code.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include "blocktype/Basic/SourceLocation.h"
#include "blocktype/Lex/Token.h"
#include <functional>

namespace blocktype {

class SourceManager;
class DiagnosticsEngine;
class IdentifierInfo;

/// Lexer - Lexical analyzer for C++26 source code.
/// Supports both English and Chinese keywords.
class Lexer {
  SourceManager &SM;
  DiagnosticsEngine &Diags;

  // Buffer pointers
  const char *BufferStart;
  const char *BufferEnd;
  const char *BufferPtr; // Current position

  // Current file ID
  unsigned FileID;

  // Flags
  bool IsAtStartOfLine = true;
  bool IsInPreprocessorDirective = false;
  bool KeepComments = false;  // B2.7: Option to retain comments as tokens

public:
  /// Constructs a lexer for the given buffer.
  Lexer(SourceManager &SM, DiagnosticsEngine &Diags,
        StringRef Buffer, SourceLocation StartLoc);

  /// Lexes the next token from the source.
  /// Returns true if a token was produced, false on EOF.
  bool lexToken(Token &Result);

  /// Peeks at the next token without consuming it.
  Token peekNextToken();

  /// Returns the current location.
  SourceLocation getSourceLocation() const;

  /// Returns the source manager.
  SourceManager &getSourceManager() const { return SM; }

  /// Returns the diagnostics engine.
  DiagnosticsEngine &getDiagnostics() const { return Diags; }

  /// Sets the lexer into preprocessor directive mode.
  void setInPreprocessorDirective(bool InPP) { IsInPreprocessorDirective = InPP; }

  /// Returns true if at the start of a line.
  bool isAtStartOfLine() const { return IsAtStartOfLine; }

  /// Sets whether to keep comments as tokens (B2.7).
  void setKeepComments(bool Keep) { KeepComments = Keep; }

  /// Returns true if comments are being kept as tokens.
  bool isKeepingComments() const { return KeepComments; }

private:
  //===--------------------------------------------------------------------===//
  // Character operations
  //===--------------------------------------------------------------------===//

  /// Returns the current character without consuming it.
  char peekChar(unsigned Offset = 0) const;

  /// Returns the current character and advances.
  char getChar();

  /// Advances the buffer pointer by one.
  void consumeChar();

  /// Advances the buffer pointer by N characters.
  void consumeChars(unsigned N);

  /// Returns true if at end of buffer.
  bool isEOF() const { return BufferPtr >= BufferEnd; }

  //===--------------------------------------------------------------------===//
  // Whitespace and comments
  //===--------------------------------------------------------------------===//

  /// Skips whitespace and comments.
  void skipWhitespaceAndComments();

  /// Skips a line comment (//).
  void skipLineComment();

  /// Skips a block comment (/* */).
  void skipBlockComment();

  /// Processes an escape sequence starting at BufferPtr.
  /// Returns true if a valid escape sequence was processed.
  bool processEscapeSequence();

  //===--------------------------------------------------------------------===//
  // Token formation
  //===--------------------------------------------------------------------===//

  /// Forms a token with the given kind.
  bool formToken(Token &Result, TokenKind Kind, const char *TokStart);

  /// Forms a token with identifier or keyword kind.
  bool formIdentifierToken(Token &Result, const char *TokStart);

  //===--------------------------------------------------------------------===//
  // Lexing routines
  //===--------------------------------------------------------------------===//

  /// Lexes an identifier or keyword.
  bool lexIdentifier(Token &Result, const char *Start);

  /// Lexes a numeric constant.
  bool lexNumericConstant(Token &Result, const char *Start);

  /// Lexes a character constant.
  bool lexCharConstant(Token &Result, const char *Start);

  /// Lexes a string literal.
  bool lexStringLiteral(Token &Result, const char *Start);

  /// Lexes a wide/UTF string or character literal (L, u, U, u8).
  bool lexWideOrUTFLiteral(Token &Result, const char *Start);

  /// Lexes an operator or punctuation.
  bool lexOperatorOrPunctuation(Token &Result, const char *Start);

  /// Lexes a raw string literal.
  bool lexRawStringLiteral(Token &Result, const char *Start);

  //===--------------------------------------------------------------------===//
  // Helpers
  //===--------------------------------------------------------------------===//

  /// Returns the token kind for an identifier.
  TokenKind getIdentifierKind(StringRef Identifier);

  /// Returns the token kind for a Chinese keyword.
  TokenKind getChineseKeywordKind(StringRef Keyword);

  /// Checks if a character can start an identifier.
  static bool isIdentifierStartChar(char C);

  /// Checks if a character can continue an identifier.
  static bool isIdentifierContinueChar(char C);

  /// Checks if a Unicode code point can start an identifier (UAX #31 ID_Start).
  static bool isUnicodeIDStart(uint32_t CP);

  /// Checks if a Unicode code point can continue an identifier (UAX #31 ID_Continue).
  static bool isUnicodeIDContinue(uint32_t CP);

  /// Decodes a UTF-8 character starting at BufferPtr.
  /// Returns the code point and advances BufferPtr.
  uint32_t decodeUTF8Char();

  /// Tries to recover from a lexical error by skipping to a synchronization point.
  /// Returns true if recovery was successful.
  bool recoverFromError();
};

} // namespace blocktype
