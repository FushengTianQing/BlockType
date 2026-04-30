#include <gtest/gtest.h>

#include "blocktype/Backend/BackendOptions.h"
#include "blocktype/Backend/BackendBase.h"
#include "blocktype/Backend/BackendRegistry.h"
#include "blocktype/Backend/LLVMBackend.h"
#include "blocktype/Backend/IRToLLVMConverter.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRBuilder.h"
#include "blocktype/IR/BackendCapability.h"
#include "blocktype/Basic/Diagnostics.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/raw_ostream.h"

using namespace blocktype;
using namespace blocktype::backend;
using namespace blocktype::ir;

// ============================================================
// C.2: IRToLLVMConverter 类型映射测试
// ============================================================

class IRToLLVMConverterTest : public ::testing::Test {
protected:
  IRTypeContext IRCtx;
  llvm::LLVMContext LLVMCtx;
  BackendOptions Opts;
};

// V1: 整数类型映射
TEST_F(IRToLLVMConverterTest, IntegerTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);

  auto* IRInt32 = IRCtx.getInt32Ty();
  auto* LLVMInt32 = Converter.mapType(IRInt32);
  ASSERT_NE(LLVMInt32, nullptr);
  EXPECT_EQ(LLVMInt32, llvm::Type::getInt32Ty(LLVMCtx));

  auto* IRInt1 = IRCtx.getInt1Ty();
  auto* LLVMInt1 = Converter.mapType(IRInt1);
  ASSERT_NE(LLVMInt1, nullptr);
  EXPECT_EQ(LLVMInt1, llvm::Type::getInt1Ty(LLVMCtx));

  auto* IRInt64 = IRCtx.getInt64Ty();
  auto* LLVMInt64 = Converter.mapType(IRInt64);
  ASSERT_NE(LLVMInt64, nullptr);
  EXPECT_EQ(LLVMInt64, llvm::Type::getInt64Ty(LLVMCtx));
}

// V1: 浮点类型映射
TEST_F(IRToLLVMConverterTest, FloatTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);

  auto* IRFloat32 = IRCtx.getFloatTy();
  auto* LLVMFloat32 = Converter.mapType(IRFloat32);
  ASSERT_NE(LLVMFloat32, nullptr);
  EXPECT_EQ(LLVMFloat32, llvm::Type::getFloatTy(LLVMCtx));

  auto* IRFloat64 = IRCtx.getDoubleTy();
  auto* LLVMFloat64 = Converter.mapType(IRFloat64);
  ASSERT_NE(LLVMFloat64, nullptr);
  EXPECT_EQ(LLVMFloat64, llvm::Type::getDoubleTy(LLVMCtx));
}

// V2: 指针类型映射
TEST_F(IRToLLVMConverterTest, PointerTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);

  auto* IRInt8 = IRCtx.getInt8Ty();
  auto* IRPtr = IRCtx.getPointerType(IRInt8);
  auto* LLVMPtr = Converter.mapType(IRPtr);
  ASSERT_NE(LLVMPtr, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::PointerType>(LLVMPtr));
}

// V3: 结构体映射（含递归）
TEST_F(IRToLLVMConverterTest, StructTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);

  auto* IRInt32 = IRCtx.getInt32Ty();
  auto* IRPtr = IRCtx.getPointerType(IRInt32);  // 用 int* 代替自引用
  auto* IRStruct = IRCtx.getStructType("TestStruct", {IRPtr, IRInt32});
  auto* LLVMStruct = Converter.mapType(IRStruct);
  ASSERT_NE(LLVMStruct, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::StructType>(LLVMStruct));
  auto* ST = llvm::cast<llvm::StructType>(LLVMStruct);
  EXPECT_FALSE(ST->isOpaque());
  EXPECT_EQ(ST->getNumElements(), 2u);
}

// Void 类型
TEST_F(IRToLLVMConverterTest, VoidTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);
  auto* Void = Converter.mapType(IRCtx.getVoidType());
  ASSERT_NE(Void, nullptr);
  EXPECT_EQ(Void, llvm::Type::getVoidTy(LLVMCtx));
}

// 数组类型
TEST_F(IRToLLVMConverterTest, ArrayTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);
  auto* IRInt32 = IRCtx.getInt32Ty();
  auto* IRArray = IRCtx.getArrayType(IRInt32, 10);
  auto* LLVMArray = Converter.mapType(IRArray);
  ASSERT_NE(LLVMArray, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::ArrayType>(LLVMArray));
  EXPECT_EQ(llvm::cast<llvm::ArrayType>(LLVMArray)->getNumElements(), 10u);
}

