// 测试Lexer中优先级功能
// 验证Digraphs和原始字符串字面量的实现

#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include <gtest/gtest.h>

using namespace blocktype;

class LexerMediumPriorityTest : public ::testing::Test {
protected:
  SourceManager SM;
  DiagnosticsEngine Diags;
  
  std::vector<Token> lex(StringRef Code) {
    auto Loc = SM.createMainFileID("test.cpp", Code);
    Lexer Lexer(SM, Diags, Code, Loc);
    
    std::vector<Token> Tokens;
    Token Tok;
    while (Lexer.lexToken(Tok)) {
      Tokens.push_back(Tok);
    }
    Tokens.push_back(Tok); // Include EOF
    return Tokens;
  }
};

// ========== 测试Digraphs ==========

TEST_F(LexerMediumPriorityTest, DigraphLessPercent) {
  // 测试 <% -> {
  auto Tokens = lex("<%");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::lesspercent);
}

TEST_F(LexerMediumPriorityTest, DigraphPercentGreater) {
  // 测试 %> -> }
  auto Tokens = lex("%>");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::percentgreater);
}

TEST_F(LexerMediumPriorityTest, DigraphLessColon) {
  // 测试 <: -> [
  auto Tokens = lex("<:");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::lesscolon);
}

TEST_F(LexerMediumPriorityTest, DigraphColonGreater) {
  // 测试 :> -> ]
  auto Tokens = lex(":>");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::colongreater);
}

TEST_F(LexerMediumPriorityTest, DigraphPercentColon) {
  // 测试 %: -> #
  auto Tokens = lex("%:");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::percentcolon);
}

TEST_F(LexerMediumPriorityTest, DigraphPercentColonPercentColon) {
  // 测试 %:%: -> ##
  auto Tokens = lex("%:%:");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::percentcolonpercentcolon);
}

TEST_F(LexerMediumPriorityTest, DigraphsInCode) {
  // 测试在代码中使用Digraphs
  auto Tokens = lex("<% int x; %>");
  ASSERT_GE(Tokens.size(), 5);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::lesspercent);  // <%
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::kw_int);
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::identifier);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::semicolon);
  EXPECT_EQ(Tokens[4].getKind(), TokenKind::percentgreater);  // %>
}

// ========== 测试原始字符串字面量 ==========

TEST_F(LexerMediumPriorityTest, RawStringLiteralSimple) {
  // 测试简单的原始字符串 R"(...)"
  auto Tokens = lex("R\"(hello world)\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, RawStringLiteralWithDelimiter) {
  // 测试带分隔符的原始字符串 R"delim(...)delim"
  Diags.reset();
  auto Tokens = lex("R\"test(content)test\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, RawStringLiteralWithNewlines) {
  // 测试包含换行的原始字符串
  Diags.reset();
  auto Tokens = lex("R\"(line1\nline2\nline3)\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, RawStringLiteralWithQuotes) {
  // 测试包含引号的原始字符串
  Diags.reset();
  auto Tokens = lex("R\"(he said \"hello\")\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, RawStringLiteralWithBackslashes) {
  // 测试包含反斜杠的原始字符串（不需要转义）
  Diags.reset();
  auto Tokens = lex("R\"(C:\\Users\\test)\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, RawStringLiteralUnterminated) {
  // 测试未终止的原始字符串
  Diags.reset();
  auto Tokens = lex("R\"(unterminated");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::unknown);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, RawStringLiteralInvalidDelimiter) {
  // 测试无效的分隔符（包含禁止字符）
  Diags.reset();
  auto Tokens = lex("R\"test)invalid(test)test\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_TRUE(Diags.hasErrorOccurred()); // 应该报错：分隔符包含 )
}

TEST_F(LexerMediumPriorityTest, RawStringLiteralDelimiterTooLong) {
  // 测试分隔符过长（超过16字符）
  Diags.reset();
  auto Tokens = lex("R\"12345678901234567(content)12345678901234567\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_TRUE(Diags.hasErrorOccurred()); // 应该报错：分隔符超过16字符
}

TEST_F(LexerMediumPriorityTest, RawStringLiteralEmpty) {
  // 测试空的原始字符串
  Diags.reset();
  auto Tokens = lex("R\"()\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

// ========== 测试UTF-8原始字符串 ==========

TEST_F(LexerMediumPriorityTest, UTF8RawStringLiteral) {
  // 测试UTF-8原始字符串 u8R"(...)"
  Diags.reset();
  auto Tokens = lex("u8R\"(hello)\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::utf8_string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, WideRawStringLiteral) {
  // 测试宽字符原始字符串 LR"(...)"
  Diags.reset();
  auto Tokens = lex("LR\"(hello)\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::wide_string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, UTF16RawStringLiteral) {
  // 测试UTF-16原始字符串 uR"(...)"
  Diags.reset();
  auto Tokens = lex("uR\"(hello)\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::utf16_string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerMediumPriorityTest, UTF32RawStringLiteral) {
  // 测试UTF-32原始字符串 UR"(...)"
  Diags.reset();
  auto Tokens = lex("UR\"(hello)\"");
  ASSERT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::utf32_string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}
