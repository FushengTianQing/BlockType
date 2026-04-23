//===--- ConceptTemplateParamTest.cpp - Concept template template param  -===//
//
// Part of the BlockType Project.
// E7.5.2.5: Concept template template parameters (P2841R7)
//
//===--------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Parse/Parser.h"
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/Sema/Sema.h"

using namespace blocktype;

namespace {

class ConceptTemplateParamTest : public ::testing::Test {
protected:
  SourceManager SM;
  DiagnosticsEngine Diags;
  ASTContext Context;
  std::unique_ptr<Sema> S;
  std::unique_ptr<Preprocessor> PP;
  std::unique_ptr<Parser> P;

  void TearDown() override {
    P.reset();
    PP.reset();
    S.reset();
  }

  void parse(StringRef Code) {
    PP = std::make_unique<Preprocessor>(SM, Diags);
    PP->enterSourceFile("test.cpp", Code);
    S = std::make_unique<Sema>(Context, Diags);
    P = std::make_unique<Parser>(*PP, Context, *S);
  }
};

// Test 1: Parse template <typename> concept C as template template parameter
TEST_F(ConceptTemplateParamTest, ParseConceptTemplateParam) {
  parse("template <template <typename> concept C> void f();");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  
  // The declaration should be a function template
  auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D);
  ASSERT_NE(FTD, nullptr);
}

// Test 2: TemplateTemplateParmDecl has isConceptParam set
TEST_F(ConceptTemplateParamTest, ConceptParamFlag) {
  parse("template <template <typename> concept C> void f();");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  
  auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D);
  ASSERT_NE(FTD, nullptr);
  
  // Check template parameters
  auto *TPL = FTD->getTemplateParameterList();
  ASSERT_NE(TPL, nullptr);
  ASSERT_EQ(TPL->size(), 1u);
  
  auto *Param = TPL->getParam(0);
  auto *TTPD = llvm::dyn_cast<TemplateTemplateParmDecl>(Param);
  ASSERT_NE(TTPD, nullptr);
  EXPECT_TRUE(TTPD->isConceptParam());
}

// Test 3: Traditional template <typename> class C still works
TEST_F(ConceptTemplateParamTest, TraditionalTemplateTemplateParam) {
  parse("template <template <typename> class C> void f();");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  
  auto *FTD = llvm::dyn_cast<FunctionTemplateDecl>(D);
  ASSERT_NE(FTD, nullptr);
  
  auto *TPL = FTD->getTemplateParameterList();
  ASSERT_NE(TPL, nullptr);
  ASSERT_EQ(TPL->size(), 1u);
  
  auto *Param = TPL->getParam(0);
  auto *TTPD = llvm::dyn_cast<TemplateTemplateParmDecl>(Param);
  ASSERT_NE(TTPD, nullptr);
  EXPECT_FALSE(TTPD->isConceptParam());
}

} // anonymous namespace
