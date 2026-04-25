//===--- IREmitExprTest.cpp - IREmitExpr Unit Tests -----------------------===//

#include <gtest/gtest.h>

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/Frontend/IREmitExpr.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRConversionResult.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"
#include "blocktype/IR/IRConstant.h"

using namespace blocktype;
using namespace blocktype::frontend;

namespace {

class IREmitExprTest : public ::testing::Test {
protected:
  ir::IRContext IRCtx;
  ir::IRTypeContext& TypeCtx;
  std::unique_ptr<ir::TargetLayout> Layout;
  DiagnosticsEngine Diags;

  IREmitExprTest()
    : TypeCtx(IRCtx.getTypeContext()),
      Layout(ir::TargetLayout::Create("x86_64-unknown-linux-gnu")),
      Diags() {}
};

} // anonymous namespace

// === Test 1: Integer literal emission ===
TEST_F(IREmitExprTest, EmitIntegerLiteral) {
  SourceLocation Loc;
  llvm::APInt Val(32, 42);
  IntegerLiteral IL(Loc, Val, QualType());

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  auto Result = Converter.convert(&TU);
  ASSERT_TRUE(Result.isUsable());

  IREmitExpr ExprEmitter(Converter);
  auto* Result2 = ExprEmitter.Emit(&IL);
  ASSERT_NE(Result2, nullptr);

  auto* CI = llvm::dyn_cast<ir::IRConstantInt>(Result2);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 42u);
}

// === Test 2: Boolean literal emission ===
TEST_F(IREmitExprTest, EmitBoolLiteral) {
  SourceLocation Loc;
  CXXBoolLiteral TrueLit(Loc, true, QualType());
  CXXBoolLiteral FalseLit(Loc, false, QualType());

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);
  auto* TrueResult = ExprEmitter.Emit(&TrueLit);
  ASSERT_NE(TrueResult, nullptr);

  auto* FalseResult = ExprEmitter.Emit(&FalseLit);
  ASSERT_NE(FalseResult, nullptr);
}

// === Test 3: Binary operator — integer addition ===
TEST_F(IREmitExprTest, EmitBinaryAdd) {
  SourceLocation Loc;
  QualType IntTy;
  llvm::APInt AVal(32, 10);
  llvm::APInt BVal(32, 20);
  IntegerLiteral A(Loc, AVal, IntTy);
  IntegerLiteral B(Loc, BVal, IntTy);
  BinaryOperator AddBO(Loc, &A, &B, BinaryOpKind::Add);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);
  auto* Result = ExprEmitter.EmitBinaryExpr(&AddBO);
  ASSERT_NE(Result, nullptr);
}

// === Test 4: Comparison operators ===
TEST_F(IREmitExprTest, EmitComparison) {
  SourceLocation Loc;
  QualType IntTy;
  llvm::APInt AVal(32, 5);
  llvm::APInt BVal(32, 10);
  IntegerLiteral A(Loc, AVal, IntTy);
  IntegerLiteral B(Loc, BVal, IntTy);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);

  BinaryOperator LTOp(Loc, &A, &B, BinaryOpKind::LT);
  auto* LTResult = ExprEmitter.EmitBinaryExpr(&LTOp);
  ASSERT_NE(LTResult, nullptr);

  BinaryOperator EQOp(Loc, &A, &B, BinaryOpKind::EQ);
  auto* EQResult = ExprEmitter.EmitBinaryExpr(&EQOp);
  ASSERT_NE(EQResult, nullptr);
}

// === Test 5: Unary negation ===
TEST_F(IREmitExprTest, EmitUnaryNeg) {
  SourceLocation Loc;
  QualType IntTy;
  llvm::APInt Val(32, 42);
  IntegerLiteral IL(Loc, Val, IntTy);
  UnaryOperator NegOp(Loc, &IL, UnaryOpKind::Minus);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);
  auto* Result = ExprEmitter.EmitUnaryExpr(&NegOp);
  ASSERT_NE(Result, nullptr);
}

