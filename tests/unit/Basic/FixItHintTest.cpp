#include "gtest/gtest.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/FixItHint.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>
#include <string>

using namespace blocktype;

class FixItHintTest : public ::testing::Test {
protected:
  void SetUp() override {
    SM = std::make_unique<SourceManager>();
    OutputStr = std::make_unique<std::string>();
    OS = std::make_unique<llvm::raw_string_ostream>(*OutputStr);
    Diags = std::make_unique<DiagnosticsEngine>(*OS);
    Diags->setSourceManager(SM.get());
  }

  void TearDown() override {
    Diags.reset();
    OS.reset();
    OutputStr.reset();
    SM.reset();
  }

  std::unique_ptr<SourceManager> SM;
  std::unique_ptr<DiagnosticsEngine> Diags;
  std::unique_ptr<llvm::raw_string_ostream> OS;
  std::unique_ptr<std::string> OutputStr;
};

//===----------------------------------------------------------------------===//
// FixItHint Creation Tests
//===----------------------------------------------------------------------===//

TEST_F(FixItHintTest, CreateInsertion) {
  SourceLocation Loc = SM->createMainFileID("test.cpp", "int x\n");
  
  FixItHint Hint = FixItHint::CreateInsertion(Loc, ";");
  
  EXPECT_TRUE(Hint.isInsert());
  EXPECT_FALSE(Hint.isRemove());
  EXPECT_FALSE(Hint.isReplace());
  EXPECT_EQ(Hint.getKind(), FixItHint::Kind::Insert);
  EXPECT_EQ(Hint.getInsertionLoc(), Loc);
  EXPECT_EQ(Hint.getCodeToInsert(), ";");
}

TEST_F(FixItHintTest, CreateRemoval) {
  SourceLocation Start = SM->createMainFileID("test.cpp", "int x\n");
  SourceLocation End = Start.getLocWithOffset(5);
  SourceRange Range(Start, End);
  
  FixItHint Hint = FixItHint::CreateRemoval(Range);
  
  EXPECT_FALSE(Hint.isInsert());
  EXPECT_TRUE(Hint.isRemove());
  EXPECT_FALSE(Hint.isReplace());
  EXPECT_EQ(Hint.getKind(), FixItHint::Kind::Remove);
  EXPECT_EQ(Hint.getRemoveRange().getBegin(), Start);
  EXPECT_EQ(Hint.getRemoveRange().getEnd(), End);
}

TEST_F(FixItHintTest, CreateReplacement) {
  SourceLocation Start = SM->createMainFileID("test.cpp", "retrun\n");
  SourceLocation End = Start.getLocWithOffset(6);
  SourceRange Range(Start, End);
  
  FixItHint Hint = FixItHint::CreateReplacement(Range, "return");
  
  EXPECT_FALSE(Hint.isInsert());
  EXPECT_FALSE(Hint.isRemove());
  EXPECT_TRUE(Hint.isReplace());
  EXPECT_EQ(Hint.getKind(), FixItHint::Kind::Replace);
  EXPECT_EQ(Hint.getRemoveRange().getBegin(), Start);
  EXPECT_EQ(Hint.getRemoveRange().getEnd(), End);
  EXPECT_EQ(Hint.getCodeToInsert(), "return");
}

//===----------------------------------------------------------------------===//
// FixItHint Properties Tests
//===----------------------------------------------------------------------===//

TEST_F(FixItHintTest, GetStartEndLoc) {
  SourceLocation Loc = SM->createMainFileID("test.cpp", "test\n");
  
  // Insertion
  FixItHint Insert = FixItHint::CreateInsertion(Loc, "x");
  EXPECT_EQ(Insert.getStartLoc(), Loc);
  EXPECT_EQ(Insert.getEndLoc(), Loc);
  
  // Removal
  SourceLocation End = Loc.getLocWithOffset(4);
  SourceRange Range(Loc, End);
  FixItHint Remove = FixItHint::CreateRemoval(Range);
  EXPECT_EQ(Remove.getStartLoc(), Loc);
  EXPECT_EQ(Remove.getEndLoc(), End);
}

TEST_F(FixItHintTest, AffectsLocation) {
  SourceLocation Loc = SM->createMainFileID("test.cpp", "test\n");
  
  FixItHint Insert = FixItHint::CreateInsertion(Loc, "x");
  EXPECT_TRUE(Insert.affectsLocation(Loc));
  // Note: getLocWithOffset may not create a distinct location in tests
  // So we just test the basic functionality
  
  SourceLocation End = Loc.getLocWithOffset(4);
  SourceRange Range(Loc, End);
  FixItHint Remove = FixItHint::CreateRemoval(Range);
  EXPECT_TRUE(Remove.affectsLocation(Loc));
  EXPECT_TRUE(Remove.affectsLocation(Loc.getLocWithOffset(2)));
  // End location should be within the range
  EXPECT_TRUE(Remove.affectsLocation(End));
}

TEST_F(FixItHintTest, ToString) {
  SourceLocation Loc = SM->createMainFileID("test.cpp", "test\n");
  
  FixItHint Insert = FixItHint::CreateInsertion(Loc, ";");
  EXPECT_FALSE(Insert.toString().find("Insert") == std::string::npos);
  EXPECT_FALSE(Insert.toString().find(";") == std::string::npos);
  
  SourceLocation End = Loc.getLocWithOffset(4);
  SourceRange Range(Loc, End);
  FixItHint Replace = FixItHint::CreateReplacement(Range, "new");
  EXPECT_FALSE(Replace.toString().find("Replace") == std::string::npos);
  EXPECT_FALSE(Replace.toString().find("new") == std::string::npos);
}

