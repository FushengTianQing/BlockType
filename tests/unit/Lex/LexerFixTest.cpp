// 测试Lexer修复的测试用例
// 测试数字字面量错误恢复和用户定义字面量验证

#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include <gtest/gtest.h>

using namespace blocktype;

class LexerFixTest : public ::testing::Test {
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

// ========== 测试数字字面量错误恢复 ==========

TEST_F(LexerFixTest, HexLiteralWithoutDigits) {
  // 测试 0x 后没有十六进制数字
  auto Tokens = lex("0x");
  EXPECT_EQ(Tokens.size(), 2); // numeric_constant + eof
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_TRUE(Diags.hasErrorOccurred()); // 应该有错误报告
}

TEST_F(LexerFixTest, HexLiteralWithDigits) {
  // 测试正常的十六进制字面量
  Diags.reset(); // 清除之前的错误
  auto Tokens = lex("0x1AB");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, BinaryLiteralWithoutDigits) {
  // 测试 0b 后没有二进制数字
  Diags.reset();
  auto Tokens = lex("0b");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, BinaryLiteralWithDigits) {
  // 测试正常的二进制字面量
  Diags.reset();
  auto Tokens = lex("0b1010");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, HexFloatWithoutExponentDigits) {
  // 测试十六进制浮点数 p 后没有指数数字
  Diags.reset();
  auto Tokens = lex("0x1.0p");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_TRUE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, HexFloatWithExponentDigits) {
  // 测试正常的十六进制浮点数
  Diags.reset();
  auto Tokens = lex("0x1.0p2");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

// ========== 测试用户定义字面量验证 ==========

TEST_F(LexerFixTest, UserDefinedLiteralValid) {
  // 测试有效的用户定义字面量
  Diags.reset();
  auto Tokens = lex("123_i");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::user_defined_integer_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, UserDefinedLiteralOnlyUnderscore) {
  // 测试只有下划线的后缀（无效）
  Diags.reset();
  auto Tokens = lex("123_");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::user_defined_integer_literal);
  EXPECT_TRUE(Diags.hasErrorOccurred()); // 应该报错：后缀太短
}

TEST_F(LexerFixTest, UserDefinedLiteralStartsWithDigit) {
  // 测试后缀以数字开头（无效）
  Diags.reset();
  auto Tokens = lex("123_456");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::user_defined_integer_literal);
  EXPECT_TRUE(Diags.hasErrorOccurred()); // 应该报错：后缀不能以数字开头
}

TEST_F(LexerFixTest, UserDefinedFloatLiteral) {
  // 测试浮点数的用户定义字面量
  Diags.reset();
  auto Tokens = lex("3.14_m");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::user_defined_floating_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, UserDefinedStringLiteral) {
  // 测试字符串的用户定义字面量
  Diags.reset();
  auto Tokens = lex("\"hello\"_s");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::user_defined_string_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, UserDefinedCharLiteral) {
  // 测试字符的用户定义字面量
  Diags.reset();
  auto Tokens = lex("'c'_ch");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::user_defined_char_literal);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, UserDefinedCharLiteralInvalid) {
  // 测试无效的字符用户定义字面量
  Diags.reset();
  auto Tokens = lex("'c'_");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::user_defined_char_literal);
  EXPECT_TRUE(Diags.hasErrorOccurred()); // 应该报错：后缀太短
}

// ========== 测试边界情况 ==========

TEST_F(LexerFixTest, HexLiteralWithDigitSeparator) {
  // 测试带数字分隔符的十六进制字面量
  Diags.reset();
  auto Tokens = lex("0x1'A'B'C");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, BinaryLiteralWithDigitSeparator) {
  // 测试带数字分隔符的二进制字面量
  Diags.reset();
  auto Tokens = lex("0b1010'1100");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}

TEST_F(LexerFixTest, StandardSuffixes) {
  // 测试标准后缀（不应该被识别为用户定义字面量）
  Diags.reset();
  auto Tokens = lex("123ULL");
  EXPECT_EQ(Tokens.size(), 2);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_FALSE(Diags.hasErrorOccurred());
  
  Diags.reset();
  auto Tokens2 = lex("3.14f");
  EXPECT_EQ(Tokens2.size(), 2);
  EXPECT_EQ(Tokens2[0].getKind(), TokenKind::numeric_constant);
  EXPECT_FALSE(Diags.hasErrorOccurred());
}
