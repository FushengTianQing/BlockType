#include <gtest/gtest.h>

#include "blocktype/IR/IRDebugInfo.h"
#include "blocktype/IR/IRDebugMetadata.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRModule.h"

using namespace blocktype;
using namespace blocktype::ir;

// ============================================================
// V1: SourceLocation 基本功能
// ============================================================

TEST(DebugInfoTest, SourceLocationBasic) {
  // 默认无效
  SourceLocation SL;
  EXPECT_FALSE(SL.isValid());
  EXPECT_EQ(SL.Line, 0u);
  EXPECT_EQ(SL.Column, 0u);

  // 设置后有效
  SL.Filename = "test.cpp";
  SL.Line = 42;
  SL.Column = 7;
  EXPECT_TRUE(SL.isValid());
  EXPECT_EQ(SL.Filename, "test.cpp");
  EXPECT_EQ(SL.Line, 42u);
  EXPECT_EQ(SL.Column, 7u);

  // 比较运算符
  SourceLocation SL2;
  SL2.Filename = "test.cpp";
  SL2.Line = 42;
  SL2.Column = 7;
  EXPECT_EQ(SL, SL2);

  SL2.Column = 8;
  EXPECT_NE(SL, SL2);
}

// ============================================================
// V2: DICompileUnit / DIType / DISubprogram / DILocation
// ============================================================

TEST(DebugInfoTest, BasicDebugMetadata) {
  // DICompileUnit
  DICompileUnit CU("main.cpp", "BlockType 1.0", 33);
  EXPECT_EQ(CU.getSourceFile(), "main.cpp");
  EXPECT_EQ(CU.getProducer(), "BlockType 1.0");
  EXPECT_EQ(CU.getLanguage(), 33u);
  EXPECT_EQ(CU.getDebugKind(), DebugMetadata::DebugKind::CompileUnit);
  EXPECT_TRUE(DICompileUnit::classof(&CU));

  CU.setSourceFile("other.cpp");
  EXPECT_EQ(CU.getSourceFile(), "other.cpp");

  // DIType
  DIType T("int", 32, 32);
  EXPECT_EQ(T.getName(), "int");
  EXPECT_EQ(T.getSizeInBits(), 32u);
  EXPECT_EQ(T.getAlignInBits(), 32u);
  EXPECT_EQ(T.getDebugKind(), DebugMetadata::DebugKind::Type);
  EXPECT_TRUE(DIType::classof(&T));

  T.setSizeInBits(64);
  EXPECT_EQ(T.getSizeInBits(), 64u);

  // DISubprogram
  DISubprogram SP("main", &CU);
  EXPECT_EQ(SP.getName(), "main");
  EXPECT_EQ(SP.getUnit(), &CU);
  EXPECT_EQ(SP.getDebugKind(), DebugMetadata::DebugKind::Subprogram);
  EXPECT_TRUE(DISubprogram::classof(&SP));

  // DILocation
  DILocation Loc(10, 5, &SP);
  EXPECT_EQ(Loc.getLine(), 10u);
  EXPECT_EQ(Loc.getColumn(), 5u);
  EXPECT_EQ(Loc.getScope(), &SP);
  EXPECT_EQ(Loc.getDebugKind(), DebugMetadata::DebugKind::Location);
  EXPECT_TRUE(DILocation::classof(&Loc));
}

TEST(DebugInfoTest, PrintMethods) {
  DICompileUnit CU("test.cpp", "BlockType", 33);
  std::string Str;
  raw_string_ostream OS(Str);
  CU.print(OS);
  OS.flush();
  EXPECT_NE(Str.find("DICompileUnit"), std::string::npos);
  EXPECT_NE(Str.find("test.cpp"), std::string::npos);

  Str.clear();
  DIType T("int", 32, 32);
  T.print(OS);
  OS.flush();
  EXPECT_NE(Str.find("DIType"), std::string::npos);
  EXPECT_NE(Str.find("int"), std::string::npos);

  Str.clear();
  DISubprogram SP("func");
  SP.print(OS);
  OS.flush();
  EXPECT_NE(Str.find("DISubprogram"), std::string::npos);
  EXPECT_NE(Str.find("func"), std::string::npos);

  Str.clear();
  DILocation Loc(10, 5);
  Loc.print(OS);
  OS.flush();
  EXPECT_NE(Str.find("DILocation"), std::string::npos);
  EXPECT_NE(Str.find("10"), std::string::npos);
}

