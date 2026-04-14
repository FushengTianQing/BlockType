//===--- PreprocessorTest.cpp - Preprocessor Unit Tests --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class PreprocessorTest : public ::testing::Test {
protected:
  SourceManager SM;
  DiagnosticsEngine Diags;

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

TEST_F(PreprocessorTest, EmptyInput) {
  auto Tokens = preprocess("");
  ASSERT_EQ(Tokens.size(), 1u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::eof);
}

TEST_F(PreprocessorTest, SimpleCode) {
  auto Tokens = preprocess("int x;");
  ASSERT_EQ(Tokens.size(), 4u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::kw_int);
}

TEST_F(PreprocessorTest, DefineObjectMacro) {
  auto Tokens = preprocess("#define PI 3.14\nPI");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
}

TEST_F(PreprocessorTest, DefineFunctionMacro) {
  auto Tokens = preprocess("#define MAX(a,b) a\nMAX(1,2)");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
}

TEST_F(PreprocessorTest, IfdefDefined) {
  auto Tokens = preprocess("#define X\n#ifdef X\n1\n#endif");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
}

TEST_F(PreprocessorTest, IfdefNotDefined) {
  auto Tokens = preprocess("#ifdef X\n1\n#endif\n2");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getText(), "2");
}

TEST_F(PreprocessorTest, Ifndef) {
  auto Tokens = preprocess("#ifndef X\n1\n#endif");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
}

TEST_F(PreprocessorTest, IfTrue) {
  auto Tokens = preprocess("#if 1\n2\n#endif");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getText(), "2");
}

TEST_F(PreprocessorTest, IfFalse) {
  auto Tokens = preprocess("#if 0\n2\n#endif\n3");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getText(), "3");
}

TEST_F(PreprocessorTest, Else) {
  auto Tokens = preprocess("#if 0\n1\n#else\n2\n#endif");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getText(), "2");
}

TEST_F(PreprocessorTest, PredefinedMacros) {
  Preprocessor PP(SM, Diags);
  EXPECT_TRUE(PP.isMacroDefined("__cplusplus"));
  EXPECT_TRUE(PP.isMacroDefined("__BLOCKTYPE__"));
}

TEST_F(PreprocessorTest, ChineseDirective) {
  auto Tokens = preprocess("#定义 X 1\nX");
  ASSERT_GE(Tokens.size(), 2u);
  EXPECT_EQ(Tokens[0].getKind(), TokenKind::numeric_constant);
}

} // anonymous namespace
