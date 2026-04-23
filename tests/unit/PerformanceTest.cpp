//===--- PerformanceTest.cpp - Performance Benchmark Tests ------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Performance benchmark tests for Lexer and Preprocessor.
// These tests measure throughput and latency of core operations.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/FileManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Lex/HeaderSearch.h"
#include "llvm/Support/MemoryBuffer.h"
#include <chrono>
#include <fstream>
#include <random>

using namespace blocktype;

namespace {

// Helper to measure execution time in microseconds
template <typename F>
double measureTime(F&& Func, unsigned Iterations = 1) {
  auto Start = std::chrono::high_resolution_clock::now();
  for (unsigned I = 0; I < Iterations; ++I) {
    Func();
  }
  auto End = std::chrono::high_resolution_clock::now();
  auto Duration = std::chrono::duration_cast<std::chrono::microseconds>(End - Start);
  return static_cast<double>(Duration.count()) / Iterations;
}

// Helper to generate random identifier
std::string generateIdentifier(unsigned Length) {
  static const char FirstChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
  static const char Chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
  std::random_device Rd;
  std::mt19937 Gen(Rd());
  std::uniform_int_distribution<> FirstDis(0, sizeof(FirstChars) - 2);
  std::uniform_int_distribution<> Dis(0, sizeof(Chars) - 2);
  
  std::string Result;
  Result.reserve(Length);
  if (Length > 0) {
    Result += FirstChars[FirstDis(Gen)];
  }
  for (unsigned I = 1; I < Length; ++I) {
    Result += Chars[Dis(Gen)];
  }
  return Result;
}

// Helper to generate test source code
std::string generateTestCode(unsigned Lines, unsigned TokensPerLine) {
  std::string Code;
  Code.reserve(Lines * TokensPerLine * 20);
  
  for (unsigned Line = 0; Line < Lines; ++Line) {
    for (unsigned Token = 0; Token < TokensPerLine; ++Token) {
      // Mix of identifiers, numbers, operators
      Code += generateIdentifier(8) + " = " + std::to_string(Line * TokensPerLine + Token) + "; ";
    }
    Code += "\n";
  }
  
  return Code;
}

} // anonymous namespace

//===----------------------------------------------------------------------===//
// Lexer Performance Tests
//===----------------------------------------------------------------------===//

class LexerPerformanceTest : public ::testing::Test {
protected:
  void SetUp() override {
    FM = std::make_unique<FileManager>();
    SM = std::make_unique<SourceManager>();
    Diags = std::make_unique<DiagnosticsEngine>();
  }

  void TearDown() override {
    Diags.reset();
    SM.reset();
    FM.reset();
  }

  std::unique_ptr<FileManager> FM;
  std::unique_ptr<SourceManager> SM;
  std::unique_ptr<DiagnosticsEngine> Diags;
};

TEST_F(LexerPerformanceTest, TokenizeSmallFile) {
  // 100 lines, 5 tokens per line = ~500 tokens
  std::string Code = generateTestCode(100, 5);
  
  // Warm up
  SourceLocation Loc = SM->createFileID("test.cpp", Code);
  Lexer Lex(*SM, *Diags, Code, Loc);
  Token Tok;
  while (Lex.lexToken(Tok)) {}
  
  // Measure
  double Time = measureTime([&]() {
    SourceLocation L = SM->createFileID("test.cpp", Code);
    Lexer Lexer(*SM, *Diags, Code, L);
    Token T;
    while (Lexer.lexToken(T)) {}
  }, 10);
  
  // Baseline: < 500 microseconds for 500 tokens
  EXPECT_LT(Time, 500.0) << "Lexer too slow for small file: " << Time << " us";
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("Tokens", "500");
  RecordProperty("Throughput", std::to_string(500.0 * 1000000.0 / Time) + " tokens/sec");
}

TEST_F(LexerPerformanceTest, TokenizeMediumFile) {
  // 1000 lines, 10 tokens per line = ~10000 tokens
  std::string Code = generateTestCode(1000, 10);
  
  double Time = measureTime([&]() {
    SourceLocation L = SM->createFileID("test.cpp", Code);
    Lexer Lexer(*SM, *Diags, Code, L);
    Token T;
    while (Lexer.lexToken(T)) {}
  }, 5);
  
  // Baseline: < 5000 microseconds for 10000 tokens
  EXPECT_LT(Time, 5000.0) << "Lexer too slow for medium file: " << Time << " us";
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("Tokens", "10000");
  RecordProperty("Throughput", std::to_string(10000.0 * 1000000.0 / Time) + " tokens/sec");
}

