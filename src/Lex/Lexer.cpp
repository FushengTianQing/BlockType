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
#include "blocktype/Basic/UTF8Validator.h"
#include "blocktype/Unicode/UnicodeData.h"
#include "llvm/Support/Debug.h"
#include <cctype>
#include <unordered_map>

#define DEBUG_TYPE "lexer"

namespace blocktype {

//===----------------------------------------------------------------------===//
// Keyword Lookup Tables
//===----------------------------------------------------------------------===//

// English keyword lookup table
// English keyword lookup table
// Use function-level static to avoid static initialization order fiasco
static const std::unordered_map<std::string, TokenKind>& getEnglishKeywords() {
  static const std::unordered_map<std::string, TokenKind> EnglishKeywords = {
#define KEYWORD(X, Y) {#X, TokenKind::kw_##X},
#include "blocktype/Lex/TokenKinds.def"
#undef KEYWORD
  };
  return EnglishKeywords;
}

// Chinese keyword lookup table
static const std::unordered_map<std::string, TokenKind>& getChineseKeywords() {
  static const std::unordered_map<std::string, TokenKind> ChineseKeywords = {
#define KEYWORD_ZH(X, Y) {#X, TokenKind::Y},
#include "blocktype/Lex/TokenKinds.def"
#undef KEYWORD_ZH
  };
  return ChineseKeywords;
}

//===----------------------------------------------------------------------===//
// Lexer Implementation
//===----------------------------------------------------------------------===//

Lexer::Lexer(SourceManager &SM, DiagnosticsEngine &Diags,
             StringRef Buffer, SourceLocation StartLoc)
    : SM(SM), Diags(Diags), BufferStart(Buffer.begin()),
      BufferEnd(Buffer.end()), BufferPtr(BufferStart),
      FileID(StartLoc.getFileID()) {
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

  // B2.7: Handle comments as tokens if KeepComments is set
  if (KeepComments && C == '/' && BufferPtr + 1 < BufferEnd) {
    char Next = *(BufferPtr + 1);
    if (Next == '/') {
      // Line comment
      BufferPtr += 2;
      while (BufferPtr < BufferEnd && *BufferPtr != '\n') {
        ++BufferPtr;
      }
      return formToken(Result, TokenKind::comment, TokStart);
    }
    if (Next == '*') {
      // Block comment
      BufferPtr += 2;
      while (BufferPtr + 1 < BufferEnd) {
        if (*BufferPtr == '*' && *(BufferPtr + 1) == '/') {
          BufferPtr += 2;
          return formToken(Result, TokenKind::comment, TokStart);
        }
        ++BufferPtr;
      }
      // Unterminated block comment
      BufferPtr = BufferEnd;
      return formToken(Result, TokenKind::comment, TokStart);
    }
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
    // #@ is an MSVC extension for charizing operator
    if (BufferPtr < BufferEnd && *BufferPtr == '@') {
      consumeChar();
      Diags.report(getSourceLocation(), DiagLevel::Warning,
                   "#@ is a Microsoft extension for charizing operator");
      return formToken(Result, TokenKind::hashat, TokStart);
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
    std::string ErrorMsg = "invalid character in source code: '";
    ErrorMsg += C;
    ErrorMsg += "'";
    Diags.report(getSourceLocation(), DiagLevel::Error, ErrorMsg);
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
  // Calculate offset from buffer start
  unsigned Offset = static_cast<unsigned>(BufferPtr - BufferStart);
  return SourceLocation::getFileLoc(FileID, Offset);
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

    // Line splicing (backslash-newline) - A2.3
    // This must be handled before other whitespace
    if (C == '\\') {
      if (BufferPtr + 1 < BufferEnd && *(BufferPtr + 1) == '\n') {
        // Remove backslash-newline pair
        BufferPtr += 2;
        continue;
      }
      // Also handle backslash-CR-LF (Windows line endings)
      if (BufferPtr + 2 < BufferEnd && *(BufferPtr + 1) == '\r' && *(BufferPtr + 2) == '\n') {
        BufferPtr += 3;
        continue;
      }
    }

    // Whitespace
    if (std::isspace(static_cast<unsigned char>(C))) {
      consumeChar();
      continue;
    }

    // Comments - B2.7: Skip only if not keeping comments
    if (C == '/') {
      if (BufferPtr + 1 < BufferEnd) {
        char Next = *(BufferPtr + 1);
        if (Next == '/') {
          // B2.7: If keeping comments, don't skip - let lexToken handle it
          if (KeepComments) {
            break;
          }
          skipLineComment();
          continue;
        }
        if (Next == '*') {
          // B2.7: If keeping comments, don't skip - let lexToken handle it
          if (KeepComments) {
            break;
          }
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

bool Lexer::processEscapeSequence() {
  // Assumes BufferPtr points to the character after backslash
  if (BufferPtr >= BufferEnd) return false;

  char C = *BufferPtr;

  // B2.5: C++23 escape sequence \e (ESC character, 0x1B)
  if (C == 'e' || C == 'E') {
    ++BufferPtr;
    return true;
  }

  // B2.6: Hex escape \xHH
  if (C == 'x') {
    ++BufferPtr;
    // E7.5.2.1 / Task 7.5.1: Check for delimited hex escape \x{HHHH...} (P2290R3)
    if (BufferPtr < BufferEnd && *BufferPtr == '{') {
      return processDelimitedHexEscape();
    }
    // Must have at least one hex digit
    if (BufferPtr < BufferEnd && std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
      ++BufferPtr;
      // Consume remaining hex digits (up to implementation limit)
      while (BufferPtr < BufferEnd && std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
        ++BufferPtr;
      }
      return true;
    }
    Diags.report(getSourceLocation(), DiagLevel::Error, 
                 "invalid hex escape sequence: expected hex digit after \\x");
    return false;
  }

  // B2.6: Octal escape \OOO (1-3 octal digits)
  if (C >= '0' && C <= '7') {
    ++BufferPtr;
    // Up to 2 more octal digits
    int digits = 1;
    while (digits < 3 && BufferPtr < BufferEnd && 
           *BufferPtr >= '0' && *BufferPtr <= '7') {
      ++BufferPtr;
      ++digits;
    }
    return true;
  }

  // Unicode escape \uXXXX
  if (C == 'u') {
    ++BufferPtr;
    for (int i = 0; i < 4 && BufferPtr < BufferEnd; ++i) {
      if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
        ++BufferPtr;
      } else {
        Diags.report(getSourceLocation(), DiagLevel::Error, 
                     "invalid universal character name: expected 4 hex digits after \\u");
        return false;
      }
    }
    return true;
  }

  // Unicode escape \UXXXXXXXX
  if (C == 'U') {
    ++BufferPtr;
    for (int i = 0; i < 8 && BufferPtr < BufferEnd; ++i) {
      if (std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
        ++BufferPtr;
      } else {
        Diags.report(getSourceLocation(), DiagLevel::Error, 
                     "invalid universal character name: expected 8 hex digits after \\U");
        return false;
      }
    }
    return true;
  }

  // E7.5.2.1 / Task 7.5.1: Named character escape \N{name} (P2071R2)
  if (C == 'N') {
    ++BufferPtr;
    return processNamedEscape();
  }

  // Simple escape sequences: \n, \t, \r, \a, \b, \f, \v, \\, \', \", \?
  ++BufferPtr;
  return true;
}

//===----------------------------------------------------------------------===//
// E7.5.2.1 / Task 7.5.1: Escape sequence extensions (P2290R3 + P2071R2)
//===----------------------------------------------------------------------===//

bool Lexer::processDelimitedHexEscape() {
  // We are positioned at '{'
  ++BufferPtr; // consume '{'

  // Must have at least one hex digit
  if (BufferPtr >= BufferEnd || !std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "invalid delimited escape sequence: expected hex digit after \\x{");
    return false;
  }

  // Collect hex digits
  unsigned Count = 0;
  uint32_t Value = 0;
  while (BufferPtr < BufferEnd && std::isxdigit(static_cast<unsigned char>(*BufferPtr))) {
    char D = *BufferPtr;
    ++BufferPtr;
    ++Count;
    Value <<= 4;
    if (D >= '0' && D <= '9') Value |= (D - '0');
    else if (D >= 'a' && D <= 'f') Value |= (D - 'a' + 10);
    else if (D >= 'A' && D <= 'F') Value |= (D - 'A' + 10);
  }

  if (Count == 0) {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "invalid delimited escape sequence: expected hex digit");
    return false;
  }

  // Expect '}'
  if (BufferPtr >= BufferEnd || *BufferPtr != '}') {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "invalid delimited escape sequence: expected '}'");
    return false;
  }
  ++BufferPtr; // consume '}'

  // Validate Unicode range
  if (Value > 0x10FFFF) {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "delimited escape sequence value too large for Unicode");
    return false;
  }

  return true;
}

bool Lexer::processNamedEscape() {
  // We are positioned after '\N'
  // Expect '{'
  if (BufferPtr >= BufferEnd || *BufferPtr != '{') {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "invalid named escape: expected '{' after \\N");
    return false;
  }
  ++BufferPtr; // consume '{'

  // Collect the character name
  const char *NameStart = BufferPtr;
  while (BufferPtr < BufferEnd && *BufferPtr != '}') {
    // Allow letters, digits, space, hyphen in character names
    char C = *BufferPtr;
    if (!std::isalnum(static_cast<unsigned char>(C)) && C != ' ' && C != '-') {
      Diags.report(getSourceLocation(), DiagLevel::Error,
                   "invalid character in named escape sequence");
      return false;
    }
    ++BufferPtr;
  }

  if (BufferPtr >= BufferEnd) {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "unterminated named escape sequence");
    return false;
  }