// === Test 6: Character literal ===
TEST_F(IREmitExprTest, EmitCharacterLiteral) {
  SourceLocation Loc;
  CharacterLiteral CL(Loc, 'A', QualType());

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);
  auto* Result = ExprEmitter.EmitCharacterLiteral(&CL);
  ASSERT_NE(Result, nullptr);

  auto* CI = llvm::dyn_cast<ir::IRConstantInt>(Result);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 65u);
}

// === Test 7: Comma operator ===
TEST_F(IREmitExprTest, EmitCommaOperator) {
  SourceLocation Loc;
  QualType IntTy;
  llvm::APInt AVal(32, 1);
  llvm::APInt BVal(32, 2);
  IntegerLiteral A(Loc, AVal, IntTy);
  IntegerLiteral B(Loc, BVal, IntTy);
  BinaryOperator CommaOp(Loc, &A, &B, BinaryOpKind::Comma);

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);
  auto* Result = ExprEmitter.EmitBinaryExpr(&CommaOp);
  ASSERT_NE(Result, nullptr);
  auto* CI = llvm::dyn_cast<ir::IRConstantInt>(Result);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 2u);
}

// === Test 8: General Emit dispatch ===
TEST_F(IREmitExprTest, EmitDispatch) {
  SourceLocation Loc;
  llvm::APInt Val(32, 100);
  IntegerLiteral IL(Loc, Val, QualType());

  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU(Loc);
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);
  auto* Result = ExprEmitter.Emit(&IL);
  ASSERT_NE(Result, nullptr);

  auto* CI = llvm::dyn_cast<ir::IRConstantInt>(Result);
  ASSERT_NE(CI, nullptr);
  EXPECT_EQ(CI->getZExtValue(), 100u);
}

// === Test 9: Null expression → returns nullptr ===
TEST_F(IREmitExprTest, EmitNullExpr) {
  ASTToIRConverter Converter(IRCtx, TypeCtx, *Layout, Diags);
  TranslationUnitDecl TU{SourceLocation{}};
  Converter.convert(&TU);

  IREmitExpr ExprEmitter(Converter);
  auto* Result = ExprEmitter.Emit(nullptr);
  EXPECT_EQ(Result, nullptr);
}

// === Test 10: IRBuilder extended API ===
TEST_F(IREmitExprTest, IRBuilderExtendedAPI) {
  ir::IRContext C;
  ir::IRTypeContext& TC = C.getTypeContext();
  ir::IRBuilder Builder(C);

  auto* Int32Ty = TC.getInt32Ty();

  ir::IRModule Mod("test", TC);
  ir::IRFunctionType* FnTy = TC.getFunctionType(Int32Ty, {});
  ir::IRFunction* Fn = Mod.getOrInsertFunction("test_fn", FnTy);
  ir::IRBasicBlock* BB = Fn->addBasicBlock("entry");
  Builder.setInsertPoint(BB);

  auto* V1 = Builder.getInt32(10);
  auto* V2 = Builder.getInt32(3);

  auto* SDivResult = Builder.createSDiv(V1, V2);
  ASSERT_NE(SDivResult, nullptr);

  auto* UDivResult = Builder.createUDiv(V1, V2);
  ASSERT_NE(UDivResult, nullptr);

  auto* SRemResult = Builder.createSRem(V1, V2);
  ASSERT_NE(SRemResult, nullptr);

  auto* AndResult = Builder.createAnd(V1, V2);
  ASSERT_NE(AndResult, nullptr);

  auto* OrResult = Builder.createOr(V1, V2);
  ASSERT_NE(OrResult, nullptr);

  auto* XorResult = Builder.createXor(V1, V2);
  ASSERT_NE(XorResult, nullptr);

  auto* ShlResult = Builder.createShl(V1, V2);
  ASSERT_NE(ShlResult, nullptr);

  auto* AShrResult = Builder.createAShr(V1, V2);
  ASSERT_NE(AShrResult, nullptr);

  auto* LShrResult = Builder.createLShr(V1, V2);
  ASSERT_NE(LShrResult, nullptr);
}