// 函数类型
TEST_F(IRToLLVMConverterTest, FunctionTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);
  auto* IRInt32 = IRCtx.getInt32Ty();
  auto* IRFnTy = IRCtx.getFunctionType(IRInt32, {IRInt32, IRInt32});
  auto* LLVMFnTy = Converter.mapType(IRFnTy);
  ASSERT_NE(LLVMFnTy, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::FunctionType>(LLVMFnTy));
}

// 向量类型
TEST_F(IRToLLVMConverterTest, VectorTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);
  auto* IRInt32 = IRCtx.getInt32Ty();
  auto* IRVec = IRCtx.getVectorType(IRInt32, 4);
  auto* LLVMVec = Converter.mapType(IRVec);
  ASSERT_NE(LLVMVec, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::FixedVectorType>(LLVMVec));
}

// Opaque 类型
TEST_F(IRToLLVMConverterTest, OpaqueTypeMapping) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);
  auto* IROpaque = IRCtx.getOpaqueType("opaque_type");
  auto* LLVMOpaque = Converter.mapType(IROpaque);
  ASSERT_NE(LLVMOpaque, nullptr);
  EXPECT_TRUE(llvm::isa<llvm::StructType>(LLVMOpaque));
}

// 类型缓存测试
TEST_F(IRToLLVMConverterTest, TypeCaching) {
  IRToLLVMConverter Converter(IRCtx, LLVMCtx, Opts);
  auto* IRInt32 = IRCtx.getInt32Ty();
  auto* LLVM1 = Converter.mapType(IRInt32);
  auto* LLVM2 = Converter.mapType(IRInt32);
  EXPECT_EQ(LLVM1, LLVM2);
}

// ============================================================
// C.3+C.4: 端到端转换测试
// ============================================================

// V4: convert 端到端
TEST_F(IRToLLVMConverterTest, ConvertEndToEnd) {
  IRTypeContext IRTypeCtx;
  IRModule IRMod("test_module", IRTypeCtx, "x86_64-unknown-linux-gnu");

  // 创建一个简单函数
  auto* FnTy = IRTypeCtx.getFunctionType(IRTypeCtx.getVoidType(), {});
  auto* Fn = IRMod.getOrInsertFunction("test_func", FnTy);
  auto* EntryBB = Fn->addBasicBlock("entry");

  // 创建 RetVoid 指令
  auto RetVoid = std::make_unique<IRInstruction>(
    Opcode::Ret, IRTypeCtx.getVoidType(), 0);
  EntryBB->push_back(std::move(RetVoid));

  IRToLLVMConverter Converter(IRTypeCtx, LLVMCtx, Opts);
  auto LLVMMod = Converter.convert(IRMod);
  ASSERT_NE(LLVMMod, nullptr);
  EXPECT_NE(LLVMMod->getFunction("test_func"), nullptr);
}

// 带参数和加法的函数
TEST_F(IRToLLVMConverterTest, ConvertAddFunction) {
  IRTypeContext IRTypeCtx;
  IRModule IRMod("add_module", IRTypeCtx, "x86_64-unknown-linux-gnu");

  auto* Int32Ty = IRTypeCtx.getInt32Ty();
  auto* FnTy = IRTypeCtx.getFunctionType(Int32Ty, {Int32Ty, Int32Ty});
  auto* Fn = IRMod.getOrInsertFunction("add_func", FnTy);

  auto* EntryBB = Fn->addBasicBlock("entry");

  // %sum = add i32 %a, %b
  auto* Arg0 = Fn->getArg(0);
  auto* Arg1 = Fn->getArg(1);
  auto AddInst = std::make_unique<IRInstruction>(
    Opcode::Add, Int32Ty, 1, dialect::DialectID::Core, "sum");
  AddInst->addOperand(Arg0);
  AddInst->addOperand(Arg1);
  EntryBB->push_back(std::move(AddInst));

  // ret i32 %sum
  auto RetInst = std::make_unique<IRInstruction>(
    Opcode::Ret, Int32Ty, 2);
  auto* SumRef = Fn->getEntryBlock()->getFirstInsertionPt();
  RetInst->addOperand(SumRef);
  EntryBB->push_back(std::move(RetInst));

  IRToLLVMConverter Converter(IRTypeCtx, LLVMCtx, Opts);
  auto LLVMMod = Converter.convert(IRMod);
  ASSERT_NE(LLVMMod, nullptr);
  auto* F = LLVMMod->getFunction("add_func");
  ASSERT_NE(F, nullptr);
  EXPECT_FALSE(F->empty());
  EXPECT_EQ(F->arg_size(), 2u);
}

