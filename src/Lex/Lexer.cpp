//===--- Lexer.cpp - Lexer Implementation ----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Lexer class.
//
//===----------------------------------------------------------------------===//

#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/Unicode.h"
#include <cctype>
#include <unordered_map>

namespace blocktype {

//===----------------------------------------------------------------------===//
// Keyword Lookup Tables
//===----------------------------------------------------------------------===//

// English keyword lookup table
static const std::unordered_map<std::string, TokenKind> EnglishKeywords = {
#define KEYWORD(X, Y) {#X, TokenKind::kw_##X},
#include "blocktype/Lex/TokenKinds.def"
#undef KEYWORD
};

// Chinese keyword lookup table
static const std::unordered_map<std::string, TokenKind> ChineseKeywords = {
#define KEYWORD_ZH(X, Y) {#X, TokenKind::Y},
#include "blocktype/Lex/TokenKinds.def"
#undef KEYWORD_ZH
};

//===----------------------------------------------------------------------===//
// Lexer Implementation
//===----------------------------------------------------------------------===//

Lexer::Lexer(SourceManager &SM, DiagnosticsEngine &Diags,
             StringRef Buffer, SourceLocation StartLoc)
    : SM(SM), Diags(Diags), BufferStart(Buffer.begin()),
      BufferEnd(Buffer.end()), BufferPtr(BufferStart),
      FileID(StartLoc.getID()) {
  // Skip UTF-8 BOM if present
  if (BufferPtr + 3 <= BufferEnd &&
      static_cast<unsigned char>(BufferPtr[0]) == 0xEF &&
      static_cast<unsigned char>(BufferPtr[1]) == 0xBB &&
      static_cast<unsigned char>(BufferPtr[2]) == 0xBF) {
    BufferPtr += 3;
  }
}

bool Lexer::lexToken(Token &Result) {
  // Skip whitespace and comments
  skipWhitespaceAndComments();

  // In preprocessor directive mode, check for end of directive (newline)
  if (IsInPreprocessorDirective && BufferPtr < BufferEnd && *BufferPtr == '\n') {
    consumeChar();  // Consume the newline
    IsInPreprocessorDirective = false;
    Result.setKind(TokenKind::eod);
    Result.setLocation(getSourceLocation());
    return true;
  }

  // Check for EOF
  if (isEOF()) {
    Result.setKind(TokenKind::eof);
    Result.setLocation(getSourceLocation());
    return false;
  }

  const char *TokStart = BufferPtr;
  char C = *BufferPtr;

  // After skipping whitespace, we're no longer at start of line
  // (unless we're about to lex a # directive)
  if (C != '#') {
    IsAtStartOfLine = false;
  }

  // Dispatch based on first character
  switch (C) {
  // Identifiers and keywords (excluding L, u, U, R which may be literals)
  case 'a': case 'b': case 'c': case 'd': case 'e':
  case 'f': case 'g': case 'h': case 'i': case 'j':
  case 'k': case 'l': case 'm': case 'n': case 'o':
  case 'p': case 'q': case 'r': case 's': case 't':
  case 'v': case 'w': case 'x': case 'y':
  case 'z':
  case 'A': case 'B': case 'C': case 'D': case 'E':
  case 'F': case 'G': case 'H': case 'I': case 'J':
  case 'K': case 'M': case 'N': case 'O':
  case 'P': case 'Q': case 'S': case 'T':
  case 'V': case 'W': case 'X': case 'Y':
  case 'Z':
  case '_':
    return lexIdentifier(Result, TokStart);

  // L, u, U, R - may be literal prefix or identifier
  case 'L': case 'u': case 'U': case 'R':
    return lexWideOrUTFLiteral(Result, TokStart);

  // Numeric constants
  case '0': case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
    return lexNumericConstant(Result, TokStart);

  // Character and string literals
  case '\'':
    return lexCharConstant(Result, TokStart);
  case '"':
    return lexStringLiteral(Result, TokStart);

  // Preprocessor directive
  case '#':
    consumeChar();
    if (BufferPtr < BufferEnd && *BufferPtr == '#') {
      consumeChar();
      return formToken(Result, TokenKind::hashhash, TokStart);
    }
    return formToken(Result, TokenKind::hash, TokStart);

  // Operators and punctuation
  case '+': case '-': case '*': case '/': case '%':
  case '^': case '&': case '|': case '~': case '!':
  case '=': case '<': case '>': case '.':
  case '?': case ':': case ',':
  case '(': case ')': case '{': case '}': case '[': case ']':
  case ';':
    return lexOperatorOrPunctuation(Result, TokStart);

  // UTF-8 multi-byte character (potential Chinese identifier)
  default:
    if (static_cast<unsigned char>(C) >= 0x80) {
      return lexIdentifier(Result, TokStart);
    }
    // Unknown character
    Diags.report(getSourceLocation(), DiagLevel::Error, "invalid character in source code");
    consumeChar();
    return formToken(Result, TokenKind::unknown, TokStart);
  }
}

Token Lexer::peekNextToken() {
  // Save current state
  const char *SavedPtr = BufferPtr;
  bool SavedAtStartOfLine = IsAtStartOfLine;
  bool SavedInPreprocessorDirective = IsInPreprocessorDirective;

  // Lex the next token
  Token Result;
  lexToken(Result);

  // Restore state
  BufferPtr = SavedPtr;
  IsAtStartOfLine = SavedAtStartOfLine;
  IsInPreprocessorDirective = SavedInPreprocessorDirective;

  return Result;
}

SourceLocation Lexer::getSourceLocation() const {
  // Simple encoding: file ID stored in location
  return SourceLocation(FileID);
}

//===----------------------------------------------------------------------===//
// Character operations
//===----------------------------------------------------------------------===//

char Lexer::peekChar(unsigned Offset) const {
  const char *Ptr = BufferPtr + Offset;
  if (Ptr >= BufferEnd)
    return '\0';
  return *Ptr;
}

char Lexer::getChar() {
  if (isEOF())
    return '\0';
  char C = *BufferPtr;
  consumeChar();
  return C;
}

void Lexer::consumeChar() {
  if (BufferPtr < BufferEnd) {
    if (*BufferPtr == '\n') {
      IsAtStartOfLine = true;
    }
    ++BufferPtr;
  }
}

void Lexer::consumeChars(unsigned N) {
  for (unsigned i = 0; i < N && !isEOF(); ++i) {
    consumeChar();
  }
}

//===----------------------------------------------------------------------===//
// Whitespace and comments
//===----------------------------------------------------------------------===//

void Lexer::skipWhitespaceAndComments() {
  while (BufferPtr < BufferEnd) {
    char C = *BufferPtr;

    // In preprocessor directive mode, stop at newline
    if (IsInPreprocessorDirective && C == '\n') {
      break;
    }

    // Whitespace
    if (std::isspace(static_cast<unsigned char>(C))) {
      consumeChar();
      continue;
    }

    // Comments
    if (C == '/') {
      if (BufferPtr + 1 < BufferEnd) {
        char Next = *(BufferPtr + 1);
        if (Next == '/') {
          skipLineComment();
          continue;
        }
        if (Next == '*') {
          skipBlockComment();
          continue;
        }
      }
    }

    // Not whitespace or comment
    break;
  }
}

void Lexer::skipLineComment() {
  // Skip //
  BufferPtr += 2;

  // Skip until end of line
  while (BufferPtr < BufferEnd && *BufferPtr != '\n') {
    ++BufferPtr;
  }
}

void Lexer::skipBlockComment() {
  // Skip /*
  BufferPtr += 2;

  // Skip until */
  while (BufferPtr + 1 < BufferEnd) {
    if (*BufferPtr == '*' && *(BufferPtr + 1) == '/') {
      BufferPtr += 2;
      return;
    }
    ++BufferPtr;
  }

  // Unterminated block comment
  BufferPtr = BufferEnd;
  Diags.report(getSourceLocation(), DiagLevel::Error, "unterminated block comment");
}

//===----------------------------------------------------------------------===//
// Token formation
//===----------------------------------------------------------------------===//

bool Lexer::formToken(Token &Result, TokenKind Kind, const char *TokStart) {
  Result.setKind(Kind);
  Result.setLocation(getSourceLocation());
  Result.setLength(static_cast<unsigned>(BufferPtr - TokStart));
  Result.setLiteralData(TokStart);
  return true;
}

bool Lexer::formIdentifierToken(Token &Result, const char *TokStart) {
  StringRef Text(TokStart, BufferPtr - TokStart);

  // Check if it's a keyword
  TokenKind Kind = getIdentifierKind(Text);

  Result.setKind(Kind);
  Result.setLocation(getSourceLocation());
  Result.setLength(static_cast<unsigned>(BufferPtr - TokStart));
  Result.setLiteralData(TokStart);

  // Set language for keywords - check if text contains Chinese characters
  if (isKeyword(Kind)) {
    // Check if the text contains non-ASCII characters (Chinese)
    bool hasChinese = false;
    for (size_t i = 0; i < Text.size(); ++i) {
      if (static_cast<unsigned char>(Text[i]) >= 0x80) {
        hasChinese = true;
        break;
      }
    }
    Result.setSourceLanguage(hasChinese ? Language::Chinese : Language::English);
  }

  return true;
}

//===----------------------------------------------------------------------===//
// Lexing routines
//===----------------------------------------------------------------------===//

bool Lexer::lexIdentifier(Token &Result, const char *Start) {
  // Check for UTF-8 multi-byte character (Chinese)
  if (static_cast<unsigned char>(*BufferPtr) >= 0x80) {
    // Chinese identifier or keyword
    while (BufferPtr < BufferEnd) {
      unsigned char C = static_cast<unsigned char>(*BufferPtr);
      if (C < 0x80) {
        // ASCII character - check if valid for identifier
        if (!std::isalnum(C) && C != '_')
          break;
        ++BufferPtr;
      } else {
        // UTF-8 multi-byte - consume the whole character
        uint32_t CP = decodeUTF8Char();
        // Check if valid identifier character (CJK characters are valid)
        if (CP == 0xFFFD) {
          // Invalid UTF-8 sequence
          break;
        }
      }
    }
  } else {
    // ASCII identifier
    while (BufferPtr < BufferEnd) {
      char C = *BufferPtr;
      if (std::isalnum(static_cast<unsigned char>(C)) || C == '_') {
        ++BufferPtr;
      } else {
        break;
      }
    }
  }

  return formIdentifierToken(Result, Start);
}

bool Lexer::lexNumericConstant(Token &Result, const char *Start) {
  // Check for hex/binary/octal prefix
  if (*BufferPtr == '0' && BufferPtr + 1 < BufferEnd) {
    char Next = std::tolower(static_cast<unsigned char>(*(BufferPtr + 1)));
    if (Next == 'x' || Next == 'X') {
      // Hexadecimal
      BufferPtr += 2;
      while (BufferPtr < BufferEnd) {
        char C = std::tolower(static_cast<unsigned char>(*BufferPtr));
        if (std::isxdigit(static_cast<unsigned char>(C)) || C == '\'') {
          ++BufferPtr;
        } else {
          break;
        }
      }
    } else if (Next == 'b' || Next == 'B') {
      // Binary (C++14)
      BufferPtr += 2;
      while (BufferPtr < BufferEnd) {
        char C = *BufferPtr;
        if (C == '0' || C == '1' || C == '\'') {
          ++BufferPtr;
        } else {
          break;
        }
      }
    } else if (std::isdigit(static_cast<unsigned char>(Next))) {
      // Octal
      while (BufferPtr < BufferEnd) {
        char C = *BufferPtr;
        if ((C >= '0' && C <= '7') || C == '\'') {
          ++BufferPtr;
        } else {
          break;
        }
      }
    }
  } else {
    // Decimal
    while (BufferPtr < BufferEnd) {
      char C = *BufferPtr;
      if (std::isdigit(static_cast<unsigned char>(C)) || C == '\'') {
        ++BufferPtr;
      } else {
        break;
      }
    }

    // Fractional part
    if (BufferPtr < BufferEnd && *BufferPtr == '.') {
      ++BufferPtr;
      while (BufferPtr < BufferEnd) {
        char C = *BufferPtr;
        if (std::isdigit(static_cast<unsigned char>(C)) || C == '\'') {
          ++BufferPtr;
        } else {
          break;
        }
      }
    }

    // Exponent
    if (BufferPtr < BufferEnd) {
      char C = std::tolower(static_cast<unsigned char>(*BufferPtr));
      if (C == 'e' || C == 'p') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd && (*BufferPtr == '+' || *BufferPtr == '-')) {
          ++BufferPtr;
        }
        while (BufferPtr < BufferEnd) {
          char C2 = *BufferPtr;
          if (std::isdigit(static_cast<unsigned char>(C2)) || C2 == '\'') {
            ++BufferPtr;
          } else {
            break;
          }
        }
      }
    }
  }

  // Suffix
  while (BufferPtr < BufferEnd) {
    char C = *BufferPtr;
    if (std::isalnum(static_cast<unsigned char>(C)) || C == '_') {
      ++BufferPtr;
    } else {
      break;
    }
  }

  return formToken(Result, TokenKind::numeric_constant, Start);
}