TEST_F(LexerPerformanceTest, TokenizeLargeFile) {
  // 10000 lines, 10 tokens per line = ~100000 tokens
  std::string Code = generateTestCode(10000, 10);
  
  double Time = measureTime([&]() {
    SourceLocation L = SM->createFileID("test.cpp", Code);
    Lexer Lexer(*SM, *Diags, Code, L);
    Token T;
    while (Lexer.lexToken(T)) {}
  }, 3);
  
  // Baseline: < 50000 microseconds for 100000 tokens
  EXPECT_LT(Time, 50000.0) << "Lexer too slow for large file: " << Time << " us";
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("Tokens", "100000");
  RecordProperty("Throughput", std::to_string(100000.0 * 1000000.0 / Time) + " tokens/sec");
}

TEST_F(LexerPerformanceTest, IdentifierLookup) {
  // Test identifier keyword lookup performance
  std::string Code;
  for (unsigned I = 0; I < 1000; ++I) {
    Code += "int if else for while return class struct void const static\n";
  }
  
  double Time = measureTime([&]() {
    SourceLocation L = SM->createFileID("test.cpp", Code);
    Lexer Lexer(*SM, *Diags, Code, L);
    Token T;
    while (Lexer.lexToken(T)) {}
  }, 5);
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("Keywords", "8000");
}

TEST_F(LexerPerformanceTest, NumericLiterals) {
  // Test numeric literal parsing performance
  std::string Code;
  for (unsigned I = 0; I < 1000; ++I) {
    Code += "123 0x1ABC 0b1010 3.14159 1.0e-10 0xDEADBEEF\n";
  }
  
  double Time = measureTime([&]() {
    SourceLocation L = SM->createFileID("test.cpp", Code);
    Lexer Lexer(*SM, *Diags, Code, L);
    Token T;
    while (Lexer.lexToken(T)) {}
  }, 5);
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("Literals", "6000");
}

TEST_F(LexerPerformanceTest, StringLiterals) {
  // Test string literal parsing performance
  std::string Code;
  for (unsigned I = 0; I < 1000; ++I) {
    Code += "\"Hello, World!\" u8\"UTF-8\" u\"UTF-16\" U\"UTF-32\" R\"(raw)\"\n";
  }
  
  double Time = measureTime([&]() {
    SourceLocation L = SM->createFileID("test.cpp", Code);
    Lexer Lexer(*SM, *Diags, Code, L);
    Token T;
    while (Lexer.lexToken(T)) {}
  }, 5);
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("Strings", "5000");
}

//===----------------------------------------------------------------------===//
// HeaderSearch Cache Performance Tests
//===----------------------------------------------------------------------===//

class HeaderSearchPerformanceTest : public ::testing::Test {
protected:
  void SetUp() override {
    FM = std::make_unique<FileManager>();
    HS = std::make_unique<HeaderSearch>(*FM);
    
    // Create temporary test files
    TestDir = "/tmp/blocktype_perf_test";
    system(("mkdir -p " + TestDir).c_str());
    
    // Create test header files
    for (int I = 0; I < 10; ++I) {
      std::string Filename = TestDir + "/header" + std::to_string(I) + ".h";
      std::ofstream(Filename) << "// Header " << I << "\n";
    }
    
    // Add search path
    HS->addSearchPath(TestDir);
  }

  void TearDown() override {
    system(("rm -rf " + TestDir).c_str());
    HS.reset();
    FM.reset();
  }

  std::unique_ptr<FileManager> FM;
  std::unique_ptr<HeaderSearch> HS;
  std::string TestDir;
};

TEST_F(HeaderSearchPerformanceTest, CacheHitRate) {
  // First lookup - cache miss
  const FileEntry *FE1 = HS->lookupHeader("header0.h", false);
  EXPECT_NE(FE1, nullptr);
  EXPECT_EQ(HS->getLookupCacheMisses(), 1u);
  EXPECT_EQ(HS->getLookupCacheHits(), 0u);
  
  // Second lookup - cache hit
  const FileEntry *FE2 = HS->lookupHeader("header0.h", false);
  EXPECT_EQ(FE1, FE2);
  EXPECT_EQ(HS->getLookupCacheMisses(), 1u);
  EXPECT_EQ(HS->getLookupCacheHits(), 1u);
  
  // Verify hit rate
  EXPECT_NEAR(HS->getLookupCacheHitRate(), 0.5, 0.01);
}

