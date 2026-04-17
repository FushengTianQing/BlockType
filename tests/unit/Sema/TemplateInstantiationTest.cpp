//===--- TemplateInstantiationTest.cpp - Template Instantiation Tests ----*- C++ -*-===//
//
// Part of the BlockType Project.
// Task E5.5.3.1 — Template Instantiation Tests
//
//===--------------------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Type.h"
#include "blocktype/AST/TemplateParameterList.h"
#include "blocktype/Sema/TemplateInstantiation.h"
#include "blocktype/Basic/Diagnostics.h"

using namespace blocktype;

namespace {

class TemplateInstantiationTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  std::unique_ptr<Sema> S;

  TemplateInstantiationTest() : Diags() {
    S = std::make_unique<Sema>(Context, Diags);
  }

  /// Helper: create a simple class template with one type parameter T.
  ClassTemplateDecl *createClassTemplate(llvm::StringRef Name) {
    auto *Record = Context.create<CXXRecordDecl>(SourceLocation(1), Name);
    Record->setCompleteDefinition(true);

    auto *CTD = Context.create<ClassTemplateDecl>(
        SourceLocation(1), Name, Record);

    auto *TTPD = Context.create<TemplateTypeParmDecl>(
        SourceLocation(1), "T", /*Depth=*/0, /*Index=*/0,
        /*IsParameterPack=*/false, /*IsTypename=*/true);
    llvm::SmallVector<NamedDecl *, 2> Params = {TTPD};
    auto *TPL = new TemplateParameterList(
        SourceLocation(1), SourceLocation(1), SourceLocation(1), Params);
    CTD->setTemplateParameterList(TPL);

    return CTD;
  }

  /// Helper: create a function template with one type parameter.
  FunctionTemplateDecl *createFunctionTemplate(llvm::StringRef Name) {
    QualType IntTy = Context.getIntType();
    llvm::SmallVector<ParmVarDecl *, 2> Params;
    auto *FD = Context.create<FunctionDecl>(
        SourceLocation(1), Name, IntTy, Params);

    auto *FTD = Context.create<FunctionTemplateDecl>(
        SourceLocation(1), Name, FD);

    auto *TTPD = Context.create<TemplateTypeParmDecl>(
        SourceLocation(1), "T", 0, 0, false, true);
    llvm::SmallVector<NamedDecl *, 2> TParams = {TTPD};
    auto *TPL = new TemplateParameterList(
        SourceLocation(1), SourceLocation(1), SourceLocation(1), TParams);
    FTD->setTemplateParameterList(TPL);

    return FTD;
  }
};

// --- Class Template Instantiation ---

TEST_F(TemplateInstantiationTest, InstantiateClassTemplateWithInt) {
  auto *CTD = createClassTemplate("Vector");
  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  auto &Inst = S->getTemplateInstantiator();
  auto *Spec = Inst.InstantiateClassTemplate(CTD, Args);

  ASSERT_NE(Spec, nullptr);
  EXPECT_EQ(Spec->getName(), "Vector");
  EXPECT_EQ(Spec->getNumTemplateArgs(), 1u);
  EXPECT_TRUE(Spec->isCompleteDefinition());
}

TEST_F(TemplateInstantiationTest, InstantiateClassTemplateCaching) {
  auto *CTD = createClassTemplate("Vec");
  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  auto &Inst = S->getTemplateInstantiator();
  auto *Spec1 = Inst.InstantiateClassTemplate(CTD, Args);
  ASSERT_NE(Spec1, nullptr);

  auto *Spec2 = Inst.InstantiateClassTemplate(CTD, Args);
  EXPECT_EQ(Spec1, Spec2) << "Expected cached specialization";
}

TEST_F(TemplateInstantiationTest, InstantiateClassTemplateDifferentArgs) {
  auto *CTD = createClassTemplate("Box");
  auto &Inst = S->getTemplateInstantiator();

  llvm::SmallVector<TemplateArgument, 2> IntArgs;
  IntArgs.push_back(TemplateArgument(Context.getIntType()));
  auto *IntSpec = Inst.InstantiateClassTemplate(CTD, IntArgs);

  llvm::SmallVector<TemplateArgument, 2> FloatArgs;
  FloatArgs.push_back(TemplateArgument(Context.getFloatType()));
  auto *FloatSpec = Inst.InstantiateClassTemplate(CTD, FloatArgs);

  ASSERT_NE(IntSpec, nullptr);
  ASSERT_NE(FloatSpec, nullptr);
  EXPECT_NE(IntSpec, FloatSpec);
}

// --- Function Template Instantiation ---