  llvm::StringRef Name(NameStart, BufferPtr - NameStart);
  ++BufferPtr; // consume '}'

  // Look up the name
  uint32_t CodePoint = lookupUnicodeName(Name);
  if (CodePoint == 0xFFFFFFFF) {
    Diags.report(getSourceLocation(), DiagLevel::Error,
                 "unknown Unicode character name: '" + Name.str() + "'");
    return false;
  }

  return true;
}

uint32_t Lexer::lookupUnicodeName(llvm::StringRef Name) {
  // Basic Unicode character name lookup table
  // Covers common ASCII/Latin and some extended characters
  struct NameEntry {
    const char *Name;
    uint32_t CodePoint;
  };

  // Sorted for potential binary search; common characters
  static const NameEntry Names[] = {
      {"AMPERSAND", 0x0026},
      {"APOSTROPHE", 0x0027},
      {"ASTERISK", 0x002A},
      {"BACKSLASH", 0x005C},
      {"CARRIAGE RETURN", 0x000D},
      {"CENT SIGN", 0x00A2},
      {"COLON", 0x003A},
      {"COMMA", 0x002C},
      {"COMMERCIAL AT", 0x0040},
      {"COPYRIGHT SIGN", 0x00A9},
      {"DEGREE SIGN", 0x00B0},
      {"DIGIT EIGHT", 0x0038},
      {"DIGIT FIVE", 0x0035},
      {"DIGIT FOUR", 0x0034},
      {"DIGIT NINE", 0x0039},
      {"DIGIT ONE", 0x0031},
      {"DIGIT SEVEN", 0x0037},
      {"DIGIT SIX", 0x0036},
      {"DIGIT THREE", 0x0033},
      {"DIGIT TWO", 0x0032},
      {"DIGIT ZERO", 0x0030},
      {"DOLLAR SIGN", 0x0024},
      {"EMOJI GRINNING FACE", 0x1F600},
      {"EQUALS SIGN", 0x003D},
      {"EXCLAMATION MARK", 0x0021},
      {"FULL STOP", 0x002E},
      {"FULLWIDTH DIGIT ZERO", 0xFF10},
      {"GRAVE ACCENT", 0x0060},
      {"GREATER-THAN SIGN", 0x003E},
      {"HYPHEN-MINUS", 0x002D},
      {"LATIN CAPITAL LETTER A", 0x0041},
      {"LATIN CAPITAL LETTER B", 0x0042},
      {"LATIN CAPITAL LETTER C", 0x0043},
      {"LATIN CAPITAL LETTER D", 0x0044},
      {"LATIN CAPITAL LETTER E", 0x0045},
      {"LATIN CAPITAL LETTER F", 0x0046},
      {"LATIN CAPITAL LETTER G", 0x0047},
      {"LATIN CAPITAL LETTER H", 0x0048},
      {"LATIN CAPITAL LETTER I", 0x0049},
      {"LATIN CAPITAL LETTER J", 0x004A},
      {"LATIN CAPITAL LETTER K", 0x004B},
      {"LATIN CAPITAL LETTER L", 0x004C},
      {"LATIN CAPITAL LETTER M", 0x004D},
      {"LATIN CAPITAL LETTER N", 0x004E},
      {"LATIN CAPITAL LETTER O", 0x004F},
      {"LATIN CAPITAL LETTER P", 0x0050},
      {"LATIN CAPITAL LETTER Q", 0x0051},
      {"LATIN CAPITAL LETTER R", 0x0052},
      {"LATIN CAPITAL LETTER S", 0x0053},
      {"LATIN CAPITAL LETTER T", 0x0054},
      {"LATIN CAPITAL LETTER U", 0x0055},
      {"LATIN CAPITAL LETTER V", 0x0056},
      {"LATIN CAPITAL LETTER W", 0x0057},
      {"LATIN CAPITAL LETTER X", 0x0058},
      {"LATIN CAPITAL LETTER Y", 0x0059},
      {"LATIN CAPITAL LETTER Z", 0x005A},
      {"LATIN SMALL LETTER A", 0x0061},
      {"LATIN SMALL LETTER B", 0x0062},
      {"LATIN SMALL LETTER C", 0x0063},
      {"LATIN SMALL LETTER D", 0x0064},
      {"LATIN SMALL LETTER E", 0x0065},
      {"LATIN SMALL LETTER F", 0x0066},
      {"LATIN SMALL LETTER G", 0x0067},
      {"LATIN SMALL LETTER H", 0x0068},
      {"LATIN SMALL LETTER I", 0x0069},
      {"LATIN SMALL LETTER J", 0x006A},
      {"LATIN SMALL LETTER K", 0x006B},
      {"LATIN SMALL LETTER L", 0x006C},
      {"LATIN SMALL LETTER M", 0x006D},
      {"LATIN SMALL LETTER N", 0x006E},
      {"LATIN SMALL LETTER O", 0x006F},
      {"LATIN SMALL LETTER P", 0x0070},
      {"LATIN SMALL LETTER Q", 0x0071},
      {"LATIN SMALL LETTER R", 0x0072},
      {"LATIN SMALL LETTER S", 0x0073},
      {"LATIN SMALL LETTER T", 0x0074},
      {"LATIN SMALL LETTER U", 0x0075},
      {"LATIN SMALL LETTER V", 0x0076},
      {"LATIN SMALL LETTER W", 0x0077},
      {"LATIN SMALL LETTER X", 0x0078},
      {"LATIN SMALL LETTER Y", 0x0079},
      {"LATIN SMALL LETTER Z", 0x007A},
      {"LEFT CURLY BRACKET", 0x007B},
      {"LEFT PARENTHESIS", 0x0028},
      {"LEFT SQUARE BRACKET", 0x005B},
      {"LESS-THAN SIGN", 0x003C},
      {"LINE FEED", 0x000A},
      {"LOW LINE", 0x005F},
      {"MICRO SIGN", 0x00B5},
      {"NO-BREAK SPACE", 0x00A0},
      {"NUMBER SIGN", 0x0023},
      {"PERCENT SIGN", 0x0025},
      {"PLUS SIGN", 0x002B},
      {"QUESTION MARK", 0x003F},
      {"QUOTATION MARK", 0x0022},
      {"REGISTERED SIGN", 0x00AE},
      {"RIGHT CURLY BRACKET", 0x007D},
      {"RIGHT PARENTHESIS", 0x0029},
      {"RIGHT SQUARE BRACKET", 0x005D},
      {"SEMICOLON", 0x003B},
      {"SOLIDUS", 0x002F},
      {"SPACE", 0x0020},
      {"TILDE", 0x007E},
      {"VERTICAL LINE", 0x007C},
      {"YEN SIGN", 0x00A5},
  };

  // Linear search (sufficient for the small table)
  for (const auto &Entry : Names) {
    if (Name == Entry.Name) {
      return Entry.CodePoint;
    }
  }
  return 0xFFFFFFFF; // Not found
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
    // Validate UTF-8 sequence first
    const char *UTF8Start = BufferPtr;
    
    // Find the length of the potential UTF-8 sequence
    while (BufferPtr < BufferEnd && 
           static_cast<unsigned char>(*BufferPtr) >= 0x80) {
      ++BufferPtr;
    }
    size_t SeqLen = BufferPtr - UTF8Start;
    
    // Validate the UTF-8 sequence
    if (!blocktype::UTF8Validator::validate(UTF8Start, SeqLen)) {
      // Invalid UTF-8 - report error with Fix-It hint
      SourceLocation ErrorLoc = getSourceLocation();
      Diags.report(ErrorLoc, DiagLevel::Error, 
                   "invalid UTF-8 sequence in identifier");
      BufferPtr = UTF8Start + 1; // Skip one byte and continue
      return formToken(Result, TokenKind::unknown, Start);
    }
    
    // Reset and process with validated UTF-8
    BufferPtr = UTF8Start;
    
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
          if (!unicode::isIDStart(CP)) {
            BufferPtr = SavedPtr;
            break;
          }
        } else {
          if (!unicode::isIDContinue(CP)) {
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
      if (std::isalnum(static_cast<unsigned char>(C)) || C == '_' ||
          C == '@' || C == '$' || C == '`') {
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
      const char *HexStart = BufferPtr;  // 记录十六进制数字开始位置
      while (BufferPtr < BufferEnd) {
        char C = std::tolower(static_cast<unsigned char>(*BufferPtr));
        if (std::isxdigit(static_cast<unsigned char>(C)) || C == '\'') {
          ++BufferPtr;
        } else {
          break;
        }
      }
      
      // 错误恢复：检查0x后是否有十六进制数字
      if (BufferPtr == HexStart) {
        Diags.report(getSourceLocation(), DiagLevel::Error,
                     "hexadecimal literal requires at least one hexadecimal digit after '0x'");
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
          const char *ExpStart = BufferPtr;  // 记录指数数字开始位置
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
          // 错误恢复：检查p/P后是否有指数数字
          if (BufferPtr == ExpStart || (BufferPtr == ExpStart + 1 && 
              (*ExpStart == '+' || *ExpStart == '-'))) {
            Diags.report(getSourceLocation(), DiagLevel::Error,
                         "hexadecimal floating literal requires exponent digits after 'p'");
          }
        }
      }
    } else if (Next == 'b' || Next == 'B') {
      // Binary (C++14)
      BufferPtr += 2;
      const char *BinStart = BufferPtr;  // 记录二进制数字开始位置
      while (BufferPtr < BufferEnd) {
        char C = *BufferPtr;
        if (C == '0' || C == '1' || C == '\'') {
          ++BufferPtr;
        } else {
          break;
        }
      }
      // 错误恢复：检查0b后是否有二进制数字
      if (BufferPtr == BinStart) {
        Diags.report(getSourceLocation(), DiagLevel::Error,
                     "binary literal requires at least one binary digit after '0b'");
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

  // D2: Validate digit separators
  // Check for invalid separator positions:
  // - Cannot start with separator: '123
  // - Cannot end with separator: 123'
  // - Cannot have consecutive separators: 1''23
  // - Cannot have separator adjacent to radix prefix: 0x'FF or 0b'10
  // - Cannot have separator adjacent to decimal point: 1'.23 or 1.'23
  // - Cannot have separator adjacent to exponent: 1e'+10 or 1e1'0
  StringRef LiteralText(Start, BufferPtr - Start);
  for (size_t i = 0; i < LiteralText.size(); ++i) {
    if (LiteralText[i] == '\'') {
      // Check if at start (but after prefix)
      if (i == 0) {
        Diags.report(getSourceLocation(), DiagLevel::Error,
                     "digit separator cannot appear at the start of a numeric literal");
        break;
      }
      // Check if at end
      if (i == LiteralText.size() - 1) {
        Diags.report(getSourceLocation(), DiagLevel::Error,
                     "digit separator cannot appear at the end of a numeric literal");
        break;
      }
      // Check for consecutive separators
      if (LiteralText[i - 1] == '\'') {
        Diags.report(getSourceLocation(), DiagLevel::Error,
                     "consecutive digit separators are not allowed");
        break;
      }
      // Check if separator is after radix prefix (0x, 0b, 0B, 0X)
      if (i == 2 && LiteralText[0] == '0' && 
          (LiteralText[1] == 'x' || LiteralText[1] == 'X' || 
           LiteralText[1] == 'b' || LiteralText[1] == 'B')) {
        Diags.report(getSourceLocation(), DiagLevel::Error,
                     "digit separator cannot appear immediately after radix prefix");
        break;
      }
      // Check if separator is adjacent to decimal point
      if (LiteralText[i - 1] == '.' || LiteralText[i + 1] == '.') {
        Diags.report(getSourceLocation(), DiagLevel::Error,
                     "digit separator cannot be adjacent to decimal point");
        break;
      }
      // Check if separator is adjacent to exponent sign
      char Prev = LiteralText[i - 1];
      char Next = LiteralText[i + 1];
      if (Prev == 'e' || Prev == 'E' || Prev == 'p' || Prev == 'P' ||
          Next == 'e' || Next == 'E' || Next == 'p' || Next == 'P') {
        Diags.report(getSourceLocation(), DiagLevel::Error,
                     "digit separator cannot be adjacent to exponent");
        break;
      }
      // Check if separator is after exponent sign (+/-)
      if ((Prev == '+' || Prev == '-') && i >= 2) {
        char PrevPrev = LiteralText[i - 2];
        if (PrevPrev == 'e' || PrevPrev == 'E' || PrevPrev == 'p' || PrevPrev == 'P') {
          Diags.report(getSourceLocation(), DiagLevel::Error,
                       "digit separator cannot appear immediately after exponent sign");
          break;
        }
      }
    }
  }

  // Suffix - B2.4: User-defined literal suffix
  // Standard suffixes (like ULL, f, L) and user-defined suffixes (starting with _)
  const char *SuffixStart = BufferPtr;
  bool HasUserDefinedSuffix = false;
  while (BufferPtr < BufferEnd) {
    char C = *BufferPtr;
    if (std::isalnum(static_cast<unsigned char>(C)) || C == '_') {
      if (C == '_' && BufferPtr == SuffixStart) {
        HasUserDefinedSuffix = true;
      }
      ++BufferPtr;
    } else {
      break;
    }
  }
  
  // 验证用户定义字面量后缀
  if (HasUserDefinedSuffix) {
    StringRef Suffix(SuffixStart, BufferPtr - SuffixStart);
    // 后缀必须以_开头（已检查）
    // 后缀必须至少有2个字符（_ + 至少一个字符）
    if (Suffix.size() < 2) {
      Diags.report(getSourceLocation(), DiagLevel::Error,
                   "user-defined literal suffix must have at least one character after '_'");
    }
    // 后续字符必须是有效的标识符字符（已由上面的循环保证）
    // 检查第二个字符是否为数字（不允许：_123）
    if (Suffix.size() >= 2 && std::isdigit(static_cast<unsigned char>(Suffix[1]))) {
      Diags.report(getSourceLocation(), DiagLevel::Error,
                   "user-defined literal suffix cannot start with a digit after '_'");
    }
  }

  // Determine token kind based on suffix
  TokenKind Kind = TokenKind::numeric_constant;
  if (HasUserDefinedSuffix) {
    // Check if it's floating or integer based on whether it has a decimal point or exponent
    bool IsFloat = false;
    for (const char *p = Start; p < SuffixStart; ++p) {
      if (*p == '.' || *p == 'e' || *p == 'E' || *p == 'p' || *p == 'P') {
        IsFloat = true;
        break;
      }
    }
    Kind = IsFloat ? TokenKind::user_defined_floating_literal 
                   : TokenKind::user_defined_integer_literal;
  }

  return formToken(Result, Kind, Start);
}

bool Lexer::lexCharConstant(Token &Result, const char *Start) {
  assert(*BufferPtr == '\'');
  ++BufferPtr; // Skip opening quote

  while (BufferPtr < BufferEnd && *BufferPtr != '\'') {
    if (*BufferPtr == '\\') {
      ++BufferPtr; // Skip backslash
      processEscapeSequence();
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

  // B2.4: Check for user-defined literal suffix
  if (BufferPtr < BufferEnd && *BufferPtr == '_') {
    const char *SuffixStart = BufferPtr;
    while (BufferPtr < BufferEnd) {
      char C = *BufferPtr;
      if (std::isalnum(static_cast<unsigned char>(C)) || C == '_') {
        ++BufferPtr;
      } else {
        break;
      }
    }
    // 验证用户定义字面量后缀
    StringRef Suffix(SuffixStart, BufferPtr - SuffixStart);
    if (Suffix.size() < 2) {
      Diags.report(getSourceLocation(), DiagLevel::Error,
                   "user-defined literal suffix must have at least one character after '_'");
    }
    if (Suffix.size() >= 2 && std::isdigit(static_cast<unsigned char>(Suffix[1]))) {
      Diags.report(getSourceLocation(), DiagLevel::Error,
                   "user-defined literal suffix cannot start with a digit after '_'");
    }
    return formToken(Result, TokenKind::user_defined_char_literal, Start);
  }

  return formToken(Result, TokenKind::char_constant, Start);
}

bool Lexer::lexStringLiteral(Token &Result, const char *Start) {
  assert(*BufferPtr == '"');
  ++BufferPtr; // Skip opening quote

  while (BufferPtr < BufferEnd && *BufferPtr != '"') {
    if (*BufferPtr == '\\') {
      ++BufferPtr; // Skip backslash
      processEscapeSequence();
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

  // B2.4: Check for user-defined literal suffix
  if (BufferPtr < BufferEnd && *BufferPtr == '_') {
    const char *SuffixStart = BufferPtr;
    while (BufferPtr < BufferEnd) {
      char C = *BufferPtr;
      if (std::isalnum(static_cast<unsigned char>(C)) || C == '_') {
        ++BufferPtr;
      } else {
        break;
      }
    }
    // 验证用户定义字面量后缀
    StringRef Suffix(SuffixStart, BufferPtr - SuffixStart);
    if (Suffix.size() < 2) {
      Diags.report(getSourceLocation(), DiagLevel::Error,
                   "user-defined literal suffix must have at least one character after '_'");
    }
    if (Suffix.size() >= 2 && std::isdigit(static_cast<unsigned char>(Suffix[1]))) {
      Diags.report(getSourceLocation(), DiagLevel::Error,
                   "user-defined literal suffix cannot start with a digit after '_'");
    }
    return formToken(Result, TokenKind::user_defined_string_literal, Start);
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
  // For UTF-8 prefix (u8), check after consuming '8'
  // For other prefixes (L, u, U), check if next char is 'R'
  if (BufferPtr < BufferEnd && *BufferPtr == 'R') {
    ++BufferPtr;
    if (BufferPtr < BufferEnd && *BufferPtr == '"') {
      // This is a raw string literal with UTF prefix
      if (!lexRawStringLiteral(Result, Start)) {
        return false;
      }
      // Change the token kind to the appropriate UTF string kind
      if (Result.getKind() == TokenKind::string_literal) {
        if (IsUTF8) {
          Result.setKind(TokenKind::utf8_string_literal);
        } else if (Prefix == 'L') {
          Result.setKind(TokenKind::wide_string_literal);
        } else if (Prefix == 'u') {
          Result.setKind(TokenKind::utf16_string_literal);
        } else if (Prefix == 'U') {
          Result.setKind(TokenKind::utf32_string_literal);
        }
      }
      return true;
    }
    // Not a raw string, backtrack
    --BufferPtr;
  }
  
  // Check for raw string literal without UTF prefix (just R"...")
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
        processEscapeSequence();
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

    // B2.4: Check for user-defined literal suffix
    if (BufferPtr < BufferEnd && *BufferPtr == '_') {
      while (BufferPtr < BufferEnd) {
        char C = *BufferPtr;
        if (std::isalnum(static_cast<unsigned char>(C)) || C == '_') {
          ++BufferPtr;
        } else {
          break;
        }
      }
      return formToken(Result, TokenKind::user_defined_char_literal, Start);
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
        processEscapeSequence();
      } else {
        ++BufferPtr;
      }
    }

    if (BufferPtr < BufferEnd && *BufferPtr == '"') {
      ++BufferPtr;
    }

    // B2.4: Check for user-defined literal suffix
    if (BufferPtr < BufferEnd && *BufferPtr == '_') {
      while (BufferPtr < BufferEnd) {
        char C = *BufferPtr;
        if (std::isalnum(static_cast<unsigned char>(C)) || C == '_') {
          ++BufferPtr;
        } else {
          break;
        }
      }
      return formToken(Result, TokenKind::user_defined_string_literal, Start);
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

  // D4: Validate delimiter length (max 16 characters)
  if (Delimiter.size() > 16) {
    Diags.report(getSourceLocation(), DiagLevel::Error, 
                 "raw string delimiter exceeds maximum length of 16 characters");
    // Continue to find end of literal for error recovery
  }

  // Validate delimiter characters (must not contain: ), \, space, or control characters)
  for (char C : Delimiter) {
    if (C == ')' || C == '\\' || C == ' ' || 
        (static_cast<unsigned char>(C) >= 0x00 && static_cast<unsigned char>(C) <= 0x1F)) {
      Diags.report(getSourceLocation(), DiagLevel::Error,
                   "invalid character in raw string delimiter");
      break;
    }
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
  const auto& EnglishKeywords = getEnglishKeywords();
  
  // Debug: Print keyword map size and check for 'int'
  LLVM_DEBUG({
    static bool Debugged = false;
    if (!Debugged) {
      llvm::dbgs() << "EnglishKeywords size: " << EnglishKeywords.size() << "\n";
      auto It = EnglishKeywords.find("int");
      if (It != EnglishKeywords.end()) {
        llvm::dbgs() << "'int' found in EnglishKeywords, kind: " << static_cast<int>(It->second) << "\n";
      } else {
        llvm::dbgs() << "'int' NOT found in EnglishKeywords!\n";
      }
      Debugged = true;
    }
  });
  
  auto EnIt = EnglishKeywords.find(Identifier.str());
  if (EnIt != EnglishKeywords.end()) {
    return EnIt->second;
  }

  // Check Chinese keywords
  const auto& ChineseKeywords = getChineseKeywords();
  auto ZhIt = ChineseKeywords.find(Identifier.str());
  if (ZhIt != ChineseKeywords.end()) {
    return ZhIt->second;
  }

  return TokenKind::identifier;
}

TokenKind Lexer::getChineseKeywordKind(StringRef Keyword) {
  const auto& ChineseKeywords = getChineseKeywords();
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
  // P2558R2: Allow @, $, and backtick as identifier continue characters
  return std::isalnum(static_cast<unsigned char>(C)) || C == '_' ||
         C == '@' || C == '$' || C == '`';
}

bool Lexer::isUnicodeIDStart(uint32_t CodePoint) {
  return unicode::isIDStart(CodePoint);
}

bool Lexer::isUnicodeIDContinue(uint32_t CodePoint) {
  return unicode::isIDContinue(CodePoint);
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