bool Lexer::lexCharConstant(Token &Result, const char *Start) {
  assert(*BufferPtr == '\'');
  ++BufferPtr; // Skip opening quote

  while (BufferPtr < BufferEnd && *BufferPtr != '\'') {
    if (*BufferPtr == '\\') {
      ++BufferPtr;
      if (BufferPtr < BufferEnd) {
        ++BufferPtr; // Skip escaped character
      }
    } else if (*BufferPtr == '\n') {
      // Unterminated character constant
      Diags.report(getSourceLocation(), DiagLevel::Error, "unterminated character constant");
      break;
    } else {
      ++BufferPtr;
    }
  }

  if (BufferPtr < BufferEnd && *BufferPtr == '\'') {
    ++BufferPtr; // Skip closing quote
  } else {
    Diags.report(getSourceLocation(), DiagLevel::Error, "unterminated character constant");
  }

  return formToken(Result, TokenKind::char_constant, Start);
}

bool Lexer::lexStringLiteral(Token &Result, const char *Start) {
  assert(*BufferPtr == '"');
  ++BufferPtr; // Skip opening quote

  while (BufferPtr < BufferEnd && *BufferPtr != '"') {
    if (*BufferPtr == '\\') {
      ++BufferPtr;
      if (BufferPtr < BufferEnd) {
        ++BufferPtr; // Skip escaped character
      }
    } else if (*BufferPtr == '\n') {
      // Unterminated string literal
      Diags.report(getSourceLocation(), DiagLevel::Error, "unterminated string literal");
      break;
    } else {
      ++BufferPtr;
    }
  }

  if (BufferPtr < BufferEnd && *BufferPtr == '"') {
    ++BufferPtr; // Skip closing quote
  } else {
    Diags.report(getSourceLocation(), DiagLevel::Error, "unterminated string literal");
  }

  return formToken(Result, TokenKind::string_literal, Start);
}

