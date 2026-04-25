#include <gtest/gtest.h>

#include "blocktype/IR/IRDiagnostic.h"

using namespace blocktype;
using namespace blocktype::diag;
using namespace blocktype::ir;

// ============================================================
// V1: StructuredDiagnostic 创建 + 字段访问
// ============================================================

TEST(IRDiagnosticTest, CreationAndFieldAccess) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Group = DiagnosticGroup::TypeMapping;
  D.Code = DiagnosticCode::TypeMappingFailed;
  D.Message = "Cannot map QualType to IRType";

  EXPECT_EQ(D.getLevel(), DiagnosticLevel::Error);
  EXPECT_EQ(D.getGroup(), DiagnosticGroup::TypeMapping);
  EXPECT_EQ(D.getCode(), DiagnosticCode::TypeMappingFailed);
  EXPECT_EQ(D.getMessage(), "Cannot map QualType to IRType");
}

// ============================================================
// V2: JSON 输出
// ============================================================

TEST(IRDiagnosticTest, JSONOutput) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Group = DiagnosticGroup::TypeMapping;
  D.Code = DiagnosticCode::TypeMappingFailed;
  D.Message = "Cannot map QualType to IRType";

  std::string JSON = D.toJSON();

  EXPECT_NE(JSON.find("\"level\": \"error\""), std::string::npos);
  EXPECT_NE(JSON.find("\"group\": \"TypeMapping\""), std::string::npos);
  EXPECT_NE(JSON.find("\"code\": \"TypeMappingFailed\""), std::string::npos);
  EXPECT_NE(JSON.find("TypeMappingFailed"), std::string::npos);
  EXPECT_NE(JSON.find("Cannot map QualType to IRType"), std::string::npos);
}

// ============================================================
// V3: Text 输出
// ============================================================

TEST(IRDiagnosticTest, TextOutput) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Warning;
  D.Group = DiagnosticGroup::BackendCodegen;
  D.Code = DiagnosticCode::UnsupportedType;
  D.Message = "Unsupported floating point type";

  std::string Text = D.toText();

  EXPECT_NE(Text.find("warning"), std::string::npos);
  EXPECT_NE(Text.find("BackendCodegen"), std::string::npos);
  EXPECT_NE(Text.find("UnsupportedType"), std::string::npos);
  EXPECT_NE(Text.find("Unsupported floating point type"), std::string::npos);
}

// ============================================================
// V4: Location 信息
// ============================================================

TEST(IRDiagnosticTest, LocationInOutput) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Code = DiagnosticCode::InvalidInstruction;
  D.Message = "bad operand";
  D.setLocation("test.cpp", 42, 7);

  std::string JSON = D.toJSON();
  EXPECT_NE(JSON.find("\"file\": \"test.cpp\""), std::string::npos);
  EXPECT_NE(JSON.find("\"line\": 42"), std::string::npos);
  EXPECT_NE(JSON.find("\"column\": 7"), std::string::npos);

  std::string Text = D.toText();
  EXPECT_NE(Text.find("test.cpp:42:7"), std::string::npos);
}

// ============================================================
// V5: Notes 附加
// ============================================================

TEST(IRDiagnosticTest, NotesInOutput) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Code = DiagnosticCode::VerificationFailed;
  D.Message = "module verification failed";
  D.addNote("see instruction in function 'main'");
  D.addNote("expected terminator instruction");

  std::string JSON = D.toJSON();
  EXPECT_NE(JSON.find("\"notes\""), std::string::npos);
  EXPECT_NE(JSON.find("see instruction"), std::string::npos);
  EXPECT_NE(JSON.find("expected terminator"), std::string::npos);

  std::string Text = D.toText();
  EXPECT_NE(Text.find("note: see instruction"), std::string::npos);
  EXPECT_NE(Text.find("note: expected terminator"), std::string::npos);
}

// ============================================================
// V6: DiagnosticLevel 名称
// ============================================================

TEST(IRDiagnosticTest, LevelNames) {
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Note), "note");
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Remark), "remark");
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Warning), "warning");
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Error), "error");
  EXPECT_STREQ(getDiagnosticLevelName(DiagnosticLevel::Fatal), "fatal");
}

// ============================================================
// V7: DiagnosticGroup 名称
// ============================================================

