#include <gtest/gtest.h>
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/FileManager.h"
#include "blocktype/Lex/HeaderSearch.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace blocktype;

class MediumPriorityFixesTest : public ::testing::Test {
protected:
  SourceManager SM;
  std::unique_ptr<DiagnosticsEngine> Diags;
  std::unique_ptr<FileManager> FileMgr;
  std::unique_ptr<HeaderSearch> Headers;
  std::unique_ptr<Preprocessor> PP;
  std::string OutputStr;
  std::unique_ptr<llvm::raw_string_ostream> OutputStream;

  void SetUp() override {
    OutputStream = std::make_unique<llvm::raw_string_ostream>(OutputStr);
    Diags = std::make_unique<DiagnosticsEngine>(*OutputStream);
    FileMgr = std::make_unique<FileManager>();
    Headers = std::make_unique<HeaderSearch>(*FileMgr);
    PP = std::make_unique<Preprocessor>(SM, *Diags, Headers.get(), nullptr, FileMgr.get());
  }
};

// Test 1: Digraphs support - <% and %>
TEST_F(MediumPriorityFixesTest, DigraphsBraces) {
  PP->enterSourceFile("test.cpp", "<% int x; %>\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::lesspercent);  // <%
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::kw_int);
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::semicolon);
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::percentgreater);  // %>
}

// Test 2: Digraphs support - <: and :>
TEST_F(MediumPriorityFixesTest, DigraphsBrackets) {
  PP->enterSourceFile("test.cpp", "int arr<:5:>;\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::kw_int);
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::lesscolon);  // <:
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::numeric_constant);
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::colongreater);  // :>
}

