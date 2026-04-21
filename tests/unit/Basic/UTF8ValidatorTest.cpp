#include "gtest/gtest.h"
#include "blocktype/Basic/UTF8Validator.h"
#include <chrono>
#include <random>
#include <string>

using namespace blocktype;

class UTF8ValidatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Generate test data
    AsciiData = generateAscii(100000);
    MixedData = generateMixed(100000);
    CJKData = generateCJK(100000);
    InvalidData = generateInvalid();
  }

  std::string generateAscii(size_t Size) {
    std::string Result;
    Result.reserve(Size);
    std::mt19937 Rng(42);
    std::uniform_int_distribution<> Dist(32, 126);
    
    for (size_t i = 0; i < Size; i++) {
      Result += static_cast<char>(Dist(Rng));
    }
    return Result;
  }

  std::string generateMixed(size_t Size) {
    std::string Result;
    Result.reserve(Size * 3); // UTF-8 can be up to 3x larger
    std::mt19937 Rng(42);
    
    for (size_t i = 0; i < Size; i++) {
      if (i % 3 == 0) {
        // ASCII
        Result += 'a' + (i % 26);
      } else if (i % 3 == 1) {
        // 2-byte UTF-8 (Latin Extended)
        Result += static_cast<char>(0xC0 | (0xC0 >> 6));
        Result += static_cast<char>(0x80 | (0xC0 & 0x3F));
      } else {
        // 3-byte UTF-8 (CJK)
        uint32_t CP = 0x4E00 + (i % 20000); // CJK Unified Ideographs
        Result += static_cast<char>(0xE0 | (CP >> 12));
        Result += static_cast<char>(0x80 | ((CP >> 6) & 0x3F));
        Result += static_cast<char>(0x80 | (CP & 0x3F));
      }
    }
    return Result;
  }

  std::string generateCJK(size_t Size) {
    std::string Result;
    Result.reserve(Size * 3);
    std::mt19937 Rng(42);
    
    for (size_t i = 0; i < Size; i++) {
      uint32_t CP = 0x4E00 + (Rng() % 20000);
      Result += static_cast<char>(0xE0 | (CP >> 12));
      Result += static_cast<char>(0x80 | ((CP >> 6) & 0x3F));
      Result += static_cast<char>(0x80 | (CP & 0x3F));
    }
    return Result;
  }

  std::string generateInvalid() {
    std::string Result;
    // Invalid lead byte
    Result += static_cast<char>(0xFF);
    Result += static_cast<char>(0xFE);
    
    // Invalid continuation byte
    Result += static_cast<char>(0xC2);
    Result += static_cast<char>(0xC0); // Should be 0x80-0xBF
    
    // Overlong encoding
    Result += static_cast<char>(0xC0);
    Result += static_cast<char>(0x80);
    
    // Surrogate pair
    Result += static_cast<char>(0xED);
    Result += static_cast<char>(0xA0);
    Result += static_cast<char>(0x80);
    
    return Result;
  }

  std::string AsciiData;
  std::string MixedData;
  std::string CJKData;
  std::string InvalidData;
};

//===----------------------------------------------------------------------===//
// Correctness Tests
//===----------------------------------------------------------------------===//

TEST_F(UTF8ValidatorTest, ValidateEmpty) {
  EXPECT_TRUE(UTF8Validator::validate(""));
  EXPECT_TRUE(UTF8Validator::validate(nullptr, 0));
}

TEST_F(UTF8ValidatorTest, ValidateASCII) {
  EXPECT_TRUE(UTF8Validator::validate("Hello, World!"));
  EXPECT_TRUE(UTF8Validator::validate("The quick brown fox jumps over the lazy dog"));
  EXPECT_TRUE(UTF8Validator::validate(AsciiData));
}

TEST_F(UTF8ValidatorTest, ValidateUTF8_2Byte) {
  // Latin Extended-A (2-byte sequences)
  std::string Text = "Ä Ö Ü ß";
  EXPECT_TRUE(UTF8Validator::validate(Text));
  
  // Single 2-byte character
  std::string TwoByte;
  TwoByte += static_cast<char>(0xC3);
  TwoByte += static_cast<char>(0xA9); // é
  EXPECT_TRUE(UTF8Validator::validate(TwoByte));
}

