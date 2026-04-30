#include <gtest/gtest.h>

#include "blocktype/Backend/BackendOptions.h"
#include "blocktype/Backend/BackendBase.h"
#include "blocktype/Backend/BackendRegistry.h"
#include "blocktype/Backend/LLVMBackend.h"
#include "blocktype/Backend/IRToLLVMConverter.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/IRDebugInfo.h"
#include "blocktype/IR/IRDebugMetadata.h"
#include "blocktype/IR/BackendCapability.h"
#include "blocktype/Basic/Diagnostics.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace blocktype;
using namespace blocktype::backend;
using namespace blocktype::ir;

// V1: BackendRegistry 注册+创建+端到端 IR 文本输出
TEST(BackendIntegrationTest, RegistryCreateAndEmitIRText) {
  auto& Reg = BackendRegistry::instance();
  Reg.registerBackend("llvm", createLLVMBackend);

  BackendOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  auto BE = Reg.create("llvm", Opts, Diags);
  ASSERT_NE(BE, nullptr);
  EXPECT_EQ(BE->getName(), "llvm");

  IRTypeContext IRCtx;
  IRModule IRMod("integration_test", IRCtx, "x86_64-unknown-linux-gnu");
  auto* FnTy = IRCtx.getFunctionType(IRCtx.getVoidType(), {});
  auto* Fn = IRMod.getOrInsertFunction("test_func", FnTy);
  auto* EntryBB = Fn->addBasicBlock("entry");
  auto RetVoid = std::make_unique<IRInstruction>(
    Opcode::Ret, IRCtx.getVoidType(), 0);
  EntryBB->push_back(std::move(RetVoid));

  std::string Buf;
  ir::raw_string_ostream OS(Buf);
  bool ok = BE->emitIRText(IRMod, OS);
  EXPECT_TRUE(ok);
  EXPECT_NE(Buf.find("test_func"), std::string::npos);
}

// V2: autoSelect 测试
TEST(BackendIntegrationTest, AutoSelect) {
  auto& Reg = BackendRegistry::instance();
  Reg.registerBackend("llvm", createLLVMBackend);

  IRTypeContext IRCtx;
  IRModule IRMod("auto_test", IRCtx, "x86_64-unknown-linux-gnu");

  BackendOptions Opts;
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  auto BE = Reg.autoSelect(IRMod, Opts, Diags);
  ASSERT_NE(BE, nullptr);
  EXPECT_EQ(BE->getName(), "llvm");
}

// V3: 调试信息集成测试
TEST(BackendIntegrationTest, DebugInfoIntegration) {
  BackendOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";
  Opts.DebugInfo = true;
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  IRTypeContext IRCtx;
  llvm::LLVMContext LLVMCtx;

  IRModule IRMod("dbg_test", IRCtx, "x86_64-unknown-linux-gnu");
  auto* FnTy = IRCtx.getFunctionType(IRCtx.getVoidType(), {});
  auto* Fn = IRMod.getOrInsertFunction("dbg_func", FnTy);
  auto* EntryBB = Fn->addBasicBlock("entry");
  auto RetVoid = std::make_unique<IRInstruction>(
    Opcode::Ret, IRCtx.getVoidType(), 0);
  // 设置调试信息
  debug::IRInstructionDebugInfo DbgInfo;
  DbgInfo.setLocation(ir::SourceLocation{"test.bt", 10, 5});
  RetVoid->setDebugInfo(DbgInfo);
  EntryBB->push_back(std::move(RetVoid));

  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts, Diags);
  auto LLVMMod = Converter.convert(IRMod);
  ASSERT_NE(LLVMMod, nullptr);
  // 验证有 DICompileUnit
  EXPECT_NE(LLVMMod->debug_compile_units_begin(),
            LLVMMod->debug_compile_units_end());
}

// V4: 无调试信息不生成调试段
TEST(BackendIntegrationTest, NoDebugInfoNoCompileUnit) {
  BackendOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";
  Opts.DebugInfo = true; // 即使开启了 DebugInfo，无 IR 调试信息也不生成
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  IRTypeContext IRCtx;
  llvm::LLVMContext LLVMCtx;

  IRModule IRMod("no_dbg_test", IRCtx, "x86_64-unknown-linux-gnu");
  auto* FnTy = IRCtx.getFunctionType(IRCtx.getVoidType(), {});
  auto* Fn = IRMod.getOrInsertFunction("no_dbg_func", FnTy);
  auto* EntryBB = Fn->addBasicBlock("entry");
  auto RetVoid = std::make_unique<IRInstruction>(
    Opcode::Ret, IRCtx.getVoidType(), 0);
  EntryBB->push_back(std::move(RetVoid));

  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts, Diags);
  auto LLVMMod = Converter.convert(IRMod);
  ASSERT_NE(LLVMMod, nullptr);
  // 无 DICompileUnit
  EXPECT_EQ(LLVMMod->debug_compile_units_begin(),
            LLVMMod->debug_compile_units_end());
}

// V5: 多平台目标
TEST(BackendIntegrationTest, MultiPlatformTarget) {
  auto& Reg = BackendRegistry::instance();
  Reg.registerBackend("llvm", createLLVMBackend);

  llvm::raw_null_ostream NullOS;

  BackendOptions Opts1;
  Opts1.TargetTriple = "x86_64-unknown-linux-gnu";
  DiagnosticsEngine Diags1(NullOS);
  auto BE1 = Reg.create("llvm", Opts1, Diags1);
  ASSERT_NE(BE1, nullptr);

  BackendOptions Opts2;
  Opts2.TargetTriple = "aarch64-unknown-linux-gnu";
  DiagnosticsEngine Diags2(NullOS);
  auto BE2 = Reg.create("llvm", Opts2, Diags2);
  ASSERT_NE(BE2, nullptr);
}
