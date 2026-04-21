//===--- ModuleDependencyGraph.h - Module Dependency Graph ---*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ModuleDependencyGraph for managing module dependencies.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/Basic/LLVM.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>

namespace blocktype {

struct ModuleInfo;

/// ModuleNode - 依赖图节点
struct ModuleNode {
  llvm::StringRef Name;
  ModuleInfo *Info;
  llvm::SmallVector<ModuleNode *, 8> Dependencies; // 直接依赖
  llvm::SmallVector<ModuleNode *, 8> Dependents;   // 被依赖
  bool Visited = false;                             // 用于遍历
  bool InStack = false;                             // 用于循环检测
};

/// ModuleDependencyGraph - 模块依赖图
///
/// 支持拓扑排序和循环依赖检测。
class ModuleDependencyGraph {
  llvm::StringMap<std::unique_ptr<ModuleNode>> Nodes;

public:
  /// 添加模块节点
  /// \param Name 模块名
  /// \param Info 模块信息
  /// \return 节点指针
  ModuleNode *addNode(llvm::StringRef Name, ModuleInfo *Info);

  /// 添加依赖边 (A depends on B)
  /// \param A 依赖方模块名
  /// \param B 被依赖模块名
  void addDependency(llvm::StringRef A, llvm::StringRef B);

  /// 获取拓扑排序（编译顺序）
  /// \return 拓扑序，存在循环依赖返回空
  llvm::SmallVector<ModuleNode *, 16> topologicalSort();

  /// 检测循环依赖
  /// \return 循环依赖链，无循环返回空
  llvm::SmallVector<ModuleNode *, 8> detectCycle();

  /// 获取模块的所有依赖（递归）
  /// \param Name 模块名
  /// \return 依赖列表
  llvm::SmallVector<ModuleNode *, 16> getAllDependencies(llvm::StringRef Name);

  /// 获取模块的所有被依赖者（递归）
  /// \param Name 模块名
  /// \return 被依赖列表
  llvm::SmallVector<ModuleNode *, 16> getAllDependents(llvm::StringRef Name);

  /// 获取节点
  /// \param Name 模块名
  /// \return 节点指针，不存在返回 nullptr
  ModuleNode *getNode(llvm::StringRef Name) const;

  /// 检查节点是否存在
  /// \param Name 模块名
  /// \return 存在返回 true
  bool hasNode(llvm::StringRef Name) const;

  /// 获取所有节点
  /// \return 节点列表
  llvm::SmallVector<ModuleNode *, 16> getAllNodes() const;

  /// 清空图
  void clear();

private:
  /// DFS 拓扑排序
  bool dfsTopologicalSort(ModuleNode *Node,
                          llvm::SmallVectorImpl<ModuleNode *> &Result);

  /// DFS 循环检测
  bool dfsDetectCycle(ModuleNode *Node,
                      llvm::SmallVectorImpl<ModuleNode *> &Path);

  /// DFS 收集所有依赖
  void dfsCollectDependencies(ModuleNode *Node,
                              llvm::SmallVectorImpl<ModuleNode *> &Result,
                              llvm::StringMap<bool> &Visited);
};

} // namespace blocktype
