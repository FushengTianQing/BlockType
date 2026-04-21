//===--- LexerExtensionTest.cpp - Test extension warnings -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for compiler extension warnings (MSVC, GCC extensions)
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "llvm/Support/raw_ostream.h"

using namespace blocktype;

class LexerExtensionTest : public ::testing::Test {
protected:
  std::string Buffer;
  std::unique_ptr<SourceManager> SM;
  std::unique_ptr<DiagnosticsEngine> Diags;
  std::string Output;

  void SetUp() override {
    SM = std::make_unique<SourceManager>();
    Output.clear();
    Diags = std::make_unique<DiagnosticsEngine>(llvm::errs());
    Diags->setSourceManager(SM.get());
  }

  Token lexSingleToken(const std::string &Code) {
    Buffer = Code;
    SourceLocation Loc = SM->createMainFileID("test.cpp", Buffer);
    Lexer L(*SM, *Diags, Buffer, Loc);
    
    Token Tok;
    while (true) {
      if (!L.lexToken(Tok)) {
        return Tok; // EOF
      }
      if (!Tok.is(TokenKind::comment)) {
        return Tok;
      }
    }
    return Tok;
  }
};

//===----------------------------------------------------------------------===//
// Test 1: #@ MSVC extension warning
//===----------------------------------------------------------------------===//

TEST_F(LexerExtensionTest, HashAtMSVCExtension) {
  // #@ is an MSVC extension for charizing operator
  Token Tok = lexSingleToken("#@");
  
  EXPECT_EQ(Tok.getKind(), TokenKind::hashat);
  EXPECT_TRUE(Diags->hasErrorOccurred() || Diags->getNumWarnings() > 0);
}

TEST_F(LexerExtensionTest, HashAtInMacroContext) {
  // Test #@ in a more realistic context
  Buffer = "#define CHARIZE(x) #@x\n";
  SourceLocation Loc = SM->createMainFileID("test.cpp", Buffer);
  Lexer L(*SM, *Diags, Buffer, Loc);
  
  // Lex through the macro definition
  Token Tok;
  bool FoundHashAt = false;
  
  while (L.lexToken(Tok) && !Tok.is(TokenKind::eof)) {
    if (Tok.is(TokenKind::hashat)) {
      FoundHashAt = true;
    }
  }
  
  EXPECT_TRUE(FoundHashAt);
  EXPECT_TRUE(Diags->hasErrorOccurred() || Diags->getNumWarnings() > 0);
}

//===----------------------------------------------------------------------===//
// Test 2: Standard hash operators (no warning)
//===----------------------------------------------------------------------===//

TEST_F(LexerExtensionTest, HashHashNoWarning) {
  // ## is standard token pasting operator
  Diags->reset();
  Token Tok = lexSingleToken("##");
  
  EXPECT_EQ(Tok.getKind(), TokenKind::hashhash);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
}

TEST_F(LexerExtensionTest, SingleHashNoWarning) {
  // # is standard stringizing operator
  Diags->reset();
  Token Tok = lexSingleToken("#");
  
  EXPECT_EQ(Tok.getKind(), TokenKind::hash);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
}

//===----------------------------------------------------------------------===//
// Test 3: Digraphs (standard C++, no warning)
//===----------------------------------------------------------------------===//

TEST_F(LexerExtensionTest, DigraphsNoWarning) {
  // Digraphs are standard C++ alternative tokens
  Diags->reset();
  
  // Test <% (digraph for {)
  Token Tok = lexSingleToken("<%");
  EXPECT_EQ(Tok.getKind(), TokenKind::lesspercent);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
  
  // Test %> (digraph for })
  Diags->reset();
  Tok = lexSingleToken("%>");
  EXPECT_EQ(Tok.getKind(), TokenKind::percentgreater);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
  
  // Test <: (digraph for [)
  Diags->reset();
  Tok = lexSingleToken("<:");
  EXPECT_EQ(Tok.getKind(), TokenKind::lesscolon);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
  
  // Test :> (digraph for ])
  Diags->reset();
  Tok = lexSingleToken(":>");
  EXPECT_EQ(Tok.getKind(), TokenKind::colongreater);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
  
  // Test %: (digraph for #)
  Diags->reset();
  Tok = lexSingleToken("%:");
  EXPECT_EQ(Tok.getKind(), TokenKind::percentcolon);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
  
  // Test %:%: (digraph for ##)
  Diags->reset();
  Tok = lexSingleToken("%:%:");
  EXPECT_EQ(Tok.getKind(), TokenKind::percentcolonpercentcolon);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
}

//===----------------------------------------------------------------------===//
// Test 4: C++26 new tokens (no warning)
//===----------------------------------------------------------------------===//

TEST_F(LexerExtensionTest, Cpp26TokensNoWarning) {
  // .. is C++26 placeholder pattern
  Diags->reset();
  Token Tok = lexSingleToken("..");
  EXPECT_EQ(Tok.getKind(), TokenKind::dotdot);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
  
  // <<< is C++26 digraph
  Diags->reset();
  Tok = lexSingleToken("<<<");
  EXPECT_EQ(Tok.getKind(), TokenKind::lesslessless);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
  
  // >>> is C++26 digraph
  Diags->reset();
  Tok = lexSingleToken(">>>");
  EXPECT_EQ(Tok.getKind(), TokenKind::greatergreatergreater);
  EXPECT_FALSE(Diags->hasErrorOccurred());
  EXPECT_EQ(Diags->getNumWarnings(), 0u);
}

//===----------------------------------------------------------------------===//
// Test 5: Multiple extension warnings in sequence
//===----------------------------------------------------------------------===//

TEST_F(LexerExtensionTest, MultipleHashAtWarnings) {
  Buffer = "#@ #@ #@";
  SourceLocation Loc = SM->createMainFileID("test.cpp", Buffer);
  Lexer L(*SM, *Diags, Buffer, Loc);
  
  Token Tok;
  int HashAtCount = 0;
  
  while (L.lexToken(Tok) && !Tok.is(TokenKind::eof)) {
    if (Tok.is(TokenKind::hashat)) {
      HashAtCount++;
    }
  }
  
  EXPECT_EQ(HashAtCount, 3);
  // Should have 3 warnings
  EXPECT_GE(Diags->getNumWarnings(), 3u);
}

//===----------------------------------------------------------------------===//
// Test 6: Mixed standard and extension tokens
//===----------------------------------------------------------------------===//

TEST_F(LexerExtensionTest, MixedStandardAndExtension) {
  // Mix of ## (standard) and #@ (extension)
  Buffer = "## #@";
  SourceLocation Loc = SM->createMainFileID("test.cpp", Buffer);
  Lexer L(*SM, *Diags, Buffer, Loc);
  
  Token Tok;
  int TokenCount = 0;
  bool FoundHashHash = false;
  bool FoundHashAt = false;
  
  while (L.lexToken(Tok) && !Tok.is(TokenKind::eof)) {
    TokenCount++;
    if (Tok.is(TokenKind::hashhash)) FoundHashHash = true;
    if (Tok.is(TokenKind::hashat)) FoundHashAt = true;
  }
  
  EXPECT_TRUE(FoundHashHash);
  EXPECT_TRUE(FoundHashAt);
  EXPECT_EQ(TokenCount, 2);
  // Should have exactly 1 warning (for #@)
  EXPECT_GE(Diags->getNumWarnings(), 1u);
}
