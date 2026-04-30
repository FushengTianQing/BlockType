//===--- CompilerInvocationTest.cpp - CompilerInvocation tests -*- C++ -*-===//

#include <string>

#include <gtest/gtest.h>

#include "blocktype/Frontend/CompilerInvocation.h"

using namespace blocktype;

// ============================================================
// T1: Default values
// ============================================================

TEST(CompilerInvocationPipelineTest, DefaultValues) {
  CompilerInvocation CI;
  EXPECT_EQ(CI.getFrontendName(), "cpp");
  EXPECT_EQ(CI.getBackendName(), "llvm");
  EXPECT_FALSE(CI.isFrontendExplicitlySet());
  EXPECT_FALSE(CI.isBackendExplicitlySet());
}

// ============================================================
// T2: Explicit set via setters
// ============================================================

TEST(CompilerInvocationPipelineTest, SetViaSetters) {
  CompilerInvocation CI;
  CI.setFrontendName("bt");
  CI.setBackendName("cranelift");
  EXPECT_EQ(CI.getFrontendName(), "bt");
  EXPECT_EQ(CI.getBackendName(), "cranelift");
  EXPECT_TRUE(CI.isFrontendExplicitlySet());
  EXPECT_TRUE(CI.isBackendExplicitlySet());
}

// ============================================================
// T3: Command-line parsing --frontend/--backend
// ============================================================

TEST(CompilerInvocationPipelineTest, ParseCommandLine) {
  CompilerInvocation CI;
  const char* Args[] = {"blocktype", "--frontend=bt", "--backend=cranelift", "test.cpp"};
  EXPECT_TRUE(CI.parseCommandLine(4, Args));
  EXPECT_EQ(CI.getFrontendName(), "bt");
  EXPECT_EQ(CI.getBackendName(), "cranelift");
  EXPECT_TRUE(CI.isFrontendExplicitlySet());
  EXPECT_TRUE(CI.isBackendExplicitlySet());
  EXPECT_EQ(CI.FrontendOpts.InputFiles.size(), 1u);
  EXPECT_EQ(CI.FrontendOpts.InputFiles[0], "test.cpp");
}

// ============================================================
// T4: Command-line parsing without --frontend/--backend
// ============================================================

TEST(CompilerInvocationPipelineTest, ParseCommandLineDefaults) {
  CompilerInvocation CI;
  const char* Args[] = {"blocktype", "test.cpp"};
  EXPECT_TRUE(CI.parseCommandLine(2, Args));
  EXPECT_EQ(CI.getFrontendName(), "cpp");
  EXPECT_EQ(CI.getBackendName(), "llvm");
  EXPECT_FALSE(CI.isFrontendExplicitlySet());
  EXPECT_FALSE(CI.isBackendExplicitlySet());
}

// ============================================================
// T5: toString includes pipeline options
// ============================================================

TEST(CompilerInvocationPipelineTest, ToStringIncludesPipeline) {
  CompilerInvocation CI;
  CI.setFrontendName("bt");
  CI.setBackendName("cranelift");
  std::string S = CI.toString();
  EXPECT_NE(S.find("Frontend Name: bt"), std::string::npos);
  EXPECT_NE(S.find("Backend Name: cranelift"), std::string::npos);
  EXPECT_NE(S.find("Frontend Explicitly Set: yes"), std::string::npos);
  EXPECT_NE(S.find("Backend Explicitly Set: yes"), std::string::npos);
}

// ============================================================
// T6: Validate rejects empty names
// ============================================================

TEST(CompilerInvocationPipelineTest, ValidateRejectsEmptyFrontend) {
  CompilerInvocation CI;
  CI.FrontendName = "";
  EXPECT_FALSE(CI.validate());
}

TEST(CompilerInvocationPipelineTest, ValidateRejectsEmptyBackend) {
  CompilerInvocation CI;
  CI.BackendName = "";
  EXPECT_FALSE(CI.validate());
}

// ============================================================
// T7: --use-new-pipeline flag
// ============================================================

TEST(CompilerInvocationPipelineTest, UseNewPipelineFlag) {
  CompilerInvocation CI;
  const char* Args[] = {"blocktype", "--use-new-pipeline", "test.cpp"};
  EXPECT_TRUE(CI.parseCommandLine(3, Args));
  EXPECT_TRUE(CI.isFrontendExplicitlySet());
  EXPECT_TRUE(CI.isBackendExplicitlySet());
  // Names keep defaults
  EXPECT_EQ(CI.getFrontendName(), "cpp");
  EXPECT_EQ(CI.getBackendName(), "llvm");
}

// ============================================================
// T8: toString -> fromString round-trip for Pipeline Options
// ============================================================

TEST(CompilerInvocationPipelineTest, FromStringRoundTrip) {
  // Set up a CompilerInvocation with known Pipeline Options
  CompilerInvocation Original;
  Original.setFrontendName("bt");
  Original.setBackendName("cranelift");

  // Serialize
  std::string Serialized = Original.toString();

  // Deserialize into a fresh instance
  CompilerInvocation Restored;
  EXPECT_TRUE(Restored.fromString(Serialized));

  // Verify Pipeline Options round-trip correctly
  EXPECT_EQ(Restored.getFrontendName(), "bt");
  EXPECT_EQ(Restored.getBackendName(), "cranelift");
  EXPECT_TRUE(Restored.isFrontendExplicitlySet());
  EXPECT_TRUE(Restored.isBackendExplicitlySet());
}

TEST(CompilerInvocationPipelineTest, FromStringRoundTripDefaults) {
  // Round-trip with default values
  CompilerInvocation Original;
  std::string Serialized = Original.toString();

  CompilerInvocation Restored;
  EXPECT_TRUE(Restored.fromString(Serialized));

  EXPECT_EQ(Restored.getFrontendName(), "cpp");
  EXPECT_EQ(Restored.getBackendName(), "llvm");
  EXPECT_FALSE(Restored.isFrontendExplicitlySet());
  EXPECT_FALSE(Restored.isBackendExplicitlySet());
}

// ============================================================
// T7: --freproducible-build option parsing
// ============================================================

TEST(CompilerInvocationPipelineTest, ReproducibleBuildOption) {
  CompilerInvocation CI;
  const char* Args[] = {"blocktype", "--freproducible-build", "test.cpp"};
  EXPECT_TRUE(CI.parseCommandLine(3, Args));
  EXPECT_TRUE(CI.ReproducibleBuild);
}

TEST(CompilerInvocationPipelineTest, ReproducibleBuildDefaultFalse) {
  CompilerInvocation CI;
  const char* Args[] = {"blocktype", "test.cpp"};
  EXPECT_TRUE(CI.parseCommandLine(2, Args));
  EXPECT_FALSE(CI.ReproducibleBuild);
}

// ============================================================
// T8: --fir-integrity-check option parsing
// ============================================================

TEST(CompilerInvocationPipelineTest, IRIntegrityCheckOption) {
  CompilerInvocation CI;
  const char* Args[] = {"blocktype", "--fir-integrity-check", "test.cpp"};
  EXPECT_TRUE(CI.parseCommandLine(3, Args));
  EXPECT_TRUE(CI.IRIntegrityCheck);
}

TEST(CompilerInvocationPipelineTest, IRIntegrityCheckDefaultFalse) {
  CompilerInvocation CI;
  const char* Args[] = {"blocktype", "test.cpp"};
  EXPECT_TRUE(CI.parseCommandLine(2, Args));
  EXPECT_FALSE(CI.IRIntegrityCheck);
}