// Test 3: Digraphs support - %: (hash)
TEST_F(MediumPriorityFixesTest, DigraphHash) {
  PP->enterSourceFile("test.cpp", "%:define FOO 1\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::percentcolon);  // %:
}

// Test 4: Digraphs support - %:%: (hashhash)
TEST_F(MediumPriorityFixesTest, DigraphHashHash) {
  Lexer Lex(SM, *Diags, "%:%:\n", SM.createMainFileID("test.cpp", "%:%:\n"));
  
  Token Tok;
  ASSERT_TRUE(Lex.lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::percentcolonpercentcolon);  // %:%:
}

// Test 5: C++26 Digraphs - <<< and >>>
TEST_F(MediumPriorityFixesTest, Cpp26Digraphs) {
  PP->enterSourceFile("test.cpp", "<<< >>>\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::lesslessless);  // <<<
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::greatergreatergreater);  // >>>
}

// Test 6: Hexadecimal floating-point
TEST_F(MediumPriorityFixesTest, HexFloat) {
  PP->enterSourceFile("test.cpp", "0x1.8p10\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tok.getText(), "0x1.8p10");
}

// Test 7: Hexadecimal floating-point with exponent
TEST_F(MediumPriorityFixesTest, HexFloatNegativeExponent) {
  PP->enterSourceFile("test.cpp", "0x1.fp-4\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tok.getText(), "0x1.fp-4");
}

// Test 8: __VA_ARGS__ basic support
TEST_F(MediumPriorityFixesTest, VaArgs) {
  // Define a variadic macro
  PP->enterSourceFile("test.cpp", "#define PRINT(fmt, ...) printf(fmt, __VA_ARGS__)\nPRINT(\"hello %d %d\", 1, 2)\n");
  
  Token Tok;
  // After macro expansion, should get: printf, (, "hello %d %d", ,, 1, ,, 2, )
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "printf");
}

// Test 9: __VA_OPT__ with arguments
TEST_F(MediumPriorityFixesTest, VaOptWithArgs) {
  // Define a variadic macro with __VA_OPT__
  PP->enterSourceFile("test.cpp", "#define LOG(fmt, ...) printf(fmt __VA_OPT__(, __VA_ARGS__))\nLOG(\"test\")\nLOG(\"test %d\", 42)\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "printf");
}

// Test 10: __VA_OPT__ without arguments
TEST_F(MediumPriorityFixesTest, VaOptWithoutArgs) {
  // Define a variadic macro with __VA_OPT__
  PP->enterSourceFile("test.cpp", "#define LOG(fmt, ...) printf(fmt __VA_OPT__(, __VA_ARGS__))\nLOG(\"test\")\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "printf");
}

// Test 11: Token buffer - multiple peek
TEST_F(MediumPriorityFixesTest, TokenBufferPeek) {
  PP->enterSourceFile("test.cpp", "int x y z\n");
  
  // Peek at first token
  Token Tok1 = PP->peekToken(0);
  EXPECT_EQ(Tok1.getKind(), TokenKind::kw_int);
  
  // Peek at second token
  Token Tok2 = PP->peekToken(1);
  EXPECT_EQ(Tok2.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok2.getText(), "x");
  
  // Peek at third token
  Token Tok3 = PP->peekToken(2);
  EXPECT_EQ(Tok3.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok3.getText(), "y");
}

// Test 12: Token buffer - peek and consume
TEST_F(MediumPriorityFixesTest, TokenBufferPeekAndConsume) {
  PP->enterSourceFile("test.cpp", "int x y\n");
  
  // Peek first
  Token Tok1 = PP->peekToken(0);
  EXPECT_EQ(Tok1.getKind(), TokenKind::kw_int);
  
  // Consume should return the same token
  Token Tok2;
  ASSERT_TRUE(PP->consumeToken(Tok2));
  EXPECT_EQ(Tok2.getKind(), TokenKind::kw_int);
  
  // Next peek should be 'x'
  Token Tok3 = PP->peekToken(0);
  EXPECT_EQ(Tok3.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok3.getText(), "x");
}

// Test 13: Complex hexadecimal floating-point
TEST_F(MediumPriorityFixesTest, ComplexHexFloat) {
  PP->enterSourceFile("test.cpp", "0xABC.DEFp+123\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tok.getText(), "0xABC.DEFp+123");
}

// Test 14: Hex integer (not floating-point)
TEST_F(MediumPriorityFixesTest, HexInteger) {
  PP->enterSourceFile("test.cpp", "0xDEADBEEF\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::numeric_constant);
  EXPECT_EQ(Tok.getText(), "0xDEADBEEF");
}

// Test 15: Variadic macro with multiple arguments
TEST_F(MediumPriorityFixesTest, VariadicMacroMultipleArgs) {
  // Define a variadic macro
  PP->enterSourceFile("test.cpp", "#define SUM(x, ...) sum(x, __VA_ARGS__)\nSUM(1, 2, 3)\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "sum");
}

// Test 16: UAX #31 - CJK identifier
TEST_F(MediumPriorityFixesTest, UAX31CJKIdentifier) {
  PP->enterSourceFile("test.cpp", "变量名 整数值\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "变量名");
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "整数值");  // Not a keyword
}

// Test 17: UAX #31 - Mixed ASCII and Unicode identifier
TEST_F(MediumPriorityFixesTest, UAX31MixedIdentifier) {
  PP->enterSourceFile("test.cpp", "变量_1 变量2\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "变量_1");
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "变量2");
}

// Test 18: UAX #31 - Greek letters
TEST_F(MediumPriorityFixesTest, UAX31GreekIdentifier) {
  PP->enterSourceFile("test.cpp", "α β γ\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "α");
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "β");
}

// Test 19: UAX #31 - Cyrillic letters
TEST_F(MediumPriorityFixesTest, UAX31CyrillicIdentifier) {
  PP->enterSourceFile("test.cpp", "привет мир\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "привет");
  
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "мир");
}

// Test 20: Error recovery - skip to synchronization point
TEST_F(MediumPriorityFixesTest, ErrorRecovery) {
  Lexer Lex(SM, *Diags, "int x @#$% y;\n", SM.createMainFileID("test.cpp", "int x @#$% y;\n"));
  
  Token Tok;
  ASSERT_TRUE(Lex.lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::kw_int);
  
  ASSERT_TRUE(Lex.lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::identifier);
  EXPECT_EQ(Tok.getText(), "x");
  
  // The lexer should handle the invalid characters
  // and continue to the next valid token
  while (Lex.lexToken(Tok)) {
    if (Tok.is(TokenKind::identifier) && Tok.getText() == "y") {
      break;
    }
  }
  EXPECT_EQ(Tok.getText(), "y");
}

// Test 21: Unicode escape sequence \uXXXX in string
TEST_F(MediumPriorityFixesTest, UnicodeEscapeInString) {
  PP->enterSourceFile("test.cpp", "\"Hello \\u0041\\u0042\"\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::string_literal);
  EXPECT_EQ(Tok.getText(), "\"Hello \\u0041\\u0042\"");
}

// Test 22: Unicode escape sequence \UXXXXXXXX in string
TEST_F(MediumPriorityFixesTest, UnicodeEscape8DigitInString) {
  PP->enterSourceFile("test.cpp", "\"Test \\U00000041\\U00000042\"\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::string_literal);
  EXPECT_EQ(Tok.getText(), "\"Test \\U00000041\\U00000042\"");
}

// Test 23: Unicode escape sequence in character constant
TEST_F(MediumPriorityFixesTest, UnicodeEscapeInChar) {
  PP->enterSourceFile("test.cpp", "'\\u0041'\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::char_constant);
  EXPECT_EQ(Tok.getText(), "'\\u0041'");
}

// Test 24: UTF-8 character literal u8'c'
TEST_F(MediumPriorityFixesTest, UTF8CharLiteral) {
  PP->enterSourceFile("test.cpp", "u8'x'\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::utf8_char_constant);
  EXPECT_EQ(Tok.getText(), "u8'x'");
}

// Test 25: UTF-8 character literal with Unicode escape
TEST_F(MediumPriorityFixesTest, UTF8CharLiteralWithUnicodeEscape) {
  PP->enterSourceFile("test.cpp", "u8'\\u0041'\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::utf8_char_constant);
  EXPECT_EQ(Tok.getText(), "u8'\\u0041'");
}

// Test 26: UTF-16 character literal u'c'
TEST_F(MediumPriorityFixesTest, UTF16CharLiteral) {
  PP->enterSourceFile("test.cpp", "u'x'\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::utf16_char_constant);
  EXPECT_EQ(Tok.getText(), "u'x'");
}

// Test 27: UTF-32 character literal U'c'
TEST_F(MediumPriorityFixesTest, UTF32CharLiteral) {
  PP->enterSourceFile("test.cpp", "U'x'\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::utf32_char_constant);
  EXPECT_EQ(Tok.getText(), "U'x'");
}

// Test 28: Wide character literal L'c'
TEST_F(MediumPriorityFixesTest, WideCharLiteral) {
  PP->enterSourceFile("test.cpp", "L'x'\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::wide_char_constant);
  EXPECT_EQ(Tok.getText(), "L'x'");
}

// Test 29: UTF-8 string literal u8"..."
TEST_F(MediumPriorityFixesTest, UTF8StringLiteral) {
  PP->enterSourceFile("test.cpp", "u8\"hello\"\n");
  
  Token Tok;
  ASSERT_TRUE(PP->lexToken(Tok));
  EXPECT_EQ(Tok.getKind(), TokenKind::utf8_string_literal);
  EXPECT_EQ(Tok.getText(), "u8\"hello\"");
}

// Test 30: Keyword range markers - isKeyword should work
TEST_F(MediumPriorityFixesTest, KeywordRangeCheck) {
  // Test that isKeyword correctly identifies keywords
  Lexer Lex(SM, *Diags, "int auto return", SM.createMainFileID("test.cpp", "int auto return"));
  
  Token Tok;
  ASSERT_TRUE(Lex.lexToken(Tok));
  EXPECT_TRUE(isKeyword(Tok.getKind())); // int
  
  ASSERT_TRUE(Lex.lexToken(Tok));
  EXPECT_TRUE(isKeyword(Tok.getKind())); // auto
  
  ASSERT_TRUE(Lex.lexToken(Tok));
  EXPECT_TRUE(isKeyword(Tok.getKind())); // return
}
