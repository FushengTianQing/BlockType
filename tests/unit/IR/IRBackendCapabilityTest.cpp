#include <gtest/gtest.h>

#include "blocktype/IR/BackendCapability.h"

using namespace blocktype;
using namespace blocktype::ir;

// V1: BackendCapability feature check
TEST(IRBackendCapabilityTest, FeatureCheck) {
  BackendCapability Cap;
  Cap.declareFeature(IRFeature::IntegerArithmetic);
  Cap.declareFeature(IRFeature::FloatArithmetic);

  EXPECT_TRUE(Cap.hasFeature(IRFeature::IntegerArithmetic));
  EXPECT_TRUE(Cap.hasFeature(IRFeature::FloatArithmetic));
  EXPECT_FALSE(Cap.hasFeature(IRFeature::ExceptionHandling));
  EXPECT_FALSE(Cap.hasFeature(IRFeature::VectorOperations));
}

// V2: supportsAll batch check
TEST(IRBackendCapabilityTest, SupportsAll) {
  BackendCapability Cap;
  Cap.declareFeature(IRFeature::IntegerArithmetic);
  Cap.declareFeature(IRFeature::FloatArithmetic);

  uint32_t Required = static_cast<uint32_t>(IRFeature::IntegerArithmetic)
                    | static_cast<uint32_t>(IRFeature::FloatArithmetic);
  EXPECT_TRUE(Cap.supportsAll(Required));

  uint32_t RequiredAll = 0xFFFu;
  EXPECT_FALSE(Cap.supportsAll(RequiredAll));
  EXPECT_EQ(Cap.getUnsupported(RequiredAll),
            RequiredAll & ~Cap.getSupportedMask());
}

// V3: Predefined backend capabilities
TEST(IRBackendCapabilityTest, PredefinedCaps) {
  auto LLVM = BackendCaps::LLVM();
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::IntegerArithmetic));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::FloatArithmetic));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::VectorOperations));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::AtomicOperations));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::ExceptionHandling));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::DebugInfo));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::VarArg));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::SeparateFloatInt));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::StructReturn));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::DynamicCast));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::VirtualDispatch));
  EXPECT_TRUE(LLVM.hasFeature(IRFeature::Coroutines));
  EXPECT_EQ(LLVM.getSupportedMask(), 0xFFFu);

  auto Cran = BackendCaps::Cranelift();
  EXPECT_TRUE(Cran.hasFeature(IRFeature::IntegerArithmetic));
  EXPECT_TRUE(Cran.hasFeature(IRFeature::FloatArithmetic));
  EXPECT_TRUE(Cran.hasFeature(IRFeature::VectorOperations));
  EXPECT_FALSE(Cran.hasFeature(IRFeature::AtomicOperations));
  EXPECT_FALSE(Cran.hasFeature(IRFeature::ExceptionHandling));
  EXPECT_FALSE(Cran.hasFeature(IRFeature::DebugInfo));
}