// ============================================================
// V3: IRInstructionDebugInfo 完整版
// ============================================================

TEST(DebugInfoTest, InstructionDebugInfoComplete) {
  // 默认构造 — 无调试信息
  debug::IRInstructionDebugInfo DI;
  EXPECT_FALSE(DI.hasLocation());
  EXPECT_FALSE(DI.hasSubprogram());
  EXPECT_FALSE(DI.isArtificial());
  EXPECT_FALSE(DI.isInlined());
  EXPECT_FALSE(DI.hasInlinedAt());
  EXPECT_FALSE(DI.hasTypeKind());

  // 设置源码位置
  SourceLocation SL;
  SL.Filename = "test.cpp";
  SL.Line = 42;
  SL.Column = 7;
  DI.setLocation(SL);
  EXPECT_TRUE(DI.hasLocation());
  EXPECT_EQ(DI.getLocation().Line, 42u);
  EXPECT_EQ(DI.getLocation().Column, 7u);

  // 设置人工生成标记
  DI.setArtificial(true);
  EXPECT_TRUE(DI.isArtificial());

  // 设置内联标记
  DI.setInlined(true);
  EXPECT_TRUE(DI.isInlined());

  // 设置内联位置
  SourceLocation IL;
  IL.Filename = "header.h";
  IL.Line = 100;
  DI.setInlinedAt(IL);
  EXPECT_TRUE(DI.hasInlinedAt());
  EXPECT_EQ(DI.getInlinedAt().Filename, "header.h");
  EXPECT_EQ(DI.getInlinedAt().Line, 100u);

  // 清除内联位置
  DI.clearInlinedAt();
  EXPECT_FALSE(DI.hasInlinedAt());

  // 设置类型分类
  DI.setTypeKind(debug::DIType::Kind::Pointer);
  EXPECT_TRUE(DI.hasTypeKind());
  EXPECT_EQ(DI.getTypeKind(), debug::DIType::Kind::Pointer);

  DI.clearTypeKind();
  EXPECT_FALSE(DI.hasTypeKind());
  // 默认值
  EXPECT_EQ(DI.getTypeKind(), debug::DIType::Kind::Basic);

  // 设置子程序
  DICompileUnit CU("test.cpp");
  DISubprogram SP("main", &CU);
  DI.setSubprogram(&SP);
  EXPECT_TRUE(DI.hasSubprogram());
  EXPECT_EQ(DI.getSubprogram(), &SP);

  DI.clearSubprogram();
  EXPECT_FALSE(DI.hasSubprogram());
}

// ============================================================
// V4: IRInstruction setDebugInfo 使用完整类型
// ============================================================

TEST(DebugInfoTest, IRInstructionDebugInfoIntegration) {
  IRContext IRCtx;
  IRTypeContext& Ctx = IRCtx.getTypeContext();
  IRModule Mod("test", Ctx);
  auto* FTy = Ctx.getFunctionType(Ctx.getInt32Ty(), {});
  auto* F = Mod.getOrInsertFunction("test_fn", FTy);
  auto* Entry = F->addBasicBlock("entry");
  IRBuilder Builder(IRCtx);
  Builder.setInsertPoint(Entry);
  auto* One = Builder.getInt32(1);
  auto* Two = Builder.getInt32(2);
  auto* Add = Builder.createAdd(One, Two, "sum");

  // 默认无调试信息
  EXPECT_FALSE(Add->hasDebugInfo());
  EXPECT_EQ(Add->getDebugInfo(), nullptr);

  // 创建完整调试信息
  debug::IRInstructionDebugInfo DI;
  SourceLocation SL;
  SL.Filename = "test.cpp";
  SL.Line = 42;
  SL.Column = 7;
  DI.setLocation(SL);
  DI.setArtificial(true);

  Add->setDebugInfo(DI);
  EXPECT_TRUE(Add->hasDebugInfo());
  EXPECT_NE(Add->getDebugInfo(), nullptr);
  EXPECT_TRUE(Add->getDebugInfo()->hasLocation());
  EXPECT_EQ(Add->getDebugInfo()->getLocation().Line, 42u);
  EXPECT_TRUE(Add->getDebugInfo()->isArtificial());

  // 清除调试信息
  Add->clearDebugInfo();
  EXPECT_FALSE(Add->hasDebugInfo());
  EXPECT_EQ(Add->getDebugInfo(), nullptr);
}

