#include <gtest/gtest.h>

#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRFFI.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"

using namespace blocktype;
using namespace blocktype::ir;
using ffi::FFITypeMapper;
using ffi::FFIFunctionDecl;
using ffi::FFIModule;

// ============================================================================
// V1: C 语言类型映射 — 基本整数类型
// ============================================================================

TEST(IRFFITest, MapCTypeInt) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "int", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 32u);
}

TEST(IRFFITest, MapCTypeVoid) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "void", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isVoid());
}

TEST(IRFFITest, MapCTypeChar) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "char", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 8u);
}

TEST(IRFFITest, MapCTypeShort) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "short", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 16u);
}

TEST(IRFFITest, MapCTypeLongLong) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "long long", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 64u);
}

TEST(IRFFITest, MapCTypeBool) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "_Bool", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 8u);
}

TEST(IRFFITest, MapCTypeBoolAlt) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "bool", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isInteger());
  EXPECT_EQ(static_cast<IRIntegerType*>(Ty)->getBitWidth(), 8u);
}

// ============================================================================
// V1b: 浮点类型
// ============================================================================

TEST(IRFFITest, MapCTypeFloat) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "float", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isFloat());
  EXPECT_EQ(static_cast<IRFloatType*>(Ty)->getBitWidth(), 32u);
}

TEST(IRFFITest, MapCTypeDouble) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "double", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isFloat());
  EXPECT_EQ(static_cast<IRFloatType*>(Ty)->getBitWidth(), 64u);
}

TEST(IRFFITest, MapCTypeLongDouble) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "long double", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isFloat());
}

// ============================================================================
// V1c: 指针类型
// ============================================================================

TEST(IRFFITest, MapCTypeVoidPointer) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "void*", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isPointer());
}

TEST(IRFFITest, MapCTypeCharPointer) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "char*", TCtx);
  ASSERT_NE(Ty, nullptr);
  EXPECT_TRUE(Ty->isPointer());
}

// ============================================================================
// V1d: 未知类型返回 nullptr
// ============================================================================

TEST(IRFFITest, MapCTypeUnknown) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("C", "unknown_type_xyz", TCtx);
  EXPECT_EQ(Ty, nullptr);
}

// ============================================================================
// 反向映射
// ============================================================================

TEST(IRFFITest, MapToExternalTypeInt) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto Result = FFITypeMapper::mapToExternalType(TCtx.getInt32Ty(), "C");
  EXPECT_EQ(Result, "int");
}

TEST(IRFFITest, MapToExternalTypeFloat) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto Result = FFITypeMapper::mapToExternalType(TCtx.getFloatTy(), "C");
  EXPECT_EQ(Result, "float");
}

TEST(IRFFITest, MapToExternalTypeDouble) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto Result = FFITypeMapper::mapToExternalType(TCtx.getDoubleTy(), "C");
  EXPECT_EQ(Result, "double");
}

TEST(IRFFITest, MapToExternalTypeVoid) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto Result = FFITypeMapper::mapToExternalType(TCtx.getVoidType(), "C");
  EXPECT_EQ(Result, "void");
}

TEST(IRFFITest, MapToExternalTypePointer) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* PtrTy = TCtx.getPointerType(TCtx.getInt8Ty());
  auto Result = FFITypeMapper::mapToExternalType(PtrTy, "C");
  EXPECT_EQ(Result, "void*");
}

// ============================================================================
// V2: FFIFunctionDecl 创建和 getter
// ============================================================================

TEST(IRFFITest, FFIFunctionDeclBasic) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getPointerType(TCtx.getInt8Ty())}, true);
  FFIFunctionDecl FDecl("printf", "_printf", ffi::CallingConvention::C,
                         FTy, "C", "stdio.h", true);

  EXPECT_EQ(FDecl.getName(), "printf");
  EXPECT_EQ(FDecl.getMangledName(), "_printf");
  EXPECT_EQ(FDecl.getCallingConvention(), ffi::CallingConvention::C);
  EXPECT_EQ(FDecl.getSignature(), FTy);
  EXPECT_EQ(FDecl.getSourceLanguage(), "C");
  EXPECT_EQ(FDecl.getHeaderFile(), "stdio.h");
  EXPECT_TRUE(FDecl.isVariadic());
}

