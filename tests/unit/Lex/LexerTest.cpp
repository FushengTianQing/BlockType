//===--- LexerTest.cpp - Lexer Unit Tests ----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class LexerTest : public ::testing::Test {
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

TEST_F(LexerTest, EmptyInput) {
  auto Tokens = lex("");
  ASSERT_EQ(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::eof);
}

TEST_F(LexerTest, Whitespace) {
  auto Tokens = lex("   \t\n  ");
  ASSERT_EQ(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::eof);
}

TEST_F(LexerTest, Identifier) {
  auto Tokens = lex("foo");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::identifier);
  EXPECT_EQ(Tokens[0].getText(), "foo");
}

TEST_F(LexerTest, MultipleIdentifiers) {
  auto Tokens = lex("foo bar baz");
  ASSERT_EQ(Tokens.size(), 4u);
  EXPECT_EQ(Tokens[0].getText(), "foo");
  EXPECT_EQ(Tokens[1].getText(), "bar");
  EXPECT_EQ(Tokens[2].getText(), "baz");
}

TEST_F(LexerTest, EnglishKeyword) {
  auto Tokens = lex("int");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_int);
  EXPECT_EQ(Tokens[0].getSourceLanguage(), Language::English);
}

TEST_F(LexerTest, EnglishKeywords) {
  auto Tokens = lex("if else while for");
  ASSERT_EQ(Tokens.size(), 5u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_if);
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::kw_else);
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::kw_while);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::kw_for);
}

TEST_F(LexerTest, ChineseKeyword) {
  auto Tokens = lex("如果");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_if);
  EXPECT_EQ(Tokens[0].getSourceLanguage(), Language::Chinese);
}

TEST_F(LexerTest, ChineseKeywords) {
  auto Tokens = lex("如果 否则 当 循环");
  ASSERT_EQ(Tokens.size(), 5u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_if);
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::kw_else);
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::kw_while);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::kw_for);
}

TEST_F(LexerTest, NumericConstant) {
  auto Tokens = lex("123");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tokens[0].getText(), "123");
}

TEST_F(LexerTest, HexConstant) {
  auto Tokens = lex("0x1a2b");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tokens[0].getText(), "0x1a2b");
}

TEST_F(LexerTest, BinaryConstant) {
  auto Tokens = lex("0b1010");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tokens[0].getText(), "0b1010");
}

TEST_F(LexerTest, FloatConstant) {
  auto Tokens = lex("3.14");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tokens[0].getText(), "3.14");
}

TEST_F(LexerTest, ScientificNotation) {
  auto Tokens = lex("1.5e10");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tokens[0].getText(), "1.5e10");
}

TEST_F(LexerTest, CharConstant) {
  auto Tokens = lex("'a'");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::char_constant);
  EXPECT_EQ(Tokens[0].getText(), "'a'");
}

TEST_F(LexerTest, StringLiteral) {
  auto Tokens = lex("\"hello\"");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::string_literal);
  EXPECT_EQ(Tokens[0].getText(), "\"hello\"");
}

TEST_F(LexerTest, WideStringLiteral) {
  auto Tokens = lex("L\"wide\"");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::wide_string_literal);
}

TEST_F(LexerTest, UTF8StringLiteral) {
  auto Tokens = lex("u8\"utf8\"");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::utf8_string_literal);
}

TEST_F(LexerTest, Operators) {
  auto Tokens = lex("+ - * / %");
  ASSERT_EQ(Tokens.size(), 6u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::plus);
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::minus);
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::star);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::slash);
  EXPECT_EQ(Tokens[4].getKind(), TokenKind::percent);
}

TEST_F(LexerTest, ComparisonOperators) {
  auto Tokens = lex("== != < > <= >=");
  ASSERT_EQ(Tokens.size(), 7u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::equalequal);
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::exclaimequal);
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::less);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::greater);
  EXPECT_EQ(Tokens[4].getKind(), TokenKind::lessequal);
  EXPECT_EQ(Tokens[5].getKind(), TokenKind::greaterequal);
}

TEST_F(LexerTest, SpaceshipOperator) {
  auto Tokens = lex("<=>");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::spaceship);
}

TEST_F(LexerTest, IncrementDecrement) {
  auto Tokens = lex("++ --");
  ASSERT_EQ(Tokens.size(), 3u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::plusplus);
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::minusminus);
}

TEST_F(LexerTest, AssignmentOperators) {
  auto Tokens = lex("= += -= *= /=");
  ASSERT_EQ(Tokens.size(), 6u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::equal);
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::plusequal);
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::minusequal);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::starequal);
  EXPECT_EQ(Tokens[4].getKind(), TokenKind::slashequal);
}

TEST_F(LexerTest, Punctuation) {
  auto Tokens = lex("( ) { } [ ] ; ,");
  ASSERT_EQ(Tokens.size(), 9u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::l_paren);
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::r_paren);
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::l_brace);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::r_brace);
  EXPECT_EQ(Tokens[4].getKind(), TokenKind::l_square);
  EXPECT_EQ(Tokens[5].getKind(), TokenKind::r_square);
  EXPECT_EQ(Tokens[6].getKind(), TokenKind::semicolon);
  EXPECT_EQ(Tokens[7].getKind(), TokenKind::comma);
}

TEST_F(LexerTest, ScopeOperator) {
  auto Tokens = lex("::");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::coloncolon);
}

TEST_F(LexerTest, Ellipsis) {
  auto Tokens = lex("...");
  ASSERT_EQ(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::ellipsis);
}

TEST_F(LexerTest, LineComment) {
  auto Tokens = lex("int x; // comment\nint y;");
  ASSERT_EQ(Tokens.size(), 7u);  // int, x, ;, int, y, ;, eof
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_int);
  EXPECT_EQ(Tokens[1].getText(), "x");
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::semicolon);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::kw_int);
  EXPECT_EQ(Tokens[4].getText(), "y");
}

TEST_F(LexerTest, BlockComment) {
  auto Tokens = lex("int x; /* comment */ int y;");
  ASSERT_EQ(Tokens.size(), 7u);  // int, x, ;, int, y, ;, eof
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_int);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::kw_int);
}

TEST_F(LexerTest, MixedEnglishChinese) {
  auto Tokens = lex("int 变量 = 10;");
  ASSERT_EQ(Tokens.size(), 6u);  // int, 变量, =, 10, ;, eof
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_int);
  EXPECT_EQ(Tokens[1].getKind(), TokenKind::identifier);
  EXPECT_EQ(Tokens[1].getText(), "变量");
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::equal);
}

TEST_F(LexerTest, ChineseCode) {
  auto Tokens = lex("整数 主函数() { 返回 0; }");
  ASSERT_GE(Tokens.size(), 10u);  // 整数, 主函数, (, ), {, 返回, 0, ;, }, eof
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_int);
  EXPECT_EQ(Tokens[1].getText(), "主函数");
  EXPECT_EQ(Tokens[5].getKind(), TokenKind::kw_return);
}

TEST_F(LexerTest, SimpleFunction) {
  auto Tokens = lex("int main() { return 0; }");
  ASSERT_GE(Tokens.size(), 10u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_int);
  EXPECT_EQ(Tokens[1].getText(), "main");
  EXPECT_EQ(Tokens[2].getKind(), TokenKind::l_paren);
  EXPECT_EQ(Tokens[3].getKind(), TokenKind::r_paren);
  EXPECT_EQ(Tokens[4].getKind(), TokenKind::l_brace);
  EXPECT_EQ(Tokens[5].getKind(), TokenKind::kw_return);
}

} // anonymous namespace