bool Lexer::lexWideOrUTFLiteral(Token &Result, const char *Start) {
  char Prefix = *BufferPtr;
  ++BufferPtr;

  // Check for u8 prefix (UTF-8 literal)
  bool IsUTF8 = false;
  if (Prefix == 'u' && BufferPtr < BufferEnd && *BufferPtr == '8') {
    IsUTF8 = true;
    ++BufferPtr;
  }

  // Check for raw string literal (R"...")
  if (Prefix == 'R' && BufferPtr < BufferEnd && *BufferPtr == '"') {
    return lexRawStringLiteral(Result, Start);
  }

  // Check for prefix followed by quote
  if (BufferPtr < BufferEnd && *BufferPtr == '\'') {
    // Character literal - determine kind and lex
    return lexCharConstant(Result, Start);
  }

  if (BufferPtr < BufferEnd && *BufferPtr == '"') {
    // String literal - determine kind
    TokenKind Kind = TokenKind::string_literal;
    switch (Prefix) {
    case 'L': Kind = TokenKind::wide_string_literal; break;
    case 'u':
      if (IsUTF8) {
        Kind = TokenKind::utf8_string_literal;
      } else {
        Kind = TokenKind::utf16_string_literal;
      }
      break;
    case 'U': Kind = TokenKind::utf32_string_literal; break;
    default: Kind = TokenKind::string_literal;
    }

    ++BufferPtr; // Skip opening quote

    while (BufferPtr < BufferEnd && *BufferPtr != '"') {
      if (*BufferPtr == '\\') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd)
          ++BufferPtr;
      } else {
        ++BufferPtr;
      }
    }

    if (BufferPtr < BufferEnd && *BufferPtr == '"') {
      ++BufferPtr;
    }

    return formToken(Result, Kind, Start);
  }

  // Not a literal, treat as identifier
  return lexIdentifier(Result, Start);
}

