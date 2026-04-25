#include <fstream>

#include <gtest/gtest.h>

#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRSerializer.h"
#include "blocktype/IR/IRVerifier.h"

using namespace blocktype;
using namespace blocktype::ir;

// Helper to build a simple module with one function
static std::unique_ptr<IRModule> buildSimpleModule(IRContext& Ctx, IRTypeContext& TCtx,
                                                     StringRef Name = "test") {
  auto M = std::make_unique<IRModule>(Name, TCtx, "x86_64-unknown-linux-gnu");
  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
  auto* F = M->getOrInsertFunction("add", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  auto* Sum = Builder.createAdd(Builder.getInt32(1), Builder.getInt32(2), "sum");
  Builder.createRet(Sum);
  return M;
}

// ============================================================================
// V1: Text format write
// ============================================================================

TEST(SerializerTest, TextWrite) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();
  auto M = buildSimpleModule(Ctx, TCtx, "test");

  std::string Text;
  raw_string_ostream OS(Text);
  bool OK = IRWriter::writeText(*M, OS);
  EXPECT_TRUE(OK) << "writeText should succeed";
  EXPECT_FALSE(Text.empty()) << "Output should not be empty";
  EXPECT_NE(Text.find("module"), std::string::npos);
  EXPECT_NE(Text.find("function"), std::string::npos);
}

// ============================================================================
// V2: Text format round-trip
// ============================================================================

TEST(SerializerTest, TextRoundTrip) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();
  auto M = buildSimpleModule(Ctx, TCtx, "roundtrip");

  std::string Text;
  raw_string_ostream OS(Text);
  IRWriter::writeText(*M, OS);

  SerializationDiagnostic Diag;
  auto Parsed = IRReader::parseText(StringRef(Text), TCtx, &Diag);
  ASSERT_NE(Parsed, nullptr) << "parseText should succeed: " << Diag.Message;
  EXPECT_EQ(Parsed->getName(), M->getName());
  EXPECT_EQ(Parsed->getNumFunctions(), M->getNumFunctions());
  EXPECT_NE(Parsed->getFunction("add"), nullptr);

  auto* PF = Parsed->getFunction("add");
  ASSERT_NE(PF, nullptr);
  EXPECT_TRUE(PF->isDefinition());
  EXPECT_EQ(PF->getNumBasicBlocks(), 1u);
}

// ============================================================================
// V3: Binary format round-trip
// ============================================================================

TEST(SerializerTest, BinaryRoundTrip) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();
  auto M = buildSimpleModule(Ctx, TCtx, "bin_roundtrip");

  std::string Binary;
  raw_string_ostream BOS(Binary);
  bool WriteOK = IRWriter::writeBitcode(*M, BOS);
  EXPECT_TRUE(WriteOK);

  ASSERT_GE(Binary.size(), 4u);
  EXPECT_EQ(Binary[0], 'B');
  EXPECT_EQ(Binary[1], 'T');
  EXPECT_EQ(Binary[2], 'I');
  EXPECT_EQ(Binary[3], 'R');

  SerializationDiagnostic Diag;
  auto Parsed = IRReader::parseBitcode(
      StringRef(Binary.data(), Binary.size()), TCtx, &Diag);
  ASSERT_NE(Parsed, nullptr) << "parseBitcode should succeed: " << Diag.Message;
  EXPECT_EQ(Parsed->getName(), "bin_roundtrip");
  EXPECT_EQ(Parsed->getNumFunctions(), M->getNumFunctions());
  EXPECT_NE(Parsed->getFunction("add"), nullptr);
  auto* PF = Parsed->getFunction("add");
  ASSERT_NE(PF, nullptr);
  EXPECT_EQ(PF->getNumBasicBlocks(), 1u);
}

// ============================================================================
// V4: Error handling — invalid text format
// ============================================================================

