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
#include "blocktype/AST/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Allocator.h"
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace blocktype {

class Type;
class BuiltinType;
class PointerType;
class LValueReferenceType;
class RValueReferenceType;
class ArrayType;
class Expr;
class TypeDecl;
class RecordDecl;
class EnumDecl;
class TypedefNameDecl;

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

  /// Cleanup callbacks for resources that need explicit destruction.
  /// Following Clang's pattern: bump-allocated AST nodes that own resources
  /// (e.g., llvm::SmallVector members) register a cleanup callback here.
  /// These callbacks are invoked in reverse registration order before
  /// the allocator is reset, ensuring proper cleanup of non-trivial resources.
  std::vector<std::function<void()>> Cleanups;
  
  /// Cached builtin types.
  BuiltinType *BuiltinTypes[static_cast<unsigned>(BuiltinKind::NumBuiltinTypes)] = {};

  /// Cache: RecordDecl* -> RecordType* (ensures uniqueness for type comparison)
  llvm::DenseMap<RecordDecl *, RecordType *> RecordTypeCache;

  /// Cache: EnumDecl* -> EnumType* (ensures uniqueness for type comparison)
  llvm::DenseMap<EnumDecl *, EnumType *> EnumTypeCache;

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
    static_assert(std::is_base_of_v<ASTNode, T>,
                  "create<T>() can only be used with ASTNode subclasses "
                  "(Decl/Stmt/Expr). Use ASTContext type factory methods "
                  "(getPointerType, getFunctionType, etc.) for Type creation.");
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
  
  /// Returns the allocator for AST nodes.
  llvm::BumpPtrAllocator &getAllocator() { return Allocator; }

  /// Register a cleanup callback to be invoked when ASTContext is destroyed.
  /// Use this for bump-allocated objects that own resources requiring
  /// explicit destruction (e.g., containers with heap-allocated storage).
  /// Callbacks are invoked in reverse registration order (LIFO).
  void addCleanup(std::function<void()> Callback) {
    Cleanups.push_back(std::move(Callback));
  }
  
  /// Saves a string in the ASTContext's memory pool.
  /// Returns a StringRef that points to the saved copy.
  llvm::StringRef saveString(llvm::StringRef Str) {
    // Allocate memory for the string (including null terminator)
    size_t Size = Str.size() + 1;
    void *Mem = Allocator.Allocate(Size, 1);
    
    // Copy the string
    char *Buf = static_cast<char *>(Mem);
    std::memcpy(Buf, Str.data(), Str.size());
    Buf[Str.size()] = '\0'; // Null terminator
    
    // Return StringRef to the saved copy
    return llvm::StringRef(Buf, Str.size());
  }
  
  //===--------------------------------------------------------------------===//
  // Type creation
  //===--------------------------------------------------------------------===//
  
  /// Gets or creates a builtin type.
  BuiltinType *getBuiltinType(BuiltinKind Kind);
  
  /// Gets or creates a pointer type.
  PointerType *getPointerType(const Type *Pointee);
  
  /// Gets or creates an lvalue reference type.
  LValueReferenceType *getLValueReferenceType(const Type *Referenced);
  
  /// Gets or creates an rvalue reference type.
  RValueReferenceType *getRValueReferenceType(const Type *Referenced);
  
  /// Gets or creates an array type.
  ArrayType *getArrayType(const Type *Element, Expr *Size);

  /// Gets or creates a constant array type with known size.
  ConstantArrayType *getConstantArrayType(const Type *Element, Expr *SizeExpr,
                                          llvm::APInt Size);

  /// Gets or creates an incomplete array type (int[]).
  IncompleteArrayType *getIncompleteArrayType(const Type *Element);

  /// Gets or creates a variable length array type.
  VariableArrayType *getVariableArrayType(const Type *Element, Expr *SizeExpr);

  /// Gets or creates a template type parameter type.
  TemplateTypeParmType *getTemplateTypeParmType(class TemplateTypeParmDecl *Decl,
                                                unsigned Index, unsigned Depth,
                                                bool IsPack = false);

  /// Gets or creates a dependent type.
  DependentType *getDependentType(const Type *BaseType, llvm::StringRef Name);
  
  /// Gets or creates an unresolved type.
  UnresolvedType *getUnresolvedType(llvm::StringRef Name);

  /// Gets or creates a template specialization type.
  TemplateSpecializationType *getTemplateSpecializationType(llvm::StringRef Name);
  
  /// Gets or creates an elaborated type.
  ElaboratedType *getElaboratedType(const Type *NamedType, llvm::StringRef Qualifier);
  
  /// Gets or creates a decltype type.
  DecltypeType *getDecltypeType(Expr *E, QualType Underlying = QualType());
  
  /// Gets or creates a member pointer type.
  MemberPointerType *getMemberPointerType(const Type *ClassType, const Type *PointeeType);
  
  /// Gets or creates a function type.
  /// \param IsConst Method qualifier (for member functions): R (C::*)(Args...) const
  /// \param IsVolatile Method qualifier (for member functions): R (C::*)(Args...) volatile
  FunctionType *getFunctionType(const Type *ReturnType,
                                llvm::ArrayRef<const Type *> ParamTypes,
                                bool IsVariadic = false,
                                bool IsConst = false,
                                bool IsVolatile = false);
  
  /// Gets the type for a type declaration.
  QualType getTypeDeclType(const TypeDecl *D);

  /// Get or create an AutoType.
  QualType getAutoType();

  //===--------------------------------------------------------------------===//
  // Convenience type factories [Stage 4.2]
  //===--------------------------------------------------------------------===//

  /// Gets or creates a RecordType for the given RecordDecl.
  QualType getRecordType(RecordDecl *D);

  /// Gets or creates an EnumType for the given EnumDecl.
  QualType getEnumType(EnumDecl *D);

  /// Gets the built-in void type.
  QualType getVoidType();

  /// Gets the built-in bool type.
  QualType getBoolType();

  /// Gets the built-in int type.
  QualType getIntType();

  /// Gets the built-in float type.
  QualType getFloatType();

  /// Gets the built-in double type.
  QualType getDoubleType();

  /// Gets the built-in long type.
  QualType getLongType();

  /// Gets the nullptr_t type.
  QualType getNullPtrType();

  /// Gets the built-in char type.
  QualType getCharType();

  /// Gets the built-in signed char type.
  QualType getSCharType();

  /// Gets the built-in unsigned char type.
  QualType getUCharType();

  /// Gets the built-in wchar_t type.
  QualType getWCharType();

  /// Gets the built-in short type.
  QualType getShortType();

  /// Gets the built-in unsigned short type.
  QualType getUShortType();

  /// Gets the built-in unsigned int type.
  QualType getUIntType();

  /// Gets the built-in unsigned long type.
  QualType getULongType();

  /// Gets the built-in long long type.
  QualType getLongLongType();

  /// Gets the built-in unsigned long long type.
  QualType getULongLongType();

  /// Gets a qualified type with the given CVR qualifiers.
  QualType getQualifiedType(const Type *T, Qualifier Q);

  /// Gets a pointer-to-member function type.
  QualType getMemberFunctionType(const Type *ReturnType,
                                  llvm::ArrayRef<const Type *> ParamTypes,
                                  const Type *ClassType,
                                  bool IsConst, bool IsVolatile,
                                  bool IsVariadic);

private:
  /// Destroys all nodes in reverse order of creation.
  void destroyAll();
};

} // namespace blocktype
