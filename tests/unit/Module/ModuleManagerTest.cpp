//===--- ModuleManagerTest.cpp - Module Manager Unit Tests ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Module/ModuleManager.h"
#include "blocktype/Module/ModuleDependencyGraph.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Basic/Diagnostics.h"
#include "gtest/gtest.h"

using namespace blocktype;

class ModuleManagerTest : public ::testing::Test {
protected:
  ASTContext Context;
  DiagnosticsEngine Diags;
  std::unique_ptr<ModuleManager> ModMgr;

  void SetUp() override {
    ModMgr = std::make_unique<ModuleManager>(Context, Diags);
  }
};

TEST_F(ModuleManagerTest, CreateModuleManager) {
  ASSERT_NE(ModMgr, nullptr);
}

TEST_F(ModuleManagerTest, AddSearchPath) {
  ModMgr->addSearchPath("/usr/local/modules");
  ModMgr->addSearchPath("/home/user/modules");

  auto Paths = ModMgr->getSearchPaths();
  EXPECT_EQ(Paths.size(), 2u);
  EXPECT_EQ(Paths[0], "/usr/local/modules");
  EXPECT_EQ(Paths[1], "/home/user/modules");
}

TEST_F(ModuleManagerTest, SetSearchPaths) {
  std::vector<std::string> Paths = {"/path1", "/path2", "/path3"};
  ModMgr->setSearchPaths(Paths);

  auto Result = ModMgr->getSearchPaths();
  EXPECT_EQ(Result.size(), 3u);
}

TEST_F(ModuleManagerTest, ModuleNotLoadedInitially) {
  EXPECT_FALSE(ModMgr->isModuleLoaded("NonExistent"));
  EXPECT_EQ(ModMgr->getModuleDecl("NonExistent"), nullptr);
}

TEST_F(ModuleManagerTest, GetCurrentModuleInitiallyNull) {
  EXPECT_EQ(ModMgr->getCurrentModule(), nullptr);
  EXPECT_FALSE(ModMgr->isInModule());
}

TEST_F(ModuleManagerTest, GetLoadedModulesEmptyInitially) {
  auto Modules = ModMgr->getLoadedModules();
  EXPECT_TRUE(Modules.empty());
}

//===----------------------------------------------------------------------===//
// ModuleDependencyGraph Tests
//===----------------------------------------------------------------------===//

class DependencyGraphTest : public ::testing::Test {
protected:
  ModuleDependencyGraph Graph;
};

TEST_F(DependencyGraphTest, AddNode) {
  auto *Node = Graph.addNode("ModuleA", nullptr);
  ASSERT_NE(Node, nullptr);
  EXPECT_EQ(Node->Name, "ModuleA");
  EXPECT_TRUE(Graph.hasNode("ModuleA"));
}

TEST_F(DependencyGraphTest, AddDependency) {
  auto *A = Graph.addNode("A", nullptr);
  auto *B = Graph.addNode("B", nullptr);

  Graph.addDependency("A", "B");

  EXPECT_EQ(A->Dependencies.size(), 1u);
  EXPECT_EQ(A->Dependencies[0], B);
  EXPECT_EQ(B->Dependents.size(), 1u);
  EXPECT_EQ(B->Dependents[0], A);
}

TEST_F(DependencyGraphTest, TopologicalSortSimple) {
  Graph.addNode("A", nullptr);
  Graph.addNode("B", nullptr);
  Graph.addNode("C", nullptr);

  Graph.addDependency("A", "B");
  Graph.addDependency("B", "C");

  auto Order = Graph.topologicalSort();
  EXPECT_EQ(Order.size(), 3u);
  // C should come before B, B before A
  auto PosC = std::find(Order.begin(), Order.end(), Graph.getNode("C"));
  auto PosB = std::find(Order.begin(), Order.end(), Graph.getNode("B"));
  auto PosA = std::find(Order.begin(), Order.end(), Graph.getNode("A"));
  EXPECT_LT(PosC - Order.begin(), PosB - Order.begin());
  EXPECT_LT(PosB - Order.begin(), PosA - Order.begin());
}

TEST_F(DependencyGraphTest, DetectCycle) {
  Graph.addNode("A", nullptr);
  Graph.addNode("B", nullptr);
  Graph.addNode("C", nullptr);

  Graph.addDependency("A", "B");
  Graph.addDependency("B", "C");
  Graph.addDependency("C", "A"); // Cycle

  auto Cycle = Graph.detectCycle();
  EXPECT_FALSE(Cycle.empty()); // Should detect cycle

  auto Order = Graph.topologicalSort();
  EXPECT_TRUE(Order.empty()); // Should return empty due to cycle
}

TEST_F(DependencyGraphTest, GetAllDependencies) {
  Graph.addNode("A", nullptr);
  Graph.addNode("B", nullptr);
  Graph.addNode("C", nullptr);

  Graph.addDependency("A", "B");
  Graph.addDependency("B", "C");

  auto Deps = Graph.getAllDependencies("A");
  EXPECT_EQ(Deps.size(), 2u); // B and C
}

TEST_F(DependencyGraphTest, Clear) {
  Graph.addNode("A", nullptr);
  Graph.addNode("B", nullptr);

  EXPECT_TRUE(Graph.hasNode("A"));
  EXPECT_TRUE(Graph.hasNode("B"));

  Graph.clear();

  EXPECT_FALSE(Graph.hasNode("A"));
  EXPECT_FALSE(Graph.hasNode("B"));
}