// 带全局变量的模块
TEST_F(IRToLLVMConverterTest, ConvertGlobalVariable) {
  IRTypeContext IRTypeCtx;
  IRModule IRMod("global_module", IRTypeCtx);

  auto* Int32Ty = IRTypeCtx.getInt32Ty();
  auto* GV = IRMod.getOrInsertGlobal("g1", Int32Ty);

  IRToLLVMConverter Converter(IRTypeCtx, LLVMCtx, Opts);
  auto LLVMMod = Converter.convert(IRMod);
  ASSERT_NE(LLVMMod, nullptr);
  EXPECT_NE(LLVMMod->getGlobalVariable("g1"), nullptr);
}

// ============================================================
// C.5: LLVMBackend 测试
// ============================================================

class LLVMBackendTest : public ::testing::Test {
protected:
  BackendOptions Opts;
};

// V2: 能力检查
TEST_F(LLVMBackendTest, Capability) {
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);
  auto BE = createLLVMBackend(Opts, Diags);
  ASSERT_NE(BE, nullptr);

  auto Cap = BE->getCapability();
  EXPECT_TRUE(Cap.hasFeature(IRFeature::IntegerArithmetic));
  EXPECT_TRUE(Cap.hasFeature(IRFeature::FloatArithmetic));
  EXPECT_TRUE(Cap.hasFeature(IRFeature::DebugInfo));
  EXPECT_FALSE(Cap.hasFeature(IRFeature::Coroutines));
}

// V3: getName
TEST_F(LLVMBackendTest, GetName) {
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);
  auto BE = createLLVMBackend(Opts, Diags);
  ASSERT_NE(BE, nullptr);
  EXPECT_EQ(BE->getName(), "llvm");
}

// V4: canHandle
TEST_F(LLVMBackendTest, CanHandle) {
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);
  auto BE = createLLVMBackend(Opts, Diags);
  ASSERT_NE(BE, nullptr);
  EXPECT_TRUE(BE->canHandle("x86_64-unknown-linux-gnu"));
  EXPECT_TRUE(BE->canHandle("aarch64-unknown-linux-gnu"));
  EXPECT_TRUE(BE->canHandle("x86_64-apple-darwin"));
}

// V5: optimize 默认返回 true
TEST_F(LLVMBackendTest, Optimize) {
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);
  auto BE = createLLVMBackend(Opts, Diags);

  IRTypeContext IRCtx;
  IRModule IRMod("opt_test", IRCtx);
  EXPECT_TRUE(BE->optimize(IRMod));
}

// emitIRText 测试
TEST_F(LLVMBackendTest, EmitIRText) {
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";
  auto BE = createLLVMBackend(Opts, Diags);

  IRTypeContext IRCtx;
  IRModule IRMod("ir_text_test", IRCtx, "x86_64-unknown-linux-gnu");
  auto* FnTy = IRCtx.getFunctionType(IRCtx.getVoidType(), {});
  IRMod.getOrInsertFunction("main", FnTy);

  std::string Buf;
  ir::raw_string_ostream OS(Buf);
  bool ok = BE->emitIRText(IRMod, OS);
  EXPECT_TRUE(ok);
  EXPECT_FALSE(Buf.empty());
  EXPECT_NE(Buf.find("main"), std::string::npos);
}

// ============================================================
// C.9: 集成测试（BackendRegistry + LLVMBackend）
// ============================================================

// V1: BackendRegistry 注册+创建
TEST(IntegrationTest, RegistryCreate) {
  auto& Reg = BackendRegistry::instance();
  Reg.registerBackend("llvm", createLLVMBackend);

  BackendOptions Opts;
  Opts.TargetTriple = "x86_64-unknown-linux-gnu";
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  auto BE = Reg.create("llvm", Opts, Diags);
  ASSERT_NE(BE, nullptr);
  EXPECT_EQ(BE->getName(), "llvm");
}

// V3: 多平台目标
TEST(IntegrationTest, MultiPlatformTarget) {
  auto& Reg = BackendRegistry::instance();

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

// autoSelect 测试
TEST(IntegrationTest, AutoSelect) {
  auto& Reg = BackendRegistry::instance();

  IRTypeContext IRCtx;
  IRModule IRMod("auto_test", IRCtx, "x86_64-unknown-linux-gnu");

  BackendOptions Opts;
  llvm::raw_null_ostream NullOS;
  DiagnosticsEngine Diags(NullOS);

  auto BE = Reg.autoSelect(IRMod, Opts, Diags);
  ASSERT_NE(BE, nullptr);
  EXPECT_EQ(BE->getName(), "llvm");
}
