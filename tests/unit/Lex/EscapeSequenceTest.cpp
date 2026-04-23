//===--- EscapeSequenceTest.cpp - Escape sequence extension tests --------===//
//
// Part of the BlockType Project.
// Task 7.5.1: Escape sequence extensions (P2290R3 + P2071R2)
//
//===--------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class EscapeSequenceTest : public ::testing::Test {
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
    Tokens.push_back(T); // Add EOF token
    
    return Tokens;
  }
};

// Test 1: Delimited hex escape \x{41} in string literal
TEST_F(EscapeSequenceTest, DelimitedHexBasic) {
  auto Tokens = lex("\"\\x{41}\"");
  ASSERT_GE(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Diags.getNumErrors(), 0u);
}

// Test 2: Delimited hex escape with multiple hex digits \x{1F600}
TEST_F(EscapeSequenceTest, DelimitedHexEmoji) {
  auto Tokens = lex("\"\\x{1F600}\"");
  ASSERT_GE(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Diags.getNumErrors(), 0u);
}

// Test 3: Named escape \N{LATIN CAPITAL LETTER A}
TEST_F(EscapeSequenceTest, NamedEscapeBasic) {
  auto Tokens = lex("\"\\N{LATIN CAPITAL LETTER A}\"");
  ASSERT_GE(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Diags.getNumErrors(), 0u);
}

// Test 4: Named escape \N{SPACE}
TEST_F(EscapeSequenceTest, NamedEscapeSpace) {
  auto Tokens = lex("\"\\N{SPACE}\"");
  ASSERT_GE(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Diags.getNumErrors(), 0u);
}

// Test 5: Named escape \N{LATIN SMALL LETTER Z}
TEST_F(EscapeSequenceTest, NamedEscapeLowerZ) {
  auto Tokens = lex("\"\\N{LATIN SMALL LETTER Z}\"");
  ASSERT_GE(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Diags.getNumErrors(), 0u);
}

// Test 6: Traditional \x still works
TEST_F(EscapeSequenceTest, TraditionalHexEscape) {
  auto Tokens = lex("\"\\x41\"");
  ASSERT_GE(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Diags.getNumErrors(), 0u);
}

// Test 7: Traditional \n still works
TEST_F(EscapeSequenceTest, TraditionalEscapeSequences) {
  auto Tokens = lex("\"\\n\\t\\r\"");
  ASSERT_GE(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Diags.getNumErrors(), 0u);
}

// Test 8: lookupUnicodeName returns correct code points
TEST_F(EscapeSequenceTest, LookupUnicodeName) {
  EXPECT_EQ(Lexer::lookupUnicodeName("LATIN CAPITAL LETTER A"), 0x0041u);
  EXPECT_EQ(Lexer::lookupUnicodeName("LATIN SMALL LETTER Z"), 0x007Au);
  EXPECT_EQ(Lexer::lookupUnicodeName("SPACE"), 0x0020u);
  EXPECT_EQ(Lexer::lookupUnicodeName("DIGIT ZERO"), 0x0030u);
  EXPECT_EQ(Lexer::lookupUnicodeName("COPYRIGHT SIGN"), 0x00A9u);
  EXPECT_EQ(Lexer::lookupUnicodeName("NONEXISTENT NAME"), 0xFFFFFFFFu);
}

// Test 9: Invalid named escape produces error
TEST_F(EscapeSequenceTest, InvalidNamedEscape) {
  auto Tokens = lex("\"\\N{UNKNOWN CHARACTER XYZ}\"");
  // Should still produce a token but with an error
  EXPECT_GT(Diags.getNumErrors(), 0u);
}

// Test 10: Mix of escape sequences in one string
TEST_F(EscapeSequenceTest, MixedEscapes) {
  auto Tokens = lex("\"\\x{41}\\N{SPACE}\\x42\\n\"");
  ASSERT_GE(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Diags.getNumErrors(), 0u);
}

} // anonymous namespace
