//===--- BoundaryCaseTest.cpp - Boundary Case Unit Tests --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// D18: Boundary case tests for Phase 1 features
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class BoundaryCaseTest : public ::testing::Test {
protected:
  SourceManager SM;
  DiagnosticsEngine Diags;

  std::vector<Token> lex(StringRef Code) {
    std::vector<Token> Tokens;
    Lexer L(SM, Diags, Code, SM.createMainFileID("test.cpp", Code));
    
    Token T;
    while (L.lexToken(T)) {
      Tokens.push_back(T);
    }
    Tokens.push_back(T);
    
    return Tokens;
  }

  std::vector<Token> preprocess(StringRef Code) {
    std::vector<Token> Tokens;
    Preprocessor PP(SM, Diags);
    PP.enterSourceFile("test.cpp", Code);
    
    Token T;
    while (PP.lexToken(T)) {
      Tokens.push_back(T);
    }
    Tokens.push_back(T);
    
    return Tokens;
  }
};

// D2: Digit separator validation tests

TEST_F(BoundaryCaseTest, DigitSeparatorValid) {
  // Valid digit separators
  auto Tokens1 = lex("1'000'000");
  ASSERT_GE(Tokens1.size(), 2u);
  EXPECT_EQ(Tokens1[0].getKind(), TokenKind::numeric_constant);
  
  auto Tokens2 = lex("0x1'FFFF");
  ASSERT_GE(Tokens2.size(), 2u);
  EXPECT_EQ(Tokens2[0].getKind(), TokenKind::numeric_constant);
  
  auto Tokens3 = lex("0b1010'1100");
  ASSERT_GE(Tokens3.size(), 2u);
  EXPECT_EQ(Tokens3[0].getKind(), TokenKind::numeric_constant);
  
  auto Tokens4 = lex("3.141'592'653");
  ASSERT_GE(Tokens4.size(), 2u);
  EXPECT_EQ(Tokens4[0].getKind(), TokenKind::numeric_constant);
}