bool Lexer::lexRawStringLiteral(Token &Result, const char *Start) {
  // R"delim(content)delim"
  assert(*BufferPtr == '"');
  ++BufferPtr;

  // Extract delimiter
  std::string Delimiter;
  while (BufferPtr < BufferEnd && *BufferPtr != '(' && *BufferPtr != '"') {
    Delimiter += *BufferPtr;
    ++BufferPtr;
  }

  if (BufferPtr >= BufferEnd || *BufferPtr != '(') {
    Diags.report(getSourceLocation(), DiagLevel::Error, "invalid raw string literal");
    return formToken(Result, TokenKind::unknown, Start);
  }

  ++BufferPtr; // Skip '('

  // Find closing )delim"
  std::string ClosingPattern = ")" + Delimiter + "\"";
  while (BufferPtr < BufferEnd) {
    if (*BufferPtr == ')') {
      // Check for closing pattern
      bool Match = true;
      for (size_t i = 0; i < ClosingPattern.size(); ++i) {
        if (BufferPtr + i >= BufferEnd || BufferPtr[i] != ClosingPattern[i]) {
          Match = false;
          break;
        }
      }
      if (Match) {
        BufferPtr += ClosingPattern.size();
        return formToken(Result, TokenKind::string_literal, Start);
      }
    }
    ++BufferPtr;
  }

  Diags.report(getSourceLocation(), DiagLevel::Error, "unterminated raw string literal");
  return formToken(Result, TokenKind::unknown, Start);
}