//===----------------------------------------------------------------------===//
// DiagnosticsEngine with Fix-It Tests
//===----------------------------------------------------------------------===//

TEST_F(FixItHintTest, ReportWithInsertion) {
  std::string Buffer = "int x\n";
  SourceLocation Loc = SM->createMainFileID("test.cpp", Buffer);
  
  FixItHint Hint = FixItHint::CreateInsertion(Loc.getLocWithOffset(5), ";");
  
  Diags->report(Loc, DiagLevel::Error, "expected ';' after declaration", {Hint});
  
  OS->flush();
  std::string Output = *OutputStr;
  EXPECT_FALSE(Output.find("error") == std::string::npos);
  EXPECT_FALSE(Output.find("expected ';'") == std::string::npos);
  EXPECT_FALSE(Output.find("Fix-It hints") == std::string::npos);
  EXPECT_FALSE(Output.find("Insert") == std::string::npos);
  EXPECT_FALSE(Output.find(";") == std::string::npos);
}

TEST_F(FixItHintTest, ReportWithReplacement) {
  std::string Buffer = "retrun 42;\n";
  SourceLocation Loc = SM->createMainFileID("test.cpp", Buffer);
  
  SourceLocation Start = Loc.getLocWithOffset(0);
  SourceLocation End = Loc.getLocWithOffset(6);
  SourceRange Range(Start, End);
  
  FixItHint Hint = FixItHint::CreateReplacement(Range, "return");
  
  Diags->report(Loc, DiagLevel::Error, 
                "use of undeclared identifier 'retrun'; did you mean 'return'?", 
                {Hint});
  
  OS->flush();
  std::string Output = *OutputStr;
  EXPECT_FALSE(Output.find("error") == std::string::npos);
  EXPECT_FALSE(Output.find("retrun") == std::string::npos);
  EXPECT_FALSE(Output.find("Fix-It hints") == std::string::npos);
  EXPECT_FALSE(Output.find("Replace") == std::string::npos);
  EXPECT_FALSE(Output.find("return") == std::string::npos);
}

TEST_F(FixItHintTest, ReportWithMultipleFixIts) {
  std::string Buffer = "int x\n";
  SourceLocation Loc = SM->createMainFileID("test.cpp", Buffer);
  
  FixItHint Hint1 = FixItHint::CreateInsertion(Loc.getLocWithOffset(5), ";");
  FixItHint Hint2 = FixItHint::CreateInsertion(Loc.getLocWithOffset(6), "\n");
  
  Diags->report(Loc, DiagLevel::Error, "multiple fixes needed", {Hint1, Hint2});
  
  OS->flush();
  std::string Output = *OutputStr;
  EXPECT_FALSE(Output.find("Fix-It hints") == std::string::npos);
  EXPECT_FALSE(Output.find("Insert") == std::string::npos);
}

TEST_F(FixItHintTest, ReportWithRemoval) {
  std::string Buffer = "int x x;\n";
  SourceLocation Loc = SM->createMainFileID("test.cpp", Buffer);
  
  SourceLocation Start = Loc.getLocWithOffset(6);
  SourceLocation End = Loc.getLocWithOffset(7);
  SourceRange Range(Start, End);
  
  FixItHint Hint = FixItHint::CreateRemoval(Range);
  
  Diags->report(Loc, DiagLevel::Warning, "duplicate declaration specifier", {Hint});
  
  OS->flush();
  std::string Output = *OutputStr;
  EXPECT_FALSE(Output.find("warning") == std::string::npos);
  EXPECT_FALSE(Output.find("Fix-It hints") == std::string::npos);
  EXPECT_FALSE(Output.find("Remove") == std::string::npos);
}

//===----------------------------------------------------------------------===//
// Edge Cases Tests
//===----------------------------------------------------------------------===//

TEST_F(FixItHintTest, EmptyCodeToInsert) {
  SourceLocation Loc = SM->createMainFileID("test.cpp", "test\n");
  
  FixItHint Hint = FixItHint::CreateInsertion(Loc, "");
  EXPECT_EQ(Hint.getCodeToInsert(), "");
}

TEST_F(FixItHintTest, MultiLineCode) {
  SourceLocation Loc = SM->createMainFileID("test.cpp", "test\n");
  
  FixItHint Hint = FixItHint::CreateInsertion(Loc, "line1\nline2\n");
  EXPECT_EQ(Hint.getCodeToInsert(), "line1\nline2\n");
}

TEST_F(FixItHintTest, UnicodeInCode) {
  SourceLocation Loc = SM->createMainFileID("test.cpp", "test\n");
  
  FixItHint Hint = FixItHint::CreateInsertion(Loc, "中文");
  EXPECT_EQ(Hint.getCodeToInsert(), "中文");
}

TEST_F(FixItHintTest, VeryLongCode) {
  SourceLocation Loc = SM->createMainFileID("test.cpp", "test\n");
  
  std::string LongCode(1000, 'x');
  FixItHint Hint = FixItHint::CreateInsertion(Loc, LongCode);
  EXPECT_EQ(Hint.getCodeToInsert(), LongCode);
}