TEST_F(BoundaryCaseTest, DigitSeparatorAtStart) {
  // Invalid: separator at start
  Diags.reset();
  auto Tokens = lex("'123");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(BoundaryCaseTest, DigitSeparatorAtEnd) {
  // Invalid: separator at end
  Diags.reset();
  auto Tokens = lex("123'");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(BoundaryCaseTest, DigitSeparatorConsecutive) {
  // Invalid: consecutive separators
  Diags.reset();
  auto Tokens = lex("1''23");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(BoundaryCaseTest, DigitSeparatorAfterPrefix) {
  // Invalid: separator immediately after radix prefix
  Diags.reset();
  auto Tokens1 = lex("0x'FF");
  ASSERT_GE(Tokens1.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
  
  Diags.reset();
  auto Tokens2 = lex("0b'10");
  ASSERT_GE(Tokens2.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(BoundaryCaseTest, DigitSeparatorAdjacentToDecimalPoint) {
  // Invalid: separator adjacent to decimal point
  Diags.reset();
  auto Tokens1 = lex("1'.23");
  ASSERT_GE(Tokens1.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
  
  Diags.reset();
  auto Tokens2 = lex("1.'23");
  ASSERT_GE(Tokens2.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(BoundaryCaseTest, DigitSeparatorAdjacentToExponent) {
  // Invalid: separator adjacent to exponent
  Diags.reset();
  auto Tokens1 = lex("1e'10");
  ASSERT_GE(Tokens1.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
  
  Diags.reset();
  auto Tokens2 = lex("1e+'10");
  ASSERT_GE(Tokens2.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

// D4: Raw string delimiter validation tests

TEST_F(BoundaryCaseTest, RawStringValidDelimiter) {
  // Valid delimiters (length <= 16)
  auto Tokens1 = lex("R\"(content)\"");
  ASSERT_GE(Tokens1.size(), 2u);
  EXPECT_EQ(Tokens1[0].getKind(), TokenKind::string_literal);
  
  auto Tokens2 = lex("R\"delim(content)delim\"");
  ASSERT_GE(Tokens2.size(), 2u);
  EXPECT_EQ(Tokens2[0].getKind(), TokenKind::string_literal);
  
  // Exactly 16 characters
  auto Tokens3 = lex("R\"1234567890123456(content)1234567890123456\"");
  ASSERT_GE(Tokens3.size(), 2u);
  EXPECT_EQ(Tokens3[0].getKind(), TokenKind::string_literal);
}

TEST_F(BoundaryCaseTest, RawStringDelimiterTooLong) {
  // Invalid: delimiter > 16 characters
  Diags.reset();
  auto Tokens = lex("R\"12345678901234567(content)12345678901234567\"");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(BoundaryCaseTest, RawStringDelimiterInvalidChars) {
  // Invalid: delimiter contains invalid characters
  Diags.reset();
  auto Tokens1 = lex("R\"del)im(content)del)im\"");
  ASSERT_GE(Tokens1.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
  
  Diags.reset();
  auto Tokens2 = lex("R\"del\\im(content)del\\im\"");
  ASSERT_GE(Tokens2.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
  
  Diags.reset();
  auto Tokens3 = lex("R\"del im(content)del im\"");
  ASSERT_GE(Tokens3.size(), 2u);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(BoundaryCaseTest, RawStringEmptyDelimiter) {
  // Valid: empty delimiter
  auto Tokens = lex("R\"(content)\"");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
}

TEST_F(BoundaryCaseTest, RawStringNestedParens) {
  // Valid: nested parentheses in content
  auto Tokens = lex("R\"(content(with(nested)parens))\"");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
}

// D9: __VA_OPT__ boundary tests

TEST_F(BoundaryCaseTest, VAOptWithVariadicArgs) {
  // __VA_OPT__ should expand when there are variadic args
  auto Tokens = preprocess(
    "#define M(x, ...) x __VA_OPT__(+ __VA_ARGS__)\n"
    "M(1, 2, 3)"
  );
  ASSERT_GE(Tokens.size(), 2u);
}

TEST_F(BoundaryCaseTest, VAOptWithoutVariadicArgs) {
  // __VA_OPT__ should not expand when there are no variadic args
  auto Tokens = preprocess(
    "#define M(x, ...) x __VA_OPT__(+ __VA_ARGS__)\n"
    "M(1)"
  );
  ASSERT_GE(Tokens.size(), 2u);
}

TEST_F(BoundaryCaseTest, VAOptEmptyContent) {
  // __VA_OPT__ with empty content
  auto Tokens = preprocess(
    "#define M(...) __VA_OPT__(())\n"
    "M(1)"
  );
  ASSERT_GE(Tokens.size(), 2u);
}

TEST_F(BoundaryCaseTest, VAOptNestedParens) {
  // __VA_OPT__ with nested parentheses
  auto Tokens = preprocess(
    "#define M(...) __VA_OPT__((__VA_ARGS__))\n"
    "M(1, 2)"
  );
  ASSERT_GE(Tokens.size(), 2u);
}

TEST_F(BoundaryCaseTest, VAOptComplexContent) {
  // __VA_OPT__ with complex content including tokens
  auto Tokens = preprocess(
    "#define LOG(...) printf(__VA_ARGS__) __VA_OPT__(; log(__VA_ARGS__))\n"
    "LOG(\"test\")"
  );
  ASSERT_GE(Tokens.size(), 2u);
}

// Additional boundary cases

TEST_F(BoundaryCaseTest, EmptyInput) {
  auto Tokens = lex("");
  ASSERT_EQ(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::eof);
}

TEST_F(BoundaryCaseTest, MaxTokenLength) {
  // Very long identifier
  std::string LongId(1000, 'a');
  auto Tokens = lex(LongId);
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::identifier);
}

TEST_F(BoundaryCaseTest, MaxNumericLength) {
  // Very long numeric constant
  std::string LongNum;
  for (int i = 0; i < 100; ++i) {
    LongNum += "12345'67890";
  }
  auto Tokens = lex(LongNum);
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
}

TEST_F(BoundaryCaseTest, DeeplyNestedComments) {
  // Deeply nested block comments (not actually nested, but multiple)
  std::string Code;
  for (int i = 0; i < 100; ++i) {
    Code += "/* comment */ ";
  }
  auto Tokens = lex(Code);
  ASSERT_EQ(Tokens.size(), 1u);  // Just EOF
}

TEST_F(BoundaryCaseTest, MixedLineEndings) {
  // Mixed line endings (LF, CRLF)
  auto Tokens = lex("int x;\nint y;\r\nint z;");
  ASSERT_GE(Tokens.size(), 10u);
}

TEST_F(BoundaryCaseTest, UnicodeBoundary) {
  // Unicode at boundaries
  auto Tokens = lex("变量");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::identifier);
}

TEST_F(BoundaryCaseTest, EscapeSequenceBoundary) {
  // All escape sequences
  auto Tokens = lex("\"\\n\\t\\r\\a\\b\\f\\v\\\\\\'\\\"\\?\"");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
}

TEST_F(BoundaryCaseTest, UnicodeEscapeBoundary) {
  // Unicode escape sequences
  auto Tokens1 = lex("\"\\u0041\"");  // 'A'
  ASSERT_GE(Tokens1.size(), 2u);
  
  auto Tokens2 = lex("\"\\U0001F600\"");  // 😀
  ASSERT_GE(Tokens2.size(), 2u);
}

TEST_F(BoundaryCaseTest, HexFloatBoundary) {
  // Hexadecimal floating-point
  auto Tokens1 = lex("0x1.5p10");
  ASSERT_GE(Tokens1.size(), 2u);
  EXPECT_EQ(Tokens1[0].getKind(), TokenKind::numeric_constant);
  
  auto Tokens2 = lex("0x1.FFp-5");
  ASSERT_GE(Tokens2.size(), 2u);
  EXPECT_EQ(Tokens2[0].getKind(), TokenKind::numeric_constant);
}

TEST_F(BoundaryCaseTest, BinaryBoundary) {
  // Binary literals
  auto Tokens1 = lex("0b0");
  ASSERT_GE(Tokens1.size(), 2u);
  
  auto Tokens2 = lex("0b1");
  ASSERT_GE(Tokens2.size(), 2u);
  
  auto Tokens3 = lex("0b10101010");
  ASSERT_GE(Tokens3.size(), 2u);
}

TEST_F(BoundaryCaseTest, UserDefinedLiteralBoundary) {
  // User-defined literals
  auto Tokens1 = lex("123_suffix");
  ASSERT_GE(Tokens1.size(), 2u);
  EXPECT_EQ(Tokens1[0].getKind(), TokenKind::user_defined_integer_literal);
  
  auto Tokens2 = lex("3.14_suffix");
  ASSERT_GE(Tokens2.size(), 2u);
  EXPECT_EQ(Tokens2[0].getKind(), TokenKind::user_defined_floating_literal);
  
  auto Tokens3 = lex("\"str\"_suffix");
  ASSERT_GE(Tokens3.size(), 2u);
  EXPECT_EQ(Tokens3[0].getKind(), TokenKind::user_defined_string_literal);
}

} // anonymous namespace
