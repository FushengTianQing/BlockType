//===--- ASTContext.h - AST Context --------------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ASTContext class which manages memory for AST nodes.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "blocktype/AST/ASTNode.h"
#include "llvm/Support/Allocator.h"
#include <memory>
#include <vector>

namespace blocktype {

/// ASTContext - Manages memory allocation and lifetime of AST nodes.
///
/// All AST nodes should be created through this context using the create()
/// method. The context owns all nodes and will destroy them when the context
/// is destroyed.
///
/// Memory Management:
/// - Uses llvm::BumpPtrAllocator for efficient allocation
/// - All nodes are destroyed together when context is destroyed
/// - No individual node deletion needed
///
class ASTContext {
  /// Allocator for AST nodes.
  llvm::BumpPtrAllocator Allocator;

  /// List of all allocated nodes for cleanup.
  /// We store raw pointers here because the allocator owns the memory.
  std::vector<ASTNode *> Nodes;

public:
  ASTContext() = default;
  ~ASTContext();

  // Non-copyable and non-movable
  ASTContext(const ASTContext &) = delete;
  ASTContext &operator=(const ASTContext &) = delete;
  ASTContext(ASTContext &&) = delete;
  ASTContext &operator=(ASTContext &&) = delete;

  /// Creates a new AST node of type T with the given arguments.
  ///
  /// The node is allocated using the bump pointer allocator and will be
  /// destroyed when the context is destroyed.
  ///
  /// \param args Arguments to forward to T's constructor.
  /// \return Pointer to the newly created node.
  template <typename T, typename... Args>
  T *create(Args &&...args) {
    // Allocate memory
    void *Mem = Allocator.Allocate(sizeof(T), alignof(T));
    
    // Construct the object
    T *Node = new (Mem) T(std::forward<Args>(args)...);
    
    // Track for cleanup
    Nodes.push_back(Node);
    
    return Node;
  }

  /// Returns the total number of AST nodes allocated.
  size_t getNumNodes() const { return Nodes.size(); }

  /// Returns the total memory used by AST nodes.
  size_t getMemoryUsage() const {
    return Allocator.getTotalMemory();
  }

  /// Clears all AST nodes.
  /// Warning: This destroys all nodes. Any pointers to nodes become invalid.
  void clear();

  /// Dumps memory usage statistics for debugging.
  void dumpMemoryUsage(raw_ostream &OS) const;

private:
  /// Destroys all nodes in reverse order of creation.
  void destroyAll();
};

} // namespace blocktype
