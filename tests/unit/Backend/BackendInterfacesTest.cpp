//===--- BackendInterfacesTest.cpp - Backend interface tests -*- C++ -*-===//

#include <memory>

#include <gtest/gtest.h>

#include "blocktype/Backend/CodeEmitter.h"
#include "blocktype/Backend/FrameLowering.h"
#include "blocktype/Backend/TargetLowering.h"
#include "blocktype/Backend/RegisterAllocator.h"
#include "blocktype/IR/ADT.h"

using namespace blocktype;
using namespace blocktype::backend;

// ============================================================
// Mock implementations for interface compilation test
// ============================================================

class MockCodeEmitter : public CodeEmitter {
public:
  bool emit(const TargetFunction& F, const BackendOptions& Opts,
            ir::raw_ostream& OS) override {
    (void)F; (void)Opts; (void)OS;
    return true;
  }
  bool emitModule(const ir::SmallVector<TargetFunction, 16>& Functions,
                  const BackendOptions& Opts, ir::raw_ostream& OS) override {
    (void)Functions; (void)Opts; (void)OS;
    return true;
  }
};

class MockFrameLowering : public FrameLowering {
public:
  void lower(TargetFunction& F, const TargetRegisterInfo& TRI,
             const TargetABIInfo& ABI) override {
    (void)TRI; (void)ABI;
    F.getFrameInfo().StackSize = 64;
  }
  uint64_t getStackSize(const TargetFunction& F) const override {
    return F.getFrameInfo().StackSize;
  }
};

class MockTargetLowering : public TargetLowering {
public:
  bool lower(const ir::IRInstruction& I,
             TargetInstructionList& Output) override {
    (void)I; (void)Output;
    return true;
  }
  bool supportsDialect(ir::dialect::DialectID D) const override {
    return D == ir::dialect::DialectID::Core;
  }
};

// ============================================================
// T1: CodeEmitter interface compiles and works
// ============================================================

TEST(BackendInterfacesTest, CodeEmitterInterface) {
  MockCodeEmitter Emitter;
  TargetFunction F("test", nullptr);
  BackendOptions Opts;
  std::string Buf;
  ir::raw_string_ostream OS(Buf);
  EXPECT_TRUE(Emitter.emit(F, Opts, OS));
}

// ============================================================
// T2: FrameLowering interface compiles and works
// ============================================================

TEST(BackendInterfacesTest, FrameLoweringInterface) {
  MockFrameLowering FL;
  TargetFunction F("test", nullptr);
  TargetRegisterInfo TRI;
  TargetABIInfo ABI;
  FL.lower(F, TRI, ABI);
  EXPECT_EQ(FL.getStackSize(F), 64u);
}

// ============================================================
// T3: TargetLowering interface compiles and works
// ============================================================

TEST(BackendInterfacesTest, TargetLoweringInterface) {
  MockTargetLowering TL;
  EXPECT_TRUE(TL.supportsDialect(ir::dialect::DialectID::Core));
  EXPECT_FALSE(TL.supportsDialect(ir::dialect::DialectID::Cpp));
}

// ============================================================
// T4: TargetABIInfo defaults
// ============================================================

TEST(BackendInterfacesTest, TargetABIInfoDefaults) {
  TargetABIInfo ABI;
  EXPECT_TRUE(ABI.IsLittleEndian);
  EXPECT_EQ(ABI.PointerSize, 8u);
  EXPECT_EQ(ABI.StackAlignment, 16u);
  EXPECT_EQ(ABI.MaxVectorAlignment, 16u);
}
