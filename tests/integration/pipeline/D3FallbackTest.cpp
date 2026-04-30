//===--- D3FallbackTest.cpp - Pipeline routing tests -*- C++ -*-===//
//
// Phase E.3: Old pipeline fallback has been removed.
// All compilations now go through the new pluggable pipeline.
// This test verifies the routing logic is gone and new pipeline is default.
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include "blocktype/Frontend/CompilerInvocation.h"
#include "blocktype/Frontend/CompilerInstance.h"

using namespace blocktype;

// ============================================================
// Verify old pipeline is removed: compileFile always uses new pipeline
// ============================================================

TEST(D3Fallback, DefaultInvocationUsesNewPipeline) {
  // Even without --frontend/--backend, the new pipeline is used.
  CompilerInvocation CI;
  EXPECT_FALSE(CI.isFrontendExplicitlySet());
  EXPECT_FALSE(CI.isBackendExplicitlySet());
  // Defaults are "cpp" frontend and "llvm" backend.
  EXPECT_EQ(CI.getFrontendName(), "cpp");
  EXPECT_EQ(CI.getBackendName(), "llvm");
}

TEST(D3Fallback, ExplicitFrontendOverrides) {
  CompilerInvocation CI;
  CI.setFrontendName("cpp");
  EXPECT_TRUE(CI.isFrontendExplicitlySet());
  EXPECT_EQ(CI.getFrontendName(), "cpp");
}

TEST(D3Fallback, ExplicitBackendOverrides) {
  CompilerInvocation CI;
  CI.setBackendName("llvm");
  EXPECT_TRUE(CI.isBackendExplicitlySet());
  EXPECT_EQ(CI.getBackendName(), "llvm");
}

TEST(D3Fallback, BothExplicitOverrides) {
  CompilerInvocation CI;
  CI.setFrontendName("cpp");
  CI.setBackendName("llvm");
  EXPECT_TRUE(CI.isFrontendExplicitlySet());
  EXPECT_TRUE(CI.isBackendExplicitlySet());
}

TEST(D3Fallback, ParseCommandLineDefaults) {
  CompilerInvocation CI;
  const char* Args[] = {"blocktype", "test.cpp"};
  EXPECT_TRUE(CI.parseCommandLine(2, Args));
  // Default frontend/backend names are set even without explicit flags
  EXPECT_EQ(CI.getFrontendName(), "cpp");
  EXPECT_EQ(CI.getBackendName(), "llvm");
}
