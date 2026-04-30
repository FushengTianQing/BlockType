#include <gtest/gtest.h>

#include "blocktype/Backend/BackendOptions.h"
#include "blocktype/Backend/BackendBase.h"
#include "blocktype/Backend/BackendRegistry.h"
#include "blocktype/IR/BackendCapability.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;
using namespace blocktype::backend;
using namespace blocktype::ir;

// ---- Mock Backend for testing ----

class MockBackend : public BackendBase {
public:
  MockBackend(const BackendOptions& Opts, DiagnosticsEngine& Diags)
    : BackendBase(Opts, Diags) {}

  ir::StringRef getName() const override { return "mock"; }

  bool emitObject(ir::IRModule& IRModule, ir::StringRef OutputPath) override {
    return true;
  }
  bool emitAssembly(ir::IRModule& IRModule, ir::StringRef OutputPath) override {
    return true;
  }
  bool emitIRText(ir::IRModule& IRModule, ir::raw_ostream& OS) override {
    return true;
  }

  bool canHandle(ir::StringRef TargetTriple) const override {
    return true;
  }

  bool optimize(ir::IRModule& IRModule) override {
    return true;
  }

  ir::BackendCapability getCapability() const override {
    ir::BackendCapability Cap;
    Cap.declareFeature(IRFeature::IntegerArithmetic);
    Cap.declareFeature(IRFeature::FloatArithmetic);
    return Cap;
  }
};

// ---- V1: BackendRegistry 注册和创建 ----

TEST(BackendRegistryTest, RegisterAndCreate) {
  auto& Reg = BackendRegistry::instance();

  // 注册 mock 后端
  Reg.registerBackend("mock", [](const BackendOptions& Opts,
                                  DiagnosticsEngine& Diags) {
    return std::make_unique<MockBackend>(Opts, Diags);
  });

  BackendOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  auto BE = Reg.create("mock", Opts, Diags);
  ASSERT_NE(BE, nullptr);
  EXPECT_EQ(BE->getName(), "mock");
}

// ---- V2: BackendCapability 特性声明 ----

TEST(BackendBaseTest, CapabilityFeatureCheck) {
  ir::BackendCapability Cap;
  Cap.declareFeature(ir::IRFeature::IntegerArithmetic);
  Cap.declareFeature(ir::IRFeature::FloatArithmetic);

  EXPECT_TRUE(Cap.hasFeature(ir::IRFeature::IntegerArithmetic));
  EXPECT_TRUE(Cap.hasFeature(ir::IRFeature::FloatArithmetic));
  EXPECT_FALSE(Cap.hasFeature(ir::IRFeature::Coroutines));
}

// ---- V3: 不支持特性检测 ----

TEST(BackendBaseTest, UnsupportedFeatureDetection) {
  ir::BackendCapability Cap;
  Cap.declareFeature(ir::IRFeature::IntegerArithmetic);
  Cap.declareFeature(ir::IRFeature::FloatArithmetic);

  uint32_t Required = static_cast<uint32_t>(ir::IRFeature::IntegerArithmetic)
                    | static_cast<uint32_t>(ir::IRFeature::Coroutines);
  uint32_t Unsupported = Cap.getUnsupported(Required);
  EXPECT_NE((Unsupported & static_cast<uint32_t>(ir::IRFeature::Coroutines)), 0u);
}

// ---- V4: hasBackend 查询 ----

TEST(BackendRegistryTest, HasBackend) {
  auto& Reg = BackendRegistry::instance();
  EXPECT_TRUE(Reg.hasBackend("mock"));
  EXPECT_FALSE(Reg.hasBackend("nonexistent"));
}

// ---- BackendBase non-copyable ----

TEST(BackendBaseTest, NonCopyable) {
  // 验证编译期不可拷贝（通过 static_assert）
  static_assert(!std::is_copy_constructible_v<MockBackend>,
                "BackendBase must be non-copyable");
  static_assert(!std::is_copy_assignable_v<MockBackend>,
                "BackendBase must be non-copy-assignable");
}

// ---- BackendOptions defaults ----

TEST(BackendOptionsTest, Defaults) {
  BackendOptions Opts;
  EXPECT_EQ(Opts.OutputFormat, "elf");
  EXPECT_EQ(Opts.OptimizationLevel, 0u);
  EXPECT_FALSE(Opts.EmitAssembly);
  EXPECT_FALSE(Opts.EmitIR);
  EXPECT_FALSE(Opts.EmitIRBitcode);
  EXPECT_FALSE(Opts.DebugInfo);
  EXPECT_FALSE(Opts.DebugInfoForProfiling);
  EXPECT_EQ(Opts.DebugInfoFormat, "dwarf5");
}

// ---- BackendRegistry create nonexistent ----

TEST(BackendRegistryTest, CreateNonexistent) {
  auto& Reg = BackendRegistry::instance();
  BackendOptions Opts;
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  auto BE = Reg.create("nonexistent_backend", Opts, Diags);
  EXPECT_EQ(BE, nullptr);
}

// ---- BackendRegistry getRegisteredNames ----

TEST(BackendRegistryTest, GetRegisteredNames) {
  auto& Reg = BackendRegistry::instance();
  auto Names = Reg.getRegisteredNames();
  EXPECT_FALSE(Names.empty());
  // mock 后端应该已注册
  bool found = false;
  for (const auto& N : Names) {
    if (N == "mock") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

// ---- BackendBase getOptions/getDiagnostics ----

TEST(BackendBaseTest, Accessors) {
  BackendOptions Opts;
  Opts.TargetTriple = "aarch64-unknown-linux-gnu";
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  MockBackend BE(Opts, Diags);
  EXPECT_EQ(BE.getOptions().TargetTriple, "aarch64-unknown-linux-gnu");
  EXPECT_EQ(&BE.getDiagnostics(), &Diags);
}