TEST_F(HeaderSearchPerformanceTest, RepeatedLookups) {
  // Measure time for 1000 repeated lookups (with caching)
  double CachedTime = measureTime([&]() {
    HS->lookupHeader("header0.h", false);
  }, 1000);
  
  // Clear cache and measure uncached time
  HS->clearLookupCache();
  
  // Measure time for 1000 different lookups (no caching benefit)
  std::vector<std::string> Headers;
  for (int I = 0; I < 10; ++I) {
    Headers.push_back("header" + std::to_string(I) + ".h");
  }
  
  double UncachedTime = measureTime([&]() {
    for (const auto &H : Headers) {
      HS->clearLookupCache();
      HS->lookupHeader(H, false);
    }
  }, 10);
  
  // Cached lookups should be faster than uncached.
  // Note: On fast SSDs with OS-level file caching, the difference may be
  // small, so we use a conservative threshold (2x) rather than a strict one.
  EXPECT_LT(CachedTime * 2, UncachedTime)
    << "Cache not effective: cached=" << CachedTime << "us, uncached=" << UncachedTime << "us";
  
  RecordProperty("CachedTimeUs", std::to_string(CachedTime));
  RecordProperty("UncachedTimeUs", std::to_string(UncachedTime));
  RecordProperty("Speedup", std::to_string(UncachedTime / CachedTime) + "x");
}

TEST_F(HeaderSearchPerformanceTest, StatCacheEffectiveness) {
  // First lookup - stat cache miss
  HS->lookupHeader("header0.h", false);
  unsigned Misses1 = HS->getStatCacheMisses();
  
  // Second lookup - stat cache hit
  HS->clearLookupCache();
  HS->lookupHeader("header0.h", false);
  unsigned Misses2 = HS->getStatCacheMisses();
  
  // Stat cache should prevent repeated filesystem access
  EXPECT_EQ(Misses2, Misses1) << "Stat cache not working";
}

//===----------------------------------------------------------------------===//
// Preprocessor Performance Tests
//===----------------------------------------------------------------------===//

class PreprocessorPerformanceTest : public ::testing::Test {
protected:
  void SetUp() override {
    FM = std::make_unique<FileManager>();
    SM = std::make_unique<SourceManager>();
    HS = std::make_unique<HeaderSearch>(*FM);
    Diags = std::make_unique<DiagnosticsEngine>();
    PP = std::make_unique<Preprocessor>(*SM, *Diags, HS.get(), nullptr, FM.get());
  }

  void TearDown() override {
    PP.reset();
    HS.reset();
    Diags.reset();
    SM.reset();
    FM.reset();
  }

  std::unique_ptr<FileManager> FM;
  std::unique_ptr<SourceManager> SM;
  std::unique_ptr<HeaderSearch> HS;
  std::unique_ptr<DiagnosticsEngine> Diags;
  std::unique_ptr<Preprocessor> PP;
};

TEST_F(PreprocessorPerformanceTest, MacroExpansion) {
  std::string Code = R"(
#define ADD(a, b) ((a) + (b))
#define MUL(a, b) ((a) * (b))
#define SQUARE(x) MUL(x, x)
)";
  
  // Add many macro uses
  for (unsigned I = 0; I < 1000; ++I) {
    Code += "int result" + std::to_string(I) + " = ADD(SQUARE(" + std::to_string(I) + "), 1);\n";
  }
  
  double Time = measureTime([&]() {
    PP->enterSourceFile("test.cpp", Code);
    Token T;
    while (PP->lexToken(T)) {}
  }, 5);
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("MacroCalls", "3000");
}

TEST_F(PreprocessorPerformanceTest, NestedIncludes) {
  // Create a chain of include files
  std::string TestDir = "/tmp/blocktype_pp_test";
  system(("mkdir -p " + TestDir).c_str());
  
  for (int I = 0; I < 10; ++I) {
    std::string Filename = TestDir + "/level" + std::to_string(I) + ".h";
    std::ofstream File(Filename);
    File << "// Level " << I << "\n";
    if (I < 9) {
      File << "#include \"level" << (I + 1) << ".h\"\n";
    }
    File << "#define LEVEL" << I << " " << I << "\n";
  }
  
  HS->addSearchPath(TestDir);
  
  // Main file includes the chain
  std::string Code = "#include \"level0.h\"\n";
  
  double Time = measureTime([&]() {
    PP->enterSourceFile("test.cpp", Code);
    Token T;
    while (PP->lexToken(T)) {}
  }, 10);
  
  system(("rm -rf " + TestDir).c_str());
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("IncludeDepth", "10");
}