bool Lexer::lexOperatorOrPunctuation(Token &Result, const char *Start) {
  char C = *BufferPtr;
  ++BufferPtr;

  switch (C) {
  case '+':
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == '+') { ++BufferPtr; return formToken(Result, TokenKind::plusplus, Start); }
      if (*BufferPtr == '=') { ++BufferPtr; return formToken(Result, TokenKind::plusequal, Start); }
    }
    return formToken(Result, TokenKind::plus, Start);

  case '-':
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == '-') { ++BufferPtr; return formToken(Result, TokenKind::minusminus, Start); }
      if (*BufferPtr == '=') { ++BufferPtr; return formToken(Result, TokenKind::minusequal, Start); }
      if (*BufferPtr == '>') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd && *BufferPtr == '*') {
          ++BufferPtr;
          return formToken(Result, TokenKind::arrowstar, Start);
        }
        return formToken(Result, TokenKind::arrow, Start);
      }
    }
    return formToken(Result, TokenKind::minus, Start);

  case '*':
    if (BufferPtr < BufferEnd && *BufferPtr == '=') {
      ++BufferPtr;
      return formToken(Result, TokenKind::starequal, Start);
    }
    return formToken(Result, TokenKind::star, Start);

  case '/':
    if (BufferPtr < BufferEnd && *BufferPtr == '=') {
      ++BufferPtr;
      return formToken(Result, TokenKind::slashequal, Start);
    }
    return formToken(Result, TokenKind::slash, Start);

  case '%':
    if (BufferPtr < BufferEnd && *BufferPtr == '=') {
      ++BufferPtr;
      return formToken(Result, TokenKind::percentequal, Start);
    }
    return formToken(Result, TokenKind::percent, Start);

  case '^':
    if (BufferPtr < BufferEnd && *BufferPtr == '=') {
      ++BufferPtr;
      return formToken(Result, TokenKind::caretequal, Start);
    }
    return formToken(Result, TokenKind::caret, Start);

  case '&':
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == '&') { ++BufferPtr; return formToken(Result, TokenKind::ampamp, Start); }
      if (*BufferPtr == '=') { ++BufferPtr; return formToken(Result, TokenKind::ampequal, Start); }
    }
    return formToken(Result, TokenKind::amp, Start);

  case '|':
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == '|') { ++BufferPtr; return formToken(Result, TokenKind::pipepipe, Start); }
      if (*BufferPtr == '=') { ++BufferPtr; return formToken(Result, TokenKind::pipeequal, Start); }
    }
    return formToken(Result, TokenKind::pipe, Start);

  case '~':
    return formToken(Result, TokenKind::tilde, Start);

  case '!':
    if (BufferPtr < BufferEnd && *BufferPtr == '=') {
      ++BufferPtr;
      return formToken(Result, TokenKind::exclaimequal, Start);
    }
    return formToken(Result, TokenKind::exclaim, Start);

  case '=':
    if (BufferPtr < BufferEnd && *BufferPtr == '=') {
      ++BufferPtr;
      return formToken(Result, TokenKind::equalequal, Start);
    }
    return formToken(Result, TokenKind::equal, Start);

  case '<':
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == '<') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd && *BufferPtr == '=') {
          ++BufferPtr;
          return formToken(Result, TokenKind::lesslessequal, Start);
        }
        return formToken(Result, TokenKind::lessless, Start);
      }
      if (*BufferPtr == '=') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd && *BufferPtr == '>') {
          ++BufferPtr;
          return formToken(Result, TokenKind::spaceship, Start);
        }
        return formToken(Result, TokenKind::lessequal, Start);
      }
    }
    return formToken(Result, TokenKind::less, Start);

  case '>':
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == '>') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd && *BufferPtr == '=') {
          ++BufferPtr;
          return formToken(Result, TokenKind::greatergreaterequal, Start);
        }
        return formToken(Result, TokenKind::greatergreater, Start);
      }
      if (*BufferPtr == '=') {
        ++BufferPtr;
        return formToken(Result, TokenKind::greaterequal, Start);
      }
    }
    return formToken(Result, TokenKind::greater, Start);

  case '.':
    if (BufferPtr + 1 < BufferEnd && *BufferPtr == '.' && *(BufferPtr + 1) == '.') {
      BufferPtr += 2;
      return formToken(Result, TokenKind::ellipsis, Start);
    }
    if (BufferPtr < BufferEnd && *BufferPtr == '*') {
      ++BufferPtr;
      return formToken(Result, TokenKind::periodstar, Start);
    }
    // Check for .. (C++26)
    if (BufferPtr < BufferEnd && *BufferPtr == '.') {
      ++BufferPtr;
      return formToken(Result, TokenKind::dotdot, Start);
    }
    return formToken(Result, TokenKind::period, Start);

  case '?':
    return formToken(Result, TokenKind::question, Start);

  case ':':
    if (BufferPtr < BufferEnd && *BufferPtr == ':') {
      ++BufferPtr;
      return formToken(Result, TokenKind::coloncolon, Start);
    }
    return formToken(Result, TokenKind::colon, Start);

  case ';':
    return formToken(Result, TokenKind::semicolon, Start);

  case ',':
    return formToken(Result, TokenKind::comma, Start);

  case '(':
    return formToken(Result, TokenKind::l_paren, Start);

  case ')':
    return formToken(Result, TokenKind::r_paren, Start);

  case '{':
    return formToken(Result, TokenKind::l_brace, Start);

  case '}':
    return formToken(Result, TokenKind::r_brace, Start);

  case '[':
    return formToken(Result, TokenKind::l_square, Start);

  case ']':
    return formToken(Result, TokenKind::r_square, Start);

  default:
    return formToken(Result, TokenKind::unknown, Start);
  }
}

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

