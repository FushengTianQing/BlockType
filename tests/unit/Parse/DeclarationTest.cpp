//===--- DeclarationTest.cpp - Declaration Parsing Tests --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>
#include "blocktype/Parse/Parser.h"
#include "blocktype/Lex/Lexer.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Sema/Sema.h"

using namespace blocktype;

namespace {

class DeclarationTest : public ::testing::Test {
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

//===----------------------------------------------------------------------===//
// Class Declaration Tests
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, SimpleClass) {
  parse("class MyClass { };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "MyClass");
  EXPECT_FALSE(RD->isStruct());
  EXPECT_FALSE(RD->isUnion());
}

TEST_F(DeclarationTest, SimpleStruct) {
  parse("struct MyStruct { };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "MyStruct");
  EXPECT_TRUE(RD->isStruct());
}

TEST_F(DeclarationTest, ClassWithMembers) {
  parse("class Point { int x; int y; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Point");
  EXPECT_EQ(RD->members().size(), 2U) << "Point should have 2 members (x and y)";
}

TEST_F(DeclarationTest, ClassWithMemberFunction) {
  parse("class Calculator { int add(int a, int b); };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Calculator");
}

TEST_F(DeclarationTest, ClassWithAccessSpecifier) {
  parse("class Access { public: int x; private: int y; protected: int z; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Access");
}

TEST_F(DeclarationTest, ClassWithBase) {
  parse("class Derived : public Base { };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Derived");
}

TEST_F(DeclarationTest, ClassWithVirtualBase) {
  parse("class Derived : public virtual Base { };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Derived");
}

TEST_F(DeclarationTest, ClassWithMultipleBases) {
  parse("class Derived : public Base1, protected Base2 { };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Derived");
}

TEST_F(DeclarationTest, ClassWithConstructor) {
  parse("class Widget { Widget(int value); };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Widget");
}

TEST_F(DeclarationTest, ClassWithDestructor) {
  parse("class Resource { ~Resource(); };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Resource");
}

TEST_F(DeclarationTest, ClassWithStaticMember) {
  parse("class Counter { static int count; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Counter");
}

TEST_F(DeclarationTest, ClassWithVirtualFunction) {
  parse("class Base { virtual void foo(); };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Base");
}

TEST_F(DeclarationTest, ClassWithPureVirtualFunction) {
  parse("class Interface { virtual void foo() = 0; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "Interface");
}

TEST_F(DeclarationTest, ClassWithFriend) {
  parse("class MyClass { friend void helper(); friend class FriendClass; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<CXXRecordDecl>(D));
  
  auto *RD = llvm::cast<CXXRecordDecl>(D);
  EXPECT_EQ(RD->getName(), "MyClass");
}

//===----------------------------------------------------------------------===//
// Template Declaration Tests
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, SimpleTemplate) {
  parse("template<typename T> class Container { T value; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
  
  auto *TD = llvm::cast<TemplateDecl>(D);
  EXPECT_TRUE(TD->getTemplatedDecl() != nullptr);
}

TEST_F(DeclarationTest, TemplateWithMultipleParams) {
  parse("template<typename T, typename U> class Pair { T first; U second; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

TEST_F(DeclarationTest, TemplateWithNonTypeParam) {
  parse("template<typename T, int N> class Array { T data[N]; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

TEST_F(DeclarationTest, TemplateWithDefaultArg) {
  parse("template<typename T = int> class Wrapper { T value; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

TEST_F(DeclarationTest, TemplateTemplateParameter) {
  parse("template<template<typename> class Container> class Adapter { };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

TEST_F(DeclarationTest, VariadicTemplate) {
  parse("template<typename... Args> class Tuple { };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

TEST_F(DeclarationTest, FunctionTemplate) {
  parse("template<typename T> T max(T a, T b);");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

TEST_F(DeclarationTest, TemplateSpecialization) {
  parse("template<> class Container<int> { };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
}

TEST_F(DeclarationTest, TemplateWithConcept) {
  parse("template<typename T> requires Integral<T> T foo(T x);");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

//===----------------------------------------------------------------------===//
// Namespace Declaration Tests
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, SimpleNamespace) {
  parse("namespace MyNamespace { int x; }");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<NamespaceDecl>(D));
  
  auto *ND = llvm::cast<NamespaceDecl>(D);
  EXPECT_EQ(ND->getName(), "MyNamespace");
}

TEST_F(DeclarationTest, NestedNamespace) {
  parse("namespace Outer { namespace Inner { int x; } }");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<NamespaceDecl>(D));
}

TEST_F(DeclarationTest, InlineNamespace) {
  parse("inline namespace Details { int value; }");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<NamespaceDecl>(D));
}

TEST_F(DeclarationTest, UsingDeclaration) {
  parse("using std::vector;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<UsingDecl>(D));
}

TEST_F(DeclarationTest, UsingDirective) {
  parse("using namespace std;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<UsingDirectiveDecl>(D));
}

TEST_F(DeclarationTest, NamespaceAlias) {
  parse("namespace fs = std::filesystem;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
}

//===----------------------------------------------------------------------===//
// Enum Declaration Tests
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, SimpleEnum) {
  parse("enum Color { Red, Green, Blue };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<EnumDecl>(D));
  
  auto *ED = llvm::cast<EnumDecl>(D);
  EXPECT_EQ(ED->getName(), "Color");
}

TEST_F(DeclarationTest, EnumClass) {
  parse("enum class Status { Ok, Error, Pending };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<EnumDecl>(D));
  
  auto *ED = llvm::cast<EnumDecl>(D);
  EXPECT_EQ(ED->getName(), "Status");
  EXPECT_TRUE(ED->isScoped()) << "enum class should have isScoped() == true";
}

TEST_F(DeclarationTest, EnumWithExplicitType) {
  parse("enum class Size : unsigned char { Small = 1, Medium = 2, Large = 3 };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<EnumDecl>(D));
}

//===----------------------------------------------------------------------===//
// Variable Declaration Tests
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, SimpleVariable) {
  parse("int x;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<VarDecl>(D));
  
  auto *VD = llvm::cast<VarDecl>(D);
  EXPECT_EQ(VD->getName(), "x");
}

TEST_F(DeclarationTest, VariableWithInitializer) {
  parse("int x = 42;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<VarDecl>(D));
  
  auto *VD = llvm::cast<VarDecl>(D);
  EXPECT_EQ(VD->getName(), "x");
}

TEST_F(DeclarationTest, ConstVariable) {
  parse("const int kMaxSize = 100;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<VarDecl>(D));
}

TEST_F(DeclarationTest, StaticVariable) {
  parse("static int counter = 0;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<VarDecl>(D));
}

TEST_F(DeclarationTest, AutoVariable) {
  parse("auto value = 42;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<VarDecl>(D));
}

//===----------------------------------------------------------------------===//
// Function Declaration Tests
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, SimpleFunction) {
  parse("void foo();");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<FunctionDecl>(D));
  
  auto *FD = llvm::cast<FunctionDecl>(D);
  EXPECT_EQ(FD->getName(), "foo");
}

TEST_F(DeclarationTest, FunctionWithParams) {
  parse("int add(int a, int b);");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<FunctionDecl>(D));
  
  auto *FD = llvm::cast<FunctionDecl>(D);
  EXPECT_EQ(FD->getName(), "add");
}

TEST_F(DeclarationTest, FunctionWithDefaultArgs) {
  parse("void greet(const char* name = \"World\");");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<FunctionDecl>(D));
}

TEST_F(DeclarationTest, ConstexprFunction) {
  parse("constexpr int square(int x) { return x * x; }");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<FunctionDecl>(D));
}

TEST_F(DeclarationTest, InlineFunction) {
  parse("inline int get_value() { return 42; }");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<FunctionDecl>(D));
}

TEST_F(DeclarationTest, StaticFunction) {
  parse("static void helper();");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<FunctionDecl>(D));
}

TEST_F(DeclarationTest, VirtualFunction) {
  parse("class Base { virtual void foo(); };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
}

TEST_F(DeclarationTest, OverrideFunction) {
  parse("class Derived : public Base { void foo() override; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
}

TEST_F(DeclarationTest, FinalFunction) {
  parse("class Final { virtual void foo() final; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
}

//===----------------------------------------------------------------------===//
// Type Alias Tests
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, Typedef) {
  parse("typedef int Integer;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TypedefDecl>(D));
}

TEST_F(DeclarationTest, TypeAlias) {
  parse("using Integer = int;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TypeAliasDecl>(D));
}

TEST_F(DeclarationTest, TemplateTypeAlias) {
  parse("template<typename T> using Vec = std::vector<T>;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

//===----------------------------------------------------------------------===//
// Module Declaration Tests (C++20)
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, ModuleDeclaration) {
  parse("module MyModule;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<ModuleDecl>(D));
}

TEST_F(DeclarationTest, ModuleImport) {
  parse("import std;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<ImportDecl>(D));
}

TEST_F(DeclarationTest, ModuleExport) {
  parse("export module MyModule;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<ModuleDecl>(D));
}

TEST_F(DeclarationTest, ExportBlock) {
  parse("export { int x; int y; }");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<ExportDecl>(D));
}

TEST_F(DeclarationTest, ExportSingleDeclaration) {
  parse("export int x;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  EXPECT_TRUE(llvm::isa<ExportDecl>(D));
}

//===----------------------------------------------------------------------===//
// Concept Declaration Tests (C++20)
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, ConceptDeclaration) {
  parse("template<typename T> concept Integral = std::is_integral_v<T>;");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  // Concept is wrapped in a TemplateDecl
  EXPECT_TRUE(llvm::isa<TemplateDecl>(D));
}

//===----------------------------------------------------------------------===//
// Static Assert Tests
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, StaticAssert) {
  parse("static_assert(sizeof(int) == 4);");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
}

TEST_F(DeclarationTest, StaticAssertWithMessage) {
  parse("static_assert(sizeof(void*) == 8, \"64-bit only\");");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
}

//===----------------------------------------------------------------------===//
// Template Specialization Disambiguation in Type Context
//===----------------------------------------------------------------------===//

TEST_F(DeclarationTest, TemplateSpecTypeInClassMember) {
  // Vector<int> as type in class member should be recognized as template spec
  parse("template<typename T> class Vector {}; struct S { Vector<int> x; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  // First declaration is the template, second is the struct
  D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  auto *Record = llvm::dyn_cast<CXXRecordDecl>(D);
  ASSERT_NE(Record, nullptr);
  EXPECT_EQ(Record->getName(), "S");
}

TEST_F(DeclarationTest, TypeDisambigWithBuiltinType) {
  // Unknown<int> - <int> has builtin type keyword, should be parsed as template type
  parse("struct S { Unknown<int> x; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
  auto *Record = llvm::dyn_cast<CXXRecordDecl>(D);
  ASSERT_NE(Record, nullptr);
  // Field may or may not be created depending on Unknown resolution,
  // but parsing should succeed without crash
}

TEST_F(DeclarationTest, TypeDisambigWithKnownTemplate) {
  // Register Vector as a template, then use Vector<T> as member type
  parse("template<typename T> class Vector {}; struct S { Vector<float> v; };");
  Decl *D = P->parseDeclaration(); // template
  ASSERT_NE(D, nullptr);
  D = P->parseDeclaration(); // struct S
  ASSERT_NE(D, nullptr);
  auto *Record = llvm::dyn_cast<CXXRecordDecl>(D);
  ASSERT_NE(Record, nullptr);
  EXPECT_EQ(Record->getName(), "S");
}

TEST_F(DeclarationTest, NestedTemplateSpecType) {
  // Vector<Vector<int>> as type should work
  parse("struct S { Vector<Vector<int>> x; };");
  Decl *D = P->parseDeclaration();
  ASSERT_NE(D, nullptr);
}

} // anonymous namespace