TEST_F(TemplateInstantiationTest, InstantiateFunctionTemplate) {
  auto *FTD = createFunctionTemplate("identity");
  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  auto &Inst = S->getTemplateInstantiator();
  auto *FD = Inst.InstantiateFunctionTemplate(FTD, Args);

  ASSERT_NE(FD, nullptr);
  EXPECT_EQ(FD->getName(), "identity");
}

// --- Type Substitution ---

TEST_F(TemplateInstantiationTest, SubstituteTypeBuiltinReturnsSame) {
  auto &Inst = S->getTemplateInstantiator();
  TemplateArgumentList Args;
  Args.push_back(TemplateArgument(Context.getFloatType()));

  QualType IntTy = Context.getIntType();
  QualType Result = Inst.SubstituteType(IntTy, Args);
  EXPECT_EQ(Result.getTypePtr(), IntTy.getTypePtr());
}

TEST_F(TemplateInstantiationTest, SubstituteTypeNullReturnsNull) {
  auto &Inst = S->getTemplateInstantiator();
  TemplateArgumentList Args;
  QualType Result = Inst.SubstituteType(QualType(), Args);
  EXPECT_TRUE(Result.isNull());
}

// --- Expression Substitution ---

TEST_F(TemplateInstantiationTest, SubstituteExprNonDependent) {
  auto &Inst = S->getTemplateInstantiator();
  TemplateArgumentList Args;

  auto *Lit = Context.create<IntegerLiteral>(SourceLocation(1),
                                              llvm::APInt(32, 42),
                                              Context.getIntType());
  Expr *Result = Inst.SubstituteExpr(Lit, Args);
  EXPECT_EQ(Result, Lit);
}

TEST_F(TemplateInstantiationTest, SubstituteExprNullReturnsNull) {
  auto &Inst = S->getTemplateInstantiator();
  TemplateArgumentList Args;
  Expr *Result = Inst.SubstituteExpr(nullptr, Args);
  EXPECT_EQ(Result, nullptr);
}

TEST_F(TemplateInstantiationTest, SubstituteExprBinaryOperator) {
  auto &Inst = S->getTemplateInstantiator();
  TemplateArgumentList Args;

  auto *LHS = Context.create<IntegerLiteral>(SourceLocation(1),
                                              llvm::APInt(32, 1),
                                              Context.getIntType());
  auto *RHS = Context.create<IntegerLiteral>(SourceLocation(2),
                                              llvm::APInt(32, 2),
                                              Context.getIntType());
  auto *BO = Context.create<BinaryOperator>(SourceLocation(3), LHS, RHS,
                                             BinaryOpKind::Add);

  Expr *Result = Inst.SubstituteExpr(BO, Args);
  EXPECT_NE(Result, nullptr);
}

// --- FindExistingSpecialization ---

TEST_F(TemplateInstantiationTest, FindExistingSpecializationReturnsNullForNew) {
  auto *CTD = createClassTemplate("List");
  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  auto &Inst = S->getTemplateInstantiator();
  auto *Found = Inst.FindExistingSpecialization(CTD, Args);
  EXPECT_EQ(Found, nullptr);
}

TEST_F(TemplateInstantiationTest, FindExistingSpecializationReturnsCached) {
  auto *CTD = createClassTemplate("Map");
  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  auto &Inst = S->getTemplateInstantiator();
  auto *Spec = Inst.InstantiateClassTemplate(CTD, Args);
  ASSERT_NE(Spec, nullptr);

  auto *Found = Inst.FindExistingSpecialization(CTD, Args);
  EXPECT_EQ(Found, Spec);
}

// --- Sema Integration ---

TEST_F(TemplateInstantiationTest, ActOnClassTemplateDeclRegisters) {
  auto *CTD = createClassTemplate("MyVec");
  auto Result = S->ActOnClassTemplateDecl(CTD);
  EXPECT_TRUE(Result.isUsable());

  auto *Found = S->getSymbolTable().lookupTemplate("MyVec");
  EXPECT_EQ(Found, CTD);
}

TEST_F(TemplateInstantiationTest, IsCompleteTypeForSpecialization) {
  auto *CTD = createClassTemplate("Complete");
  llvm::SmallVector<TemplateArgument, 2> Args;
  Args.push_back(TemplateArgument(Context.getIntType()));

  auto &Inst = S->getTemplateInstantiator();
  auto *Spec = Inst.InstantiateClassTemplate(CTD, Args);
  ASSERT_NE(Spec, nullptr);

  QualType SpecType = Context.getRecordType(Spec);
  EXPECT_TRUE(S->isCompleteType(SpecType));
}

} // anonymous namespace