TEST_F(UTF8ValidatorTest, ValidateUTF8_3Byte) {
  // CJK characters (3-byte sequences)
  std::string Text = "中文测试";
  EXPECT_TRUE(UTF8Validator::validate(Text));
  
  // Single CJK character
  std::string ThreeByte;
  uint32_t CP = 0x4E2D; // 中
  ThreeByte += static_cast<char>(0xE0 | (CP >> 12));
  ThreeByte += static_cast<char>(0x80 | ((CP >> 6) & 0x3F));
  ThreeByte += static_cast<char>(0x80 | (CP & 0x3F));
  EXPECT_TRUE(UTF8Validator::validate(ThreeByte));
}

TEST_F(UTF8ValidatorTest, ValidateUTF8_4Byte) {
  // Emoji (4-byte sequences)
  std::string Text = "🎉🚀💻";
  EXPECT_TRUE(UTF8Validator::validate(Text));
  
  // Single 4-byte character
  std::string FourByte;
  uint32_t CP = 0x1F600; // 😀
  FourByte += static_cast<char>(0xF0 | (CP >> 18));
  FourByte += static_cast<char>(0x80 | ((CP >> 12) & 0x3F));
  FourByte += static_cast<char>(0x80 | ((CP >> 6) & 0x3F));
  FourByte += static_cast<char>(0x80 | (CP & 0x3F));
  EXPECT_TRUE(UTF8Validator::validate(FourByte));
}

TEST_F(UTF8ValidatorTest, ValidateMixed) {
  EXPECT_TRUE(UTF8Validator::validate(MixedData));
  EXPECT_TRUE(UTF8Validator::validate(CJKData));
}

TEST_F(UTF8ValidatorTest, ValidateInvalid) {
  EXPECT_FALSE(UTF8Validator::validate(InvalidData));
  
  // Invalid lead byte
  std::string Invalid1;
  Invalid1 += static_cast<char>(0xFF);
  EXPECT_FALSE(UTF8Validator::validate(Invalid1));
  
  // Incomplete sequence
  std::string Invalid2;
  Invalid2 += static_cast<char>(0xC3); // Missing continuation byte
  EXPECT_FALSE(UTF8Validator::validate(Invalid2));
  
  // Invalid continuation byte
  std::string Invalid3;
  Invalid3 += static_cast<char>(0xC2);
  Invalid3 += static_cast<char>(0xC0); // Not a continuation byte
  EXPECT_FALSE(UTF8Validator::validate(Invalid3));
}

TEST_F(UTF8ValidatorTest, FindInvalid) {
  std::string Valid = "Hello 中文 World";
  EXPECT_EQ(UTF8Validator::findInvalid(Valid.data(), Valid.data() + Valid.size()),
            Valid.data() + Valid.size());
  
  std::string Invalid = "Hello ";
  Invalid += static_cast<char>(0xFF);
  Invalid += " World";
  const char *InvalidPos = UTF8Validator::findInvalid(Invalid.data(), 
                                                       Invalid.data() + Invalid.size());
  EXPECT_EQ(InvalidPos, Invalid.data() + 6);
}

//===----------------------------------------------------------------------===//
// Performance Tests
//===----------------------------------------------------------------------===//

TEST_F(UTF8ValidatorTest, PerformanceASCII) {
  const int Iterations = 100;
  
  auto Start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < Iterations; i++) {
    UTF8Validator::validate(AsciiData);
  }
  auto End = std::chrono::high_resolution_clock::now();
  
  auto Duration = std::chrono::duration_cast<std::chrono::microseconds>(End - Start);
  double MBPerSec = (AsciiData.size() * Iterations) / (Duration.count() / 1000000.0) / (1024 * 1024);
  
  std::cout << "ASCII Performance: " << MBPerSec << " MB/s\n";
  std::cout << "Data size: " << AsciiData.size() << " bytes\n";
  std::cout << "Duration: " << Duration.count() << " μs\n";
  
  // Should be at least 100 MB/s
  EXPECT_GT(MBPerSec, 100.0);
}