// ============================================================
// V5: DebugMetadata classof 模式
// ============================================================

TEST(DebugInfoTest, DebugMetadataClassof) {
  DICompileUnit CU;
  DIType T;
  DISubprogram SP;
  DILocation Loc;

  // All inherit from DebugMetadata
  DebugMetadata* DM = &CU;
  EXPECT_EQ(DM->getDebugKind(), DebugMetadata::DebugKind::CompileUnit);
  EXPECT_TRUE(DICompileUnit::classof(DM));
  EXPECT_FALSE(DIType::classof(DM));

  DM = &T;
  EXPECT_EQ(DM->getDebugKind(), DebugMetadata::DebugKind::Type);
  EXPECT_TRUE(DIType::classof(DM));
  EXPECT_FALSE(DISubprogram::classof(DM));

  DM = &SP;
  EXPECT_EQ(DM->getDebugKind(), DebugMetadata::DebugKind::Subprogram);
  EXPECT_TRUE(DISubprogram::classof(DM));
  EXPECT_FALSE(DILocation::classof(DM));

  DM = &Loc;
  EXPECT_EQ(DM->getDebugKind(), DebugMetadata::DebugKind::Location);
  EXPECT_TRUE(DILocation::classof(DM));
  EXPECT_FALSE(DICompileUnit::classof(DM));
}

// ============================================================
// V6: 两套 DIType 共存不冲突
// ============================================================

TEST(DebugInfoTest, TwoDITypeCoexist) {
  // 基础版 DIType（类）
  ir::DIType BasicType("int", 32, 32);
  EXPECT_EQ(BasicType.getName(), "int");

  // 升级版 DIType（枚举结构体）
  debug::DIType::Kind K = debug::DIType::Kind::Basic;
  EXPECT_EQ(K, debug::DIType::Kind::Basic);

  // 两者可在同一编译单元中使用，不冲突
  debug::IRInstructionDebugInfo DI;
  DI.setTypeKind(debug::DIType::Kind::Pointer);
  EXPECT_EQ(DI.getTypeKind(), debug::DIType::Kind::Pointer);
}

// ============================================================
// DebugInfoEmitter 接口可编译（通过派生类验证）
// ============================================================

TEST(DebugInfoTest, DebugInfoEmitterInterface) {
  class TestEmitter : public debug::DebugInfoEmitter {
  public:
    void emitDebugInfo(const IRModule& M, raw_ostream& OS) override {}
    void emitDWARF5(const IRModule& M, raw_ostream& OS) override {}
    void emitDWARF4(const IRModule& M, raw_ostream& OS) override {}
    void emitCodeView(const IRModule& M, raw_ostream& OS) override {}
  };

  TestEmitter TE;
  // 接口可编译、可实例化
  SUCCEED();
}

// ============================================================
// 构造函数带源码位置
// ============================================================

TEST(DebugInfoTest, InstructionDebugInfoWithLocation) {
  SourceLocation SL;
  SL.Filename = " ctor.cpp";
  SL.Line = 10;
  SL.Column = 5;

  debug::IRInstructionDebugInfo DI(SL);
  EXPECT_TRUE(DI.hasLocation());
  EXPECT_EQ(DI.getLocation().Line, 10u);
  EXPECT_EQ(DI.getLocation().Filename, " ctor.cpp");
}
