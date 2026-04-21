//===--- ModuleDependencyGraph.cpp - Module Dependency Graph Implementation -*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Module/ModuleDependencyGraph.h"
#include "blocktype/Module/ModuleManager.h"

namespace blocktype {

ModuleNode *ModuleDependencyGraph::addNode(llvm::StringRef Name,
                                            ModuleInfo *Info) {
  auto Node = std::make_unique<ModuleNode>();
  Node->Name = Name;
  Node->Info = Info;

  ModuleNode *Result = Node.get();
  Nodes[Name] = std::move(Node);
  return Result;
}

void ModuleDependencyGraph::addDependency(llvm::StringRef A,
                                          llvm::StringRef B) {
  auto NodeA = getNode(A);
  auto NodeB = getNode(B);

  if (!NodeA || !NodeB) {
    return;
  }

  // 添加依赖边
  NodeA->Dependencies.push_back(NodeB);
  NodeB->Dependents.push_back(NodeA);
}

llvm::SmallVector<ModuleNode *, 16>
ModuleDependencyGraph::topologicalSort() {
  llvm::SmallVector<ModuleNode *, 16> Result;

  // 重置访问标记
  for (auto &Pair : Nodes) {
    Pair.second->Visited = false;
    Pair.second->InStack = false;
  }

  // DFS 拓扑排序
  for (auto &Pair : Nodes) {
    if (!Pair.second->Visited) {
      if (!dfsTopologicalSort(Pair.second.get(), Result)) {
        // 存在循环依赖，返回空
        return {};
      }
    }
  }

  // DFS 已经产生了正确的拓扑序（依赖项在前）
  return Result;
}

llvm::SmallVector<ModuleNode *, 8> ModuleDependencyGraph::detectCycle() {
  llvm::SmallVector<ModuleNode *, 8> Path;

  // 重置访问标记
  for (auto &Pair : Nodes) {
    Pair.second->Visited = false;
    Pair.second->InStack = false;
  }

  // DFS 检测循环
  for (auto &Pair : Nodes) {
    if (!Pair.second->Visited) {
      if (dfsDetectCycle(Pair.second.get(), Path)) {
        return Path;
      }
    }
  }

  return {};
}

llvm::SmallVector<ModuleNode *, 16>
ModuleDependencyGraph::getAllDependencies(llvm::StringRef Name) {
  llvm::SmallVector<ModuleNode *, 16> Result;
  auto Node = getNode(Name);
  if (!Node) {
    return Result;
  }

  llvm::StringMap<bool> Visited;
  dfsCollectDependencies(Node, Result, Visited);

  return Result;
}

llvm::SmallVector<ModuleNode *, 16>
ModuleDependencyGraph::getAllDependents(llvm::StringRef Name) {
  llvm::SmallVector<ModuleNode *, 16> Result;
  auto Node = getNode(Name);
  if (!Node) {
    return Result;
  }

  llvm::StringMap<bool> Visited;
  llvm::SmallVector<ModuleNode *, 16> Stack;
  Stack.push_back(Node);

  while (!Stack.empty()) {
    ModuleNode *Current = Stack.pop_back_val();
    for (ModuleNode *Dependent : Current->Dependents) {
      if (Visited[Dependent->Name]) {
        continue;
      }
      Visited[Dependent->Name] = true;
      Result.push_back(Dependent);
      Stack.push_back(Dependent);
    }
  }

  return Result;
}

ModuleNode *ModuleDependencyGraph::getNode(llvm::StringRef Name) const {
  auto It = Nodes.find(Name);
  return It != Nodes.end() ? It->second.get() : nullptr;
}

bool ModuleDependencyGraph::hasNode(llvm::StringRef Name) const {
  return Nodes.find(Name) != Nodes.end();
}

llvm::SmallVector<ModuleNode *, 16>
ModuleDependencyGraph::getAllNodes() const {
  llvm::SmallVector<ModuleNode *, 16> Result;
  for (const auto &Pair : Nodes) {
    Result.push_back(Pair.second.get());
  }
  return Result;
}

void ModuleDependencyGraph::clear() { Nodes.clear(); }

bool ModuleDependencyGraph::dfsTopologicalSort(
    ModuleNode *Node, llvm::SmallVectorImpl<ModuleNode *> &Result) {
  Node->Visited = true;
  Node->InStack = true;

  for (ModuleNode *Dep : Node->Dependencies) {
    if (Dep->InStack) {
      // 检测到循环依赖
      return false;
    }
    if (!Dep->Visited) {
      if (!dfsTopologicalSort(Dep, Result)) {
        return false;
      }
    }
  }

  Node->InStack = false;
  Result.push_back(Node);
  return true;
}

bool ModuleDependencyGraph::dfsDetectCycle(
    ModuleNode *Node, llvm::SmallVectorImpl<ModuleNode *> &Path) {
  Node->Visited = true;
  Node->InStack = true;
  Path.push_back(Node);

  for (ModuleNode *Dep : Node->Dependencies) {
    if (Dep->InStack) {
      // 找到循环
      // 找到循环的起点
      auto It = std::find(Path.begin(), Path.end(), Dep);
      if (It != Path.end()) {
        Path.erase(Path.begin(), It);
      }
      return true;
    }
    if (!Dep->Visited) {
      if (dfsDetectCycle(Dep, Path)) {
        return true;
      }
    }
  }

  Node->InStack = false;
  Path.pop_back();
  return false;
}

void ModuleDependencyGraph::dfsCollectDependencies(
    ModuleNode *Node, llvm::SmallVectorImpl<ModuleNode *> &Result,
    llvm::StringMap<bool> &Visited) {
  for (ModuleNode *Dep : Node->Dependencies) {
    if (Visited[Dep->Name]) {
      continue;
    }
    Visited[Dep->Name] = true;
    Result.push_back(Dep);
    dfsCollectDependencies(Dep, Result, Visited);
  }
}

} // namespace blocktype