TEST_F(UTF8ValidatorTest, PerformanceMixed) {
  const int Iterations = 100;
  
  auto Start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < Iterations; i++) {
    UTF8Validator::validate(MixedData);
  }
  auto End = std::chrono::high_resolution_clock::now();
  
  auto Duration = std::chrono::duration_cast<std::chrono::microseconds>(End - Start);
  double MBPerSec = (MixedData.size() * Iterations) / (Duration.count() / 1000000.0) / (1024 * 1024);
  
  std::cout << "Mixed Performance: " << MBPerSec << " MB/s\n";
  std::cout << "Data size: " << MixedData.size() << " bytes\n";
  std::cout << "Duration: " << Duration.count() << " μs\n";
  
  // Should be at least 50 MB/s
  EXPECT_GT(MBPerSec, 50.0);
}

TEST_F(UTF8ValidatorTest, PerformanceCJK) {
  const int Iterations = 100;
  
  auto Start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < Iterations; i++) {
    UTF8Validator::validate(CJKData);
  }
  auto End = std::chrono::high_resolution_clock::now();
  
  auto Duration = std::chrono::duration_cast<std::chrono::microseconds>(End - Start);
  double MBPerSec = (CJKData.size() * Iterations) / (Duration.count() / 1000000.0) / (1024 * 1024);
  
  std::cout << "CJK Performance: " << MBPerSec << " MB/s\n";
  std::cout << "Data size: " << CJKData.size() << " bytes\n";
  std::cout << "Duration: " << Duration.count() << " μs\n";
  
  // Should be at least 50 MB/s
  EXPECT_GT(MBPerSec, 50.0);
}

//===----------------------------------------------------------------------===//
// Edge Cases Tests
//===----------------------------------------------------------------------===//

TEST_F(UTF8ValidatorTest, SingleByte) {
  EXPECT_TRUE(UTF8Validator::validate("a"));
  EXPECT_TRUE(UTF8Validator::validate(" "));
  EXPECT_TRUE(UTF8Validator::validate("\n"));
}

TEST_F(UTF8ValidatorTest, BoundaryCodePoints) {
  // U+007F (last ASCII)
  std::string S1;
  S1 += static_cast<char>(0x7F);
  EXPECT_TRUE(UTF8Validator::validate(S1));
  
  // U+0080 (first 2-byte)
  std::string S2;
  S2 += static_cast<char>(0xC2);
  S2 += static_cast<char>(0x80);
  EXPECT_TRUE(UTF8Validator::validate(S2));
  
  // U+07FF (last 2-byte)
  std::string S3;
  S3 += static_cast<char>(0xDF);
  S3 += static_cast<char>(0xBF);
  EXPECT_TRUE(UTF8Validator::validate(S3));
  
  // U+0800 (first 3-byte)
  std::string S4;
  S4 += static_cast<char>(0xE0);
  S4 += static_cast<char>(0xA0);
  S4 += static_cast<char>(0x80);
  EXPECT_TRUE(UTF8Validator::validate(S4));
  
  // U+FFFF (last 3-byte, excluding surrogates)
  std::string S5;
  S5 += static_cast<char>(0xEF);
  S5 += static_cast<char>(0xBF);
  S5 += static_cast<char>(0xBF);
  EXPECT_TRUE(UTF8Validator::validate(S5));
  
  // U+10000 (first 4-byte)
  std::string S6;
  S6 += static_cast<char>(0xF0);
  S6 += static_cast<char>(0x90);
  S6 += static_cast<char>(0x80);
  S6 += static_cast<char>(0x80);
  EXPECT_TRUE(UTF8Validator::validate(S6));
  
  // U+10FFFF (last valid Unicode code point)
  std::string S7;
  S7 += static_cast<char>(0xF4);
  S7 += static_cast<char>(0x8F);
  S7 += static_cast<char>(0xBF);
  S7 += static_cast<char>(0xBF);
  EXPECT_TRUE(UTF8Validator::validate(S7));
  
  // U+110000 (beyond Unicode range)
  std::string S8;
  S8 += static_cast<char>(0xF4);
  S8 += static_cast<char>(0x90);
  S8 += static_cast<char>(0x80);
  S8 += static_cast<char>(0x80);
  EXPECT_FALSE(UTF8Validator::validate(S8));
}
