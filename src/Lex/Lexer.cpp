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
  // Check for UTF-8 multi-byte character (Unicode identifier)
  if (static_cast<unsigned char>(*BufferPtr) >= 0x80) {
    // Unicode identifier - use UAX #31 rules
    bool isFirst = true;
    while (BufferPtr < BufferEnd) {
      unsigned char C = static_cast<unsigned char>(*BufferPtr);
      if (C < 0x80) {
        // ASCII character - check if valid for identifier
        if (isFirst) {
          if (!isIdentifierStartChar(C)) break;
        } else {
          if (!isIdentifierContinueChar(C)) break;
        }
        ++BufferPtr;
      } else {
        // UTF-8 multi-byte - decode and check UAX #31
        const char *SavedPtr = BufferPtr;
        uint32_t CP = decodeUTF8Char();
        if (CP == 0xFFFD) {
          // Invalid UTF-8 sequence - restore and break
          BufferPtr = SavedPtr;
          break;
        }
        // Check UAX #31 compliance
        if (isFirst) {
          if (!isUnicodeIDStart(CP)) {
            BufferPtr = SavedPtr;
            break;
          }
        } else {
          if (!isUnicodeIDContinue(CP)) {
            BufferPtr = SavedPtr;
            break;
          }
        }
      }
      isFirst = false;
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
      // Hexadecimal (integer or floating-point)
      BufferPtr += 2;
      while (BufferPtr < BufferEnd) {
        char C = std::tolower(static_cast<unsigned char>(*BufferPtr));
        if (std::isxdigit(static_cast<unsigned char>(C)) || C == '\'') {
          ++BufferPtr;
        } else {
          break;
        }
      }
      
      // Check for hexadecimal floating-point fraction
      if (BufferPtr < BufferEnd && *BufferPtr == '.') {
        ++BufferPtr;
        while (BufferPtr < BufferEnd) {
          char C = std::tolower(static_cast<unsigned char>(*BufferPtr));
          if (std::isxdigit(static_cast<unsigned char>(C)) || C == '\'') {
            ++BufferPtr;
          } else {
            break;
          }
        }
      }
      
      // Check for hexadecimal floating-point exponent (p/P)
      if (BufferPtr < BufferEnd) {
        char C = std::tolower(static_cast<unsigned char>(*BufferPtr));
        if (C == 'p') {
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
      ++BufferPtr; // Skip backslash
      if (BufferPtr < BufferEnd) {
        // Handle Unicode escape sequences
        if (*BufferPtr == 'u') {
          ++BufferPtr; // Skip 'u'
          // \uXXXX - exactly 4 hex digits
          for (int i = 0; i < 4 && BufferPtr < BufferEnd; ++i) {
            if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
              ++BufferPtr;
            } else {
              Diags.report(getSourceLocation(), DiagLevel::Error, 
                           "invalid universal character name: expected 4 hex digits after \\u");
              break;
            }
          }
        } else if (*BufferPtr == 'U') {
          ++BufferPtr; // Skip 'U'
          // \UXXXXXXXX - exactly 8 hex digits
          for (int i = 0; i < 8 && BufferPtr < BufferEnd; ++i) {
            if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
              ++BufferPtr;
            } else {
              Diags.report(getSourceLocation(), DiagLevel::Error, 
                           "invalid universal character name: expected 8 hex digits after \\U");
              break;
            }
          }
        } else {
          ++BufferPtr; // Skip other escape character
        }
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
      ++BufferPtr; // Skip backslash
      if (BufferPtr < BufferEnd) {
        // Handle Unicode escape sequences
        if (*BufferPtr == 'u') {
          ++BufferPtr; // Skip 'u'
          // \uXXXX - exactly 4 hex digits
          for (int i = 0; i < 4 && BufferPtr < BufferEnd; ++i) {
            if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
              ++BufferPtr;
            } else {
              Diags.report(getSourceLocation(), DiagLevel::Error, 
                           "invalid universal character name: expected 4 hex digits after \\u");
              break;
            }
          }
        } else if (*BufferPtr == 'U') {
          ++BufferPtr; // Skip 'U'
          // \UXXXXXXXX - exactly 8 hex digits
          for (int i = 0; i < 8 && BufferPtr < BufferEnd; ++i) {
            if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
              ++BufferPtr;
            } else {
              Diags.report(getSourceLocation(), DiagLevel::Error, 
                           "invalid universal character name: expected 8 hex digits after \\U");
              break;
            }
          }
        } else {
          ++BufferPtr; // Skip other escape character
        }
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
    // Character literal - determine kind based on prefix
    TokenKind Kind = TokenKind::char_constant;
    switch (Prefix) {
    case 'L': Kind = TokenKind::wide_char_constant; break;
    case 'u':
      if (IsUTF8) {
        Kind = TokenKind::utf8_char_constant;
      } else {
        Kind = TokenKind::utf16_char_constant;
      }
      break;
    case 'U': Kind = TokenKind::utf32_char_constant; break;
    default: Kind = TokenKind::char_constant;
    }
    
    ++BufferPtr; // Skip opening quote
    
    // Lex the character content
    while (BufferPtr < BufferEnd && *BufferPtr != '\'') {
      if (*BufferPtr == '\\') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd) {
          // Handle Unicode escape sequences
          if (*BufferPtr == 'u') {
            ++BufferPtr;
            for (int i = 0; i < 4 && BufferPtr < BufferEnd; ++i) {
              if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
                ++BufferPtr;
              } else {
                Diags.report(getSourceLocation(), DiagLevel::Error, 
                             "invalid universal character name: expected 4 hex digits after \\u");
                break;
              }
            }
          } else if (*BufferPtr == 'U') {
            ++BufferPtr;
            for (int i = 0; i < 8 && BufferPtr < BufferEnd; ++i) {
              if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
                ++BufferPtr;
              } else {
                Diags.report(getSourceLocation(), DiagLevel::Error, 
                             "invalid universal character name: expected 8 hex digits after \\U");
                break;
              }
            }
          } else {
            ++BufferPtr;
          }
        }
      } else if (*BufferPtr == '\n') {
        Diags.report(getSourceLocation(), DiagLevel::Error, "unterminated character constant");
        break;
      } else {
        ++BufferPtr;
      }
    }
    
    if (BufferPtr < BufferEnd && *BufferPtr == '\'') {
      ++BufferPtr;
    } else {
      Diags.report(getSourceLocation(), DiagLevel::Error, "unterminated character constant");
    }
    
    return formToken(Result, Kind, Start);
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
        if (BufferPtr < BufferEnd) {
          // Handle Unicode escape sequences
          if (*BufferPtr == 'u') {
            ++BufferPtr;
            for (int i = 0; i < 4 && BufferPtr < BufferEnd; ++i) {
              if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
                ++BufferPtr;
              } else {
                Diags.report(getSourceLocation(), DiagLevel::Error, 
                             "invalid universal character name: expected 4 hex digits after \\u");
                break;
              }
            }
          } else if (*BufferPtr == 'U') {
            ++BufferPtr;
            for (int i = 0; i < 8 && BufferPtr < BufferEnd; ++i) {
              if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
                ++BufferPtr;
              } else {
                Diags.report(getSourceLocation(), DiagLevel::Error, 
                             "invalid universal character name: expected 8 hex digits after \\U");
                break;
              }
            }
          } else {
            ++BufferPtr;
          }
        }
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
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == '=') {
        ++BufferPtr;
        return formToken(Result, TokenKind::percentequal, Start);
      }
      if (*BufferPtr == '>') {
        ++BufferPtr;
        return formToken(Result, TokenKind::percentgreater, Start);  // Digraph %>
      }
      if (*BufferPtr == ':') {
        ++BufferPtr;
        if (BufferPtr + 1 < BufferEnd && *BufferPtr == '%' && *(BufferPtr + 1) == ':') {
          BufferPtr += 2;
          return formToken(Result, TokenKind::percentcolonpercentcolon, Start);  // Digraph %:%:
        }
        return formToken(Result, TokenKind::percentcolon, Start);  // Digraph %:
      }
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
        if (BufferPtr < BufferEnd && *BufferPtr == '<') {
          ++BufferPtr;
          return formToken(Result, TokenKind::lesslessless, Start);  // C++26 digraph <<<
        }
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
      if (*BufferPtr == '%') {
        ++BufferPtr;
        return formToken(Result, TokenKind::lesspercent, Start);  // Digraph <%
      }
      if (*BufferPtr == ':') {
        ++BufferPtr;
        return formToken(Result, TokenKind::lesscolon, Start);  // Digraph <:
      }
    }
    return formToken(Result, TokenKind::less, Start);

  case '>':
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == '>') {
        ++BufferPtr;
        if (BufferPtr < BufferEnd && *BufferPtr == '>') {
          ++BufferPtr;
          return formToken(Result, TokenKind::greatergreatergreater, Start);  // C++26 digraph >>>
        }
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
    if (BufferPtr < BufferEnd) {
      if (*BufferPtr == ':') {
        ++BufferPtr;
        return formToken(Result, TokenKind::coloncolon, Start);
      }
      if (*BufferPtr == '>') {
        ++BufferPtr;
        return formToken(Result, TokenKind::colongreater, Start);  // Digraph :>
      }
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

//===----------------------------------------------------------------------===//
// UAX #31 Unicode Identifier Support
//===----------------------------------------------------------------------===//

bool Lexer::isUnicodeIDStart(uint32_t CP) {
  // ASCII letters and underscore
  if (CP < 0x80) {
    return std::isalpha(static_cast<unsigned char>(CP)) || CP == '_';
  }

  // Common ID_Start ranges (simplified UAX #31 implementation)
  // CJK Unified Ideographs: U+4E00..U+9FFF
  if (CP >= 0x4E00 && CP <= 0x9FFF) return true;
  // CJK Unified Ideographs Extension A: U+3400..U+4DBF
  if (CP >= 0x3400 && CP <= 0x4DBF) return true;
  // CJK Unified Ideographs Extension B-F: U+20000..U+2CEAF
  if (CP >= 0x20000 && CP <= 0x2CEAF) return true;
  // CJK Compatibility Ideographs: U+F900..U+FAFF
  if (CP >= 0xF900 && CP <= 0xFAFF) return true;

  // Latin Extended Additional: U+1E00..U+1EFF
  if (CP >= 0x1E00 && CP <= 0x1EFF) return true;
  // Latin Extended-A: U+0100..U+017F
  if (CP >= 0x0100 && CP <= 0x017F) return true;
  // Latin Extended-B: U+0180..U+024F
  if (CP >= 0x0180 && CP <= 0x024F) return true;

  // Greek and Coptic: U+0370..U+03FF
  if (CP >= 0x0370 && CP <= 0x03FF) return true;
  // Cyrillic: U+0400..U+04FF
  if (CP >= 0x0400 && CP <= 0x04FF) return true;
  // Arabic: U+0600..U+06FF
  if (CP >= 0x0600 && CP <= 0x06FF) return true;
  // Hebrew: U+0590..U+05FF
  if (CP >= 0x0590 && CP <= 0x05FF) return true;

  // Thai: U+0E00..U+0E7F
  if (CP >= 0x0E00 && CP <= 0x0E7F) return true;
  // Hiragana: U+3040..U+309F
  if (CP >= 0x3040 && CP <= 0x309F) return true;
  // Katakana: U+30A0..U+30FF
  if (CP >= 0x30A0 && CP <= 0x30FF) return true;
  // Hangul Syllables: U+AC00..U+D7AF
  if (CP >= 0xAC00 && CP <= 0xD7AF) return true;

  return false;
}

bool Lexer::isUnicodeIDContinue(uint32_t CP) {
  // ID_Continue includes ID_Start plus digits and some punctuation
  if (isUnicodeIDStart(CP)) return true;

  // ASCII digits
  if (CP >= '0' && CP <= '9') return true;

  // Combining Diacritical Marks: U+0300..U+036F
  if (CP >= 0x0300 && CP <= 0x036F) return true;
  // Combining Diacritical Marks Extended: U+1AB0..U+1AFF
  if (CP >= 0x1AB0 && CP <= 0x1AFF) return true;

  return false;
}

//===----------------------------------------------------------------------===//
// Error Recovery
//===----------------------------------------------------------------------===//

bool Lexer::recoverFromError() {
  // Skip to the next synchronization point (whitespace, newline, or semicolon)
  while (BufferPtr < BufferEnd) {
    char C = *BufferPtr;
    if (std::isspace(static_cast<unsigned char>(C)) || C == ';' || C == '}') {
      return true;
    }
    ++BufferPtr;
  }
  return false;
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