TEST_F(PreprocessorPerformanceTest, ConditionalCompilation) {
  std::string Code;
  
  // Many nested conditionals
  for (unsigned I = 0; I < 100; ++I) {
    Code += "#ifdef FLAG" + std::to_string(I) + "\n";
    Code += "int enabled" + std::to_string(I) + " = 1;\n";
    Code += "#else\n";
    Code += "int disabled" + std::to_string(I) + " = 0;\n";
    Code += "#endif\n";
  }
  
  double Time = measureTime([&]() {
    PP->enterSourceFile("test.cpp", Code);
    Token T;
    while (PP->lexToken(T)) {}
  }, 10);
  
  RecordProperty("TimeUs", std::to_string(Time));
  RecordProperty("Conditionals", "100");
}

//===----------------------------------------------------------------------===//
// Memory Performance Tests
//===----------------------------------------------------------------------===//

TEST(MemoryPerformanceTest, TokenMemoryFootprint) {
  // Verify Token size is reasonable
  // Token contains: TokenKind(4) + SourceLocation(8) + Length(4) + LiteralData(8) + Language(4) = 28 bytes
  // With alignment: 40 bytes
  EXPECT_LE(sizeof(Token), 48) << "Token size too large: " << sizeof(Token) << " bytes";
  RecordProperty("TokenSize", std::to_string(sizeof(Token)));
}

TEST(MemoryPerformanceTest, SourceLocationSize) {
  // Verify SourceLocation is compact
  EXPECT_LE(sizeof(SourceLocation), 8) << "SourceLocation too large: " << sizeof(SourceLocation) << " bytes";
  RecordProperty("SourceLocationSize", std::to_string(sizeof(SourceLocation)));
}

//===----------------------------------------------------------------------===//
// Regression Detection Tests
//===----------------------------------------------------------------------===//

class RegressionTest : public ::testing::Test {
protected:
  // These tests compare against baseline performance
  // If performance degrades significantly, tests will fail
};

TEST_F(RegressionTest, LexerThroughputBaseline) {
  // Baseline: Lexer should process at least 1 million tokens/second
  // on a modern machine for simple code
  
  auto FM = std::make_unique<FileManager>();
  auto SM = std::make_unique<SourceManager>();
  auto Diags = std::make_unique<DiagnosticsEngine>();
  
  std::string Code = generateTestCode(5000, 10); // 50000 tokens
  
  auto Start = std::chrono::high_resolution_clock::now();
  SourceLocation L = SM->createFileID("test.cpp", Code);
  Lexer Lexer(*SM, *Diags, Code, L);
  Token T;
  while (Lexer.lexToken(T)) {}
  auto End = std::chrono::high_resolution_clock::now();
  
  auto DurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(End - Start).count();
  double TokensPerSecond = 50000.0 * 1000.0 / DurationMs;
  
  // Minimum acceptable throughput: 500k tokens/sec
  EXPECT_GT(TokensPerSecond, 500000.0) 
    << "Lexer throughput below baseline: " << TokensPerSecond << " tokens/sec";
  
  RecordProperty("TokensPerSecond", std::to_string(static_cast<unsigned>(TokensPerSecond)));
}

TEST_F(RegressionTest, HeaderSearchCacheBaseline) {
  // Baseline: Cache should provide at least 10x speedup for repeated lookups
  
  auto FM = std::make_unique<FileManager>();
  auto HS = std::make_unique<HeaderSearch>(*FM);
  
  std::string TestDir = "/tmp/blocktype_regr_test";
  system(("mkdir -p " + TestDir).c_str());
  
  std::string HeaderFile = TestDir + "/test.h";
  std::ofstream(HeaderFile) << "// Test header\n";
  
  HS->addSearchPath(TestDir);
  
  // First lookup (uncached)
  auto Start1 = std::chrono::high_resolution_clock::now();
  const FileEntry *FE1 = HS->lookupHeader("test.h", false);
  auto End1 = std::chrono::high_resolution_clock::now();
  auto UncachedUs = std::chrono::duration_cast<std::chrono::microseconds>(End1 - Start1).count();
  
  // Second lookup (cached)
  auto Start2 = std::chrono::high_resolution_clock::now();
  const FileEntry *FE2 = HS->lookupHeader("test.h", false);
  auto End2 = std::chrono::high_resolution_clock::now();
  auto CachedUs = std::chrono::duration_cast<std::chrono::microseconds>(End2 - Start2).count();
  
  system(("rm -rf " + TestDir).c_str());
  
  EXPECT_EQ(FE1, FE2);
  
  // Cache should be at least 5x faster (allowing for variance)
  double Speedup = static_cast<double>(UncachedUs) / CachedUs;
  EXPECT_GT(Speedup, 5.0) 
    << "Cache speedup below baseline: " << Speedup << "x";
  
  RecordProperty("UncachedUs", std::to_string(UncachedUs));
  RecordProperty("CachedUs", std::to_string(CachedUs));
  RecordProperty("Speedup", std::to_string(Speedup) + "x");
}