TEST(IRDiagnosticTest, GroupNames) {
  EXPECT_STREQ(getDiagnosticGroupName(DiagnosticGroup::TypeMapping), "TypeMapping");
  EXPECT_STREQ(getDiagnosticGroupName(DiagnosticGroup::IRVerification), "IRVerification");
  EXPECT_STREQ(getDiagnosticGroupName(DiagnosticGroup::Serialization), "Serialization");
}

// ============================================================
// V8: DiagnosticCode 名称 + getGroupForCode
// ============================================================

TEST(IRDiagnosticTest, CodeNamesAndGroupMapping) {
  EXPECT_STREQ(getDiagnosticCodeName(DiagnosticCode::TypeMappingFailed), "TypeMappingFailed");
  EXPECT_STREQ(getDiagnosticCodeName(DiagnosticCode::InvalidInstruction), "InvalidInstruction");
  EXPECT_STREQ(getDiagnosticCodeName(DiagnosticCode::SerializationFailed), "SerializationFailed");

  EXPECT_EQ(getGroupForCode(DiagnosticCode::TypeMappingFailed), DiagnosticGroup::TypeMapping);
  EXPECT_EQ(getGroupForCode(DiagnosticCode::CodegenFailed), DiagnosticGroup::BackendCodegen);
  EXPECT_EQ(getGroupForCode(DiagnosticCode::DeserializationFailed), DiagnosticGroup::Serialization);
}

// ============================================================
// V9: DiagnosticGroupManager 默认全启用
// ============================================================

TEST(IRDiagnosticTest, GroupManagerDefaultAllEnabled) {
  DiagnosticGroupManager Mgr;
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::Serialization));
}

// ============================================================
// V10: DiagnosticGroupManager 启用/禁用
// ============================================================

TEST(IRDiagnosticTest, GroupManagerEnableDisable) {
  DiagnosticGroupManager Mgr;

  Mgr.disableGroup(DiagnosticGroup::TypeMapping);
  EXPECT_FALSE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::IRVerification));

  Mgr.enableGroup(DiagnosticGroup::TypeMapping);
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
}

// ============================================================
// V11: DiagnosticGroupManager disableAll/enableAll
// ============================================================

TEST(IRDiagnosticTest, GroupManagerAllOrNone) {
  DiagnosticGroupManager Mgr;

  Mgr.disableAll();
  EXPECT_FALSE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
  EXPECT_FALSE(Mgr.isGroupEnabled(DiagnosticGroup::Serialization));

  Mgr.enableAll();
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::TypeMapping));
  EXPECT_TRUE(Mgr.isGroupEnabled(DiagnosticGroup::Serialization));
}

// ============================================================
// V12: JSON 转义
// ============================================================

TEST(IRDiagnosticTest, JSONEscape) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Error;
  D.Code = DiagnosticCode::Unknown;
  D.Message = "path \"C:\\test\" has\nnewlines";

  std::string JSON = D.toJSON();
  EXPECT_NE(JSON.find("\\\"C:\\\\test\\\""), std::string::npos);
  EXPECT_NE(JSON.find("\\n"), std::string::npos);
}

// ============================================================
// V13: 无 Location 时 JSON 不输出 location 字段
// ============================================================

TEST(IRDiagnosticTest, NoLocationOmitField) {
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Note;
  D.Code = DiagnosticCode::Unknown;
  D.Message = "just a note";

  std::string JSON = D.toJSON();
  EXPECT_EQ(JSON.find("\"location\""), std::string::npos);
}

// ============================================================
// V14: StructuredDiagEmitter 抽象基类（派生测试）
// ============================================================

namespace {
class MockEmitter : public StructuredDiagEmitter {
public:
  mutable StructuredDiagnostic Last;
  mutable int Count = 0;
  void emit(const StructuredDiagnostic& D) override {
    Last = D;
    ++Count;
  }
};
}

TEST(IRDiagnosticTest, EmitterInterface) {
  MockEmitter E;
  StructuredDiagnostic D;
  D.Level = DiagnosticLevel::Fatal;
  D.Code = DiagnosticCode::Unknown;
  D.Message = "fatal error";

  E.emit(D);
  EXPECT_EQ(E.Count, 1);
  EXPECT_EQ(E.Last.getLevel(), DiagnosticLevel::Fatal);
  EXPECT_EQ(E.Last.getMessage(), "fatal error");
}