TEST(SerializerTest, InvalidTextFormat) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();

  SerializationDiagnostic Diag;
  auto M = IRReader::parseText("this is not valid BTIR", TCtx, &Diag);
  EXPECT_EQ(M, nullptr) << "Invalid text should fail to parse";
  EXPECT_EQ(Diag.Kind, SerializationErrorKind::InvalidFormat)
      << "Expected InvalidFormat error";
  EXPECT_GT(Diag.Line, 0u) << "Expected non-zero line number";
  EXPECT_FALSE(Diag.Message.empty()) << "Expected non-empty error message";
}

// ============================================================================
// V5: Error handling — invalid binary magic number
// ============================================================================

TEST(SerializerTest, InvalidBinaryMagic) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();

  const char* BadData = "XXXX";
  SerializationDiagnostic Diag;
  auto M = IRReader::parseBitcode(StringRef(BadData, 4), TCtx, &Diag);
  EXPECT_EQ(M, nullptr) << "Invalid magic should fail";
  EXPECT_EQ(Diag.Kind, SerializationErrorKind::InvalidFormat);
}

// ============================================================================
// V6: readFile auto format detection
// ============================================================================

TEST(SerializerTest, ReadFileTextDetection) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();
  auto M = buildSimpleModule(Ctx, TCtx, "file_test");

  // Use a simpler module without target triple to avoid issues
  IRModule SimpleM("file_test", TCtx);
  auto* FTy = TCtx.getFunctionType(TCtx.getVoidType(), {});
  auto* F = SimpleM.getOrInsertFunction("dummy", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  Builder.createRetVoid();

  std::string Text;
  raw_string_ostream OS(Text);
  IRWriter::writeText(SimpleM, OS);

  // Write to temp file
  const char* TmpPath = "/tmp/bt_test_file.btir";
  {
    std::ofstream File(TmpPath, std::ios::binary);
    File.write(Text.data(), Text.size());
  }

  SerializationDiagnostic Diag;
  auto Loaded = IRReader::readFile(TmpPath, TCtx, &Diag);
  ASSERT_NE(Loaded, nullptr) << "readFile should succeed: " << Diag.Message;
  EXPECT_EQ(Loaded->getName(), "file_test");
}

// ============================================================================
// V7: Error handling — version mismatch
// ============================================================================

TEST(SerializerTest, VersionMismatch) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();

  char BadVersion[26] = {};
  BadVersion[0] = 'B'; BadVersion[1] = 'T'; BadVersion[2] = 'I'; BadVersion[3] = 'R';
  BadVersion[4] = 99; BadVersion[5] = 0;  // Major = 99
  BadVersion[6] = 0;  BadVersion[7] = 0;  // Minor = 0
  BadVersion[8] = 0;  BadVersion[9] = 0;  // Patch = 0

  SerializationDiagnostic Diag;
  auto M = IRReader::parseBitcode(StringRef(BadVersion, 26), TCtx, &Diag);
  EXPECT_EQ(M, nullptr) << "Version mismatch should fail";
  EXPECT_EQ(Diag.Kind, SerializationErrorKind::VersionMismatch);
}

// ============================================================================
// V8: Module with global variable round-trip
// ============================================================================

TEST(SerializerTest, GlobalVariableRoundTrip) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();
  IRModule M("globals_test", TCtx, "x86_64-unknown-linux-gnu");

  auto* GV = M.getOrInsertGlobal("g_counter", TCtx.getInt32Ty());

  std::string Text;
  raw_string_ostream OS(Text);
  IRWriter::writeText(M, OS);

  auto Parsed = IRReader::parseText(StringRef(Text), TCtx);
  ASSERT_NE(Parsed, nullptr);
  EXPECT_NE(Parsed->getGlobalVariable("g_counter"), nullptr);
}

// ============================================================================
// ICmp predicate round-trip (text format)
// ============================================================================