TokenKind Lexer::getIdentifierKind(StringRef Identifier) {
  // Check English keywords first
  auto EnIt = EnglishKeywords.find(Identifier.str());
  if (EnIt != EnglishKeywords.end()) {
    return EnIt->second;
  }

  // Check Chinese keywords
  auto ZhIt = ChineseKeywords.find(Identifier.str());
  if (ZhIt != ChineseKeywords.end()) {
    return ZhIt->second;
  }

  return TokenKind::identifier;
}

TokenKind Lexer::getChineseKeywordKind(StringRef Keyword) {
  auto It = ChineseKeywords.find(Keyword.str());
  if (It != ChineseKeywords.end()) {
    return It->second;
  }
  return TokenKind::identifier;
}

bool Lexer::isIdentifierStartChar(char C) {
  return std::isalpha(static_cast<unsigned char>(C)) || C == '_';
}

bool Lexer::isIdentifierContinueChar(char C) {
  return std::isalnum(static_cast<unsigned char>(C)) || C == '_';
}

uint32_t Lexer::decodeUTF8Char() {
  if (BufferPtr >= BufferEnd)
    return 0;

  unsigned char FirstByte = static_cast<unsigned char>(*BufferPtr++);

  // ASCII
  if ((FirstByte & 0x80) == 0) {
    return FirstByte;
  }

  // Multi-byte sequence
  uint32_t CP = 0;
  unsigned NumBytes = 0;

  if ((FirstByte & 0xE0) == 0xC0) {
    CP = FirstByte & 0x1F;
    NumBytes = 2;
  } else if ((FirstByte & 0xF0) == 0xE0) {
    CP = FirstByte & 0x0F;
    NumBytes = 3;
  } else if ((FirstByte & 0xF8) == 0xF0) {
    CP = FirstByte & 0x07;
    NumBytes = 4;
  } else {
    return 0xFFFD; // Replacement character
  }

  // Read continuation bytes
  for (unsigned i = 1; i < NumBytes && BufferPtr < BufferEnd; ++i) {
    unsigned char Byte = static_cast<unsigned char>(*BufferPtr);
    if ((Byte & 0xC0) != 0x80) {
      return 0xFFFD;
    }
    CP = (CP << 6) | (Byte & 0x3F);
    ++BufferPtr;
  }

  return CP;
}

} // namespace blocktype