// ============================================================================
// FFIFunctionDecl::createIRDeclaration
// ============================================================================

TEST(IRFFITest, CreateIRDeclaration) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {TCtx.getPointerType(TCtx.getInt8Ty())}, true);
  FFIFunctionDecl FDecl("printf", "_printf", ffi::CallingConvention::C,
                         FTy, "C", "stdio.h", true);

  IRModule M("ffi_test", TCtx, "x86_64-unknown-linux-gnu");
  auto* IRFunc = FDecl.createIRDeclaration(M);

  ASSERT_NE(IRFunc, nullptr);
  EXPECT_EQ(IRFunc->getName(), "_printf");
  EXPECT_TRUE(IRFunc->isDeclaration());
}

// ============================================================================
// FFIModule
// ============================================================================

TEST(IRFFITest, FFIModuleAddAndLookup) {
  FFIModule Mod;

  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {});

  auto Decl = std::make_unique<FFIFunctionDecl>("abs", "_abs", ffi::CallingConvention::C,
                                                  FTy, "C", "stdlib.h");
  Mod.addDeclaration(std::move(Decl));

  EXPECT_EQ(Mod.getNumDeclarations(), 1u);

  auto* Found = Mod.lookup("abs");
  ASSERT_NE(Found, nullptr);
  EXPECT_EQ(Found->getName(), "abs");
  EXPECT_EQ(Found->getMangledName(), "_abs");

  // 查找不存在的声明
  EXPECT_EQ(Mod.lookup("nonexistent"), nullptr);
}

TEST(IRFFITest, FFIModuleMultipleDeclarations) {
  FFIModule Mod;

  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();
  auto* FTy = TCtx.getFunctionType(TCtx.getInt32Ty(), {});

  Mod.addDeclaration(std::make_unique<FFIFunctionDecl>("abs", "_abs",
                    ffi::CallingConvention::C, FTy, "C", "stdlib.h"));
  Mod.addDeclaration(std::make_unique<FFIFunctionDecl>("strlen", "strlen",
                    ffi::CallingConvention::C, FTy, "C", "string.h"));

  EXPECT_EQ(Mod.getNumDeclarations(), 2u);
  EXPECT_NE(Mod.lookup("abs"), nullptr);
  EXPECT_NE(Mod.lookup("strlen"), nullptr);
}

// ============================================================================
// getSupportedLanguages
// ============================================================================

TEST(IRFFITest, GetSupportedLanguages) {
  auto Languages = FFITypeMapper::getSupportedLanguages();
  EXPECT_GE(Languages.size(), 6u);

  // 验证包含 "C"
  bool HasC = false;
  for (auto& L : Languages) {
    if (L == "C") HasC = true;
  }
  EXPECT_TRUE(HasC);
}

// ============================================================================
// 其他语言通过 C ABI 映射
// ============================================================================

TEST(IRFFITest, MapRustTypeThroughCABI) {
  IRContext Ctx;
  IRTypeContext& TCtx = Ctx.getTypeContext();

  auto* Ty = FFITypeMapper::mapExternalType("Rust", "i32", TCtx);
  // Rust 通过 C ABI 映射，"i32" 不在 C 映射表中，返回 nullptr
  EXPECT_EQ(Ty, nullptr);

  // 但 "int" 可以通过 C ABI 映射
  auto* Ty2 = FFITypeMapper::mapExternalType("Rust", "int", TCtx);
  ASSERT_NE(Ty2, nullptr);
  EXPECT_TRUE(Ty2->isInteger());
}