TEST(SerializerTest, ICmpPredicateRoundTrip) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();
  IRModule M("icmp_rt", TCtx, "x86_64-unknown-linux-gnu");

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
  auto* F = M.getOrInsertFunction("cmp_sgt", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  auto* A = Builder.getInt32(1);
  auto* B = Builder.getInt32(2);
  auto* ICmp = Builder.createICmp(ICmpPred::SGT, A, B, "cmp");
  Builder.createRet(ICmp);

  // Text round-trip
  std::string Text;
  raw_string_ostream OS(Text);
  IRWriter::writeText(M, OS);
  EXPECT_NE(Text.find("icmp sgt"), std::string::npos);

  auto Parsed = IRReader::parseText(StringRef(Text), TCtx);
  ASSERT_NE(Parsed, nullptr);
  auto* PF = Parsed->getFunction("cmp_sgt");
  ASSERT_NE(PF, nullptr);
  auto* PEntry = PF->getEntryBlock();
  ASSERT_NE(PEntry, nullptr);
  for (auto& I : PEntry->getInstList()) {
    if (I->getOpcode() == Opcode::ICmp) {
      EXPECT_EQ(I->getICmpPredicate(), ICmpPred::SGT);
      break;
    }
  }

  // Binary round-trip
  std::string Binary;
  raw_string_ostream BOS(Binary);
  IRWriter::writeBitcode(M, BOS);
  SerializationDiagnostic Diag;
  auto BinParsed = IRReader::parseBitcode(StringRef(Binary.data(), Binary.size()), TCtx, &Diag);
  ASSERT_NE(BinParsed, nullptr) << Diag.Message;
  auto* BF = BinParsed->getFunction("cmp_sgt");
  ASSERT_NE(BF, nullptr);
  for (auto& I : BF->getEntryBlock()->getInstList()) {
    if (I->getOpcode() == Opcode::ICmp) {
      EXPECT_EQ(I->getICmpPredicate(), ICmpPred::SGT);
      break;
    }
  }
}

// ============================================================================
// FCmp predicate round-trip (text format)
// ============================================================================

TEST(SerializerTest, FCmpPredicateRoundTrip) {
  IRContext Ctx;
  auto& TCtx = Ctx.getTypeContext();
  IRModule M("fcmp_rt", TCtx, "x86_64-unknown-linux-gnu");

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getInt32Ty(), TCtx.getInt32Ty()});
  auto* F = M.getOrInsertFunction("fcmp_uno", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(Ctx);
  Builder.setInsertPoint(Entry);
  auto* A = Builder.getInt32(1);
  auto* B = Builder.getInt32(2);
  auto* FCmp = Builder.createFCmp(FCmpPred::UNO, A, B, "cmp");
  Builder.createRet(FCmp);

  // Text round-trip
  std::string Text;
  raw_string_ostream OS(Text);
  IRWriter::writeText(M, OS);
  EXPECT_NE(Text.find("fcmp uno"), std::string::npos);

  auto Parsed = IRReader::parseText(StringRef(Text), TCtx);
  ASSERT_NE(Parsed, nullptr);
  auto* PF = Parsed->getFunction("fcmp_uno");
  ASSERT_NE(PF, nullptr);
  for (auto& I : PF->getEntryBlock()->getInstList()) {
    if (I->getOpcode() == Opcode::FCmp) {
      EXPECT_EQ(I->getFCmpPredicate(), FCmpPred::UNO);
      break;
    }
  }

  // Binary round-trip
  std::string Binary;
  raw_string_ostream BOS(Binary);
  IRWriter::writeBitcode(M, BOS);
  SerializationDiagnostic Diag;
  auto BinParsed = IRReader::parseBitcode(StringRef(Binary.data(), Binary.size()), TCtx, &Diag);
  ASSERT_NE(BinParsed, nullptr) << Diag.Message;
  auto* BF = BinParsed->getFunction("fcmp_uno");
  ASSERT_NE(BF, nullptr);
  for (auto& I : BF->getEntryBlock()->getInstList()) {
    if (I->getOpcode() == Opcode::FCmp) {
      EXPECT_EQ(I->getFCmpPredicate(), FCmpPred::UNO);
      break;
    }
  }
}
