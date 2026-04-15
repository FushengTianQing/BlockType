//===--- ASTTest.cpp - AST Infrastructure Tests --------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"
#include "blocktype/AST/ASTNode.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/AST/ASTVisitor.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/Basic/SourceLocation.h"
#include "llvm/ADT/APInt.h"

using namespace blocktype;

namespace {

/// TestASTNode - A simple test AST node.
class TestASTNode : public ASTNode {
public:
  TestASTNode(SourceLocation Loc) : ASTNode(Loc) {}

  NodeKind getKind() const override { return NodeKind::ExprKind; }
  
  void dump(raw_ostream &OS, unsigned Indent = 0) const override {
    printIndent(OS, Indent);
    OS << "TestASTNode\n";
  }
  
  static bool classof(const ASTNode *N) {
    return N->getKind() == NodeKind::ExprKind;
  }
};

} // anonymous namespace

//===----------------------------------------------------------------------===//
// ASTContext Tests
//===----------------------------------------------------------------------===//

TEST(ASTContextTest, CreateNode) {
  ASTContext Context;
  
  SourceLocation Loc(42);
  TestASTNode *Node = Context.create<TestASTNode>(Loc);
  
  ASSERT_NE(Node, nullptr);
  EXPECT_EQ(Node->getLocation(), Loc);
  EXPECT_EQ(Context.getNumNodes(), 1u);
}

TEST(ASTContextTest, MultipleNodes) {
  ASTContext Context;
  
  for (int I = 0; I < 10; ++I) {
    SourceLocation Loc(I);
    TestASTNode *Node = Context.create<TestASTNode>(Loc);
    ASSERT_NE(Node, nullptr);
    EXPECT_EQ(Node->getLocation(), Loc);
  }
  
  EXPECT_EQ(Context.getNumNodes(), 10u);
}

TEST(ASTContextTest, MemoryUsage) {
  ASTContext Context;
  
  // Create some nodes
  for (int I = 0; I < 100; ++I) {
    Context.create<TestASTNode>(SourceLocation(I));
  }
  
  size_t Memory = Context.getMemoryUsage();
  EXPECT_GT(Memory, 0u);
  
  // Memory usage should be at least the size of nodes
  EXPECT_GE(Memory, 100 * sizeof(TestASTNode));
}

TEST(ASTContextTest, Clear) {
  ASTContext Context;
  
  // Create some nodes
  for (int I = 0; I < 10; ++I) {
    Context.create<TestASTNode>(SourceLocation(I));
  }
  
  EXPECT_EQ(Context.getNumNodes(), 10u);
  
  // Clear context
  Context.clear();
  
  EXPECT_EQ(Context.getNumNodes(), 0u);
  
  // Can create new nodes after clear
  TestASTNode *Node = Context.create<TestASTNode>(SourceLocation(0));
  ASSERT_NE(Node, nullptr);
  EXPECT_EQ(Context.getNumNodes(), 1u);
}

//===----------------------------------------------------------------------===//
// ASTNode Tests
//===----------------------------------------------------------------------===//

TEST(ASTNodeTest, SourceLocation) {
  ASTContext Context;
  
  SourceLocation Loc(12345);
  TestASTNode *Node = Context.create<TestASTNode>(Loc);
  
  EXPECT_EQ(Node->getLocation(), Loc);
}

TEST(ASTNodeTest, Dump) {
  ASTContext Context;
  
  TestASTNode *Node = Context.create<TestASTNode>(SourceLocation(0));
  
  // Test that dump() doesn't crash
  std::string Str;
  llvm::raw_string_ostream OS(Str);
  Node->dump(OS);
  
  EXPECT_FALSE(Str.empty());
  EXPECT_TRUE(Str.find("TestASTNode") != std::string::npos);
}

//===----------------------------------------------------------------------===//
// Type Checking Tests
//===----------------------------------------------------------------------===//

TEST(ASTNodeTest, Isa) {
  ASTContext Context;
  
  TestASTNode *Node = Context.create<TestASTNode>(SourceLocation(0));
  ASTNode *Base = Node;
  
  EXPECT_TRUE(isa<TestASTNode>(Base));
}

TEST(ASTNodeTest, DynCast) {
  ASTContext Context;
  
  TestASTNode *Node = Context.create<TestASTNode>(SourceLocation(0));
  ASTNode *Base = Node;
  
  TestASTNode *Casted = dyn_cast<TestASTNode>(Base);
  ASSERT_NE(Casted, nullptr);
  EXPECT_EQ(Casted, Node);
}

TEST(ASTNodeTest, Cast) {
  ASTContext Context;
  
  TestASTNode *Node = Context.create<TestASTNode>(SourceLocation(0));
  ASTNode *Base = Node;
  
  TestASTNode *Casted = cast<TestASTNode>(Base);
  EXPECT_EQ(Casted, Node);
}

//===----------------------------------------------------------------------===//
// ASTVisitor Tests
//===----------------------------------------------------------------------===//

namespace {

/// TestVisitor - A simple test visitor that counts nodes.
class TestVisitor : public ASTVisitor<TestVisitor> {
public:
  int Count = 0;

  void visitIntegerLiteral(IntegerLiteral *Node) {
    ++Count;
  }
};

} // anonymous namespace

TEST(ASTVisitorTest, Visit) {
  ASTContext Context;

  // Create real expression nodes
  IntegerLiteral *Node1 = Context.create<IntegerLiteral>(
      SourceLocation(1), llvm::APInt(64, 42));
  IntegerLiteral *Node2 = Context.create<IntegerLiteral>(
      SourceLocation(2), llvm::APInt(64, 100));

  // Visit nodes
  TestVisitor Visitor;
  Visitor.visit(Node1);
  EXPECT_EQ(Visitor.Count, 1);

  Visitor.visit(Node2);
  EXPECT_EQ(Visitor.Count, 2);
}

//===----------------------------------------------------------------------===//
// SourceRange Tests
//===----------------------------------------------------------------------===//

TEST(SourceRangeTest, Basic) {
  SourceLocation Begin(10);
  SourceLocation End(20);
  
  SourceRange Range(Begin, End);
  
  EXPECT_EQ(Range.getBegin(), Begin);
  EXPECT_EQ(Range.getEnd(), End);
  EXPECT_TRUE(Range.isValid());
}

TEST(SourceRangeTest, SingleLocation) {
  SourceLocation Loc(42);
  
  SourceRange Range(Loc);
  
  EXPECT_EQ(Range.getBegin(), Loc);
  EXPECT_EQ(Range.getEnd(), Loc);
  EXPECT_TRUE(Range.isValid());
}

TEST(SourceRangeTest, Invalid) {
  SourceRange Range;
  
  EXPECT_FALSE(Range.isValid());
  EXPECT_TRUE(Range.isInvalid());
}
