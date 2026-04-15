//===--- TokenTest.cpp - Token Unit Tests ----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Lex/Token.h"
#include "blocktype/Lex/TokenKinds.h"

using namespace blocktype;

namespace {

TEST(TokenTest, DefaultConstruction) {
  Token T;
  EXPECT_EQ(T.getKind(), TokenKind::unknown);
  EXPECT_EQ(T.getLength(), 0u);
  EXPECT_EQ(T.getLiteralData(), nullptr);
  EXPECT_EQ(T.getSourceLanguage(), Language::English);
}

TEST(TokenTest, SetKind) {
  Token T;
  T.setKind(TokenKind::kw_int);
  EXPECT_EQ(T.getKind(), TokenKind::kw_int);
  EXPECT_TRUE(T.is(TokenKind::kw_int));
  EXPECT_FALSE(T.isNot(TokenKind::kw_int));
}

TEST(TokenTest, SetLocation) {
  Token T;
  SourceLocation Loc(42);
  T.setLocation(Loc);
  EXPECT_EQ(T.getLocation().getRawEncoding(), 42u);
}

TEST(TokenTest, SetLength) {
  Token T;
  T.setLength(10);
  EXPECT_EQ(T.getLength(), 10u);
}

TEST(TokenTest, SetLiteralData) {
  Token T;
  const char *Data = "test";
  T.setLiteralData(Data);
  EXPECT_EQ(T.getLiteralData(), Data);
}

TEST(TokenTest, SetSourceLanguage) {
  Token T;
  T.setSourceLanguage(Language::Chinese);
  EXPECT_EQ(T.getSourceLanguage(), Language::Chinese);
}

TEST(TokenTest, IsKeyword) {
  Token T;
  T.setKind(TokenKind::kw_if);
  EXPECT_TRUE(T.isKeyword());
  EXPECT_FALSE(T.isLiteral());
}

TEST(TokenTest, IsLiteral) {
  Token T;
  T.setKind(TokenKind::numeric_constant);
  EXPECT_TRUE(T.isLiteral());
  EXPECT_FALSE(T.isKeyword());
}

TEST(TokenTest, IsStringLiteral) {
  Token T;
  T.setKind(TokenKind::string_literal);
  EXPECT_TRUE(T.isStringLiteral());
  EXPECT_TRUE(T.isLiteral());
}

TEST(TokenTest, IsCharLiteral) {
  Token T;
  T.setKind(TokenKind::char_constant);
  EXPECT_TRUE(T.isCharLiteral());
  EXPECT_TRUE(T.isLiteral());
}

TEST(TokenTest, IsNumericConstant) {
  Token T;
  T.setKind(TokenKind::numeric_constant);
  EXPECT_TRUE(T.isNumericConstant());
  EXPECT_TRUE(T.isLiteral());
}

TEST(TokenTest, IsPunctuation) {
  Token T;
  T.setKind(TokenKind::plus);
  EXPECT_TRUE(T.isPunctuation());
}

TEST(TokenTest, ChineseKeyword) {
  Token T;
  T.setKind(TokenKind::kw_if);
  T.setSourceLanguage(Language::Chinese);
  EXPECT_TRUE(T.isKeyword());
  EXPECT_TRUE(T.isChineseKeyword());
}

TEST(TokenTest, GetText) {
  Token T;
  const char *Data = "identifier";
  T.setLiteralData(Data);
  T.setLength(10);
  StringRef Text = T.getText();
  EXPECT_EQ(Text, "identifier");
}

TEST(TokenTest, Clear) {
  Token T;
  T.setKind(TokenKind::kw_int);
  T.setLength(3);
  T.setSourceLanguage(Language::Chinese);
  
  T.clear();
  
  EXPECT_EQ(T.getKind(), TokenKind::unknown);
  EXPECT_EQ(T.getLength(), 0u);
  EXPECT_EQ(T.getSourceLanguage(), Language::English);
}

} // anonymous namespace
