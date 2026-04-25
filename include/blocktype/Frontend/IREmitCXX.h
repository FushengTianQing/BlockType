//===--- IREmitCXX.h - IR C++ Emitter -----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_IREMITCXX_H
#define BLOCKTYPE_FRONTEND_IREMITCXX_H

#include "blocktype/AST/Decl.h"
#include "blocktype/AST/Expr.h"
#include "blocktype/AST/Stmt.h"
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRFunction.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRValue.h"
#include "blocktype/IR/IRTypeContext.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <memory>

namespace blocktype {
namespace frontend {

class ASTToIRConverter;

//===----------------------------------------------------------------------===//
// IREmitCXX — Top-level C++ IR emission dispatcher
//===----------------------------------------------------------------------===//

/// IREmitCXX — Top-level C++ IR emission dispatcher.
///
/// Owns the four sub-emitters (Layout, CtorDtor, VTable, Inherit).
/// Created by ASTToIRConverter::initializeEmitters().
class IREmitCXX {
  ASTToIRConverter& Converter_;

  // Sub-emitters (owned)
  std::unique_ptr<class IREmitCXXLayout> LayoutEmitter_;
  std::unique_ptr<class IREmitCXXCtorDtor> CtorDtorEmitter_;
  std::unique_ptr<class IREmitCXXVTable> VTableEmitter_;
  std::unique_ptr<class IREmitCXXInherit> InheritEmitter_;

public:
  explicit IREmitCXX(ASTToIRConverter& C);
  ~IREmitCXX();

  // Non-copyable
  IREmitCXX(const IREmitCXX&) = delete;
  IREmitCXX& operator=(const IREmitCXX&) = delete;

  //===--- Top-level dispatch ---===//

  /// Emit C++ constructor into IRFn.
  void EmitCXXConstructor(CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);

  /// Emit C++ destructor into IRFn.
  void EmitCXXDestructor(CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);

  /// Emit VTable for the given class.
  void EmitVTable(const CXXRecordDecl* RD);

  /// Emit RTTI typeinfo for the given class.
  void EmitRTTI(const CXXRecordDecl* RD);

  /// Emit thunk for the given virtual method.
  void EmitThunk(const CXXMethodDecl* MD);

  //===--- Sub-emitter access ---===//

  IREmitCXXLayout& getLayoutEmitter() { return *LayoutEmitter_; }
  IREmitCXXCtorDtor& getCtorDtorEmitter() { return *CtorDtorEmitter_; }
  IREmitCXXVTable& getVTableEmitter() { return *VTableEmitter_; }
  IREmitCXXInherit& getInheritEmitter() { return *InheritEmitter_; }

  /// Helper: check if a class has virtual functions in hierarchy.
  static bool hasVirtualFunctionsInHierarchy(const CXXRecordDecl* RD);
  static bool hasVirtualFunctions(const CXXRecordDecl* RD);
};

//===----------------------------------------------------------------------===//
// IREmitCXXLayout — Class layout computation
//===----------------------------------------------------------------------===//

/// IREmitCXXLayout — Computes class layout (field offsets, base offsets).
///
/// Results are cached per CXXRecordDecl.
class IREmitCXXLayout {
  ASTToIRConverter& Converter_;

  /// Cache: CXXRecordDecl* → computed field offsets (in bytes)
  llvm::DenseMap<const CXXRecordDecl*, llvm::SmallVector<uint64_t, 16>>
      FieldOffsetsCache_;

  /// Cache: CXXRecordDecl* → class size (in bytes)
  llvm::DenseMap<const CXXRecordDecl*, uint64_t> ClassSizeCache_;

  /// Cache: {Derived, Base} → base offset (in bytes)
  llvm::DenseMap<std::pair<const CXXRecordDecl*, const CXXRecordDecl*>,
                 uint64_t>
      BaseOffsetCache_;

  /// Cache: CXXRecordDecl* → IRStructType (IR representation)
  llvm::DenseMap<const CXXRecordDecl*, ir::IRStructType*> StructTypeCache_;

  /// Cache: CXXRecordDecl* → vptr field index in IRStructType
  llvm::DenseMap<const CXXRecordDecl*, unsigned> VPtrIndexCache_;

public:
  explicit IREmitCXXLayout(ASTToIRConverter& C);

  /// Compute and cache class layout. Creates IRStructType in IRTypeContext.
  /// Returns the computed IRStructType.
  ir::IRStructType* ComputeClassLayout(const CXXRecordDecl* RD);

  /// Get the field offset (in bytes) for a FieldDecl.
  uint64_t GetFieldOffset(const FieldDecl* FD);

  /// Get the total class size (in bytes).
  uint64_t GetClassSize(const CXXRecordDecl* RD);

  /// Get the non-virtual base offset (in bytes).
  uint64_t GetBaseOffset(const CXXRecordDecl* Derived,
                         const CXXRecordDecl* Base);

  /// Get the virtual base offset (in bytes).
  /// Returns 0 for now; full implementation in Phase C.
  uint64_t GetVirtualBaseOffset(const CXXRecordDecl* Derived,
                                const CXXRecordDecl* VBase);

  /// Get or create the IRStructType for a CXXRecordDecl.
  ir::IRStructType* GetOrCreateStructType(const CXXRecordDecl* RD);

  /// Get the vptr field index within the IRStructType.
  unsigned GetVPtrIndex(const CXXRecordDecl* RD);

  /// Check if class has virtual functions in hierarchy.
  static bool hasVirtualFunctionsInHierarchy(const CXXRecordDecl* RD);
  static bool hasVirtualFunctions(const CXXRecordDecl* RD);
};

//===----------------------------------------------------------------------===//
// IREmitCXXCtorDtor — Constructor/destructor emission
//===----------------------------------------------------------------------===//

/// IREmitCXXCtorDtor — Emits IR for C++ constructors and destructors.
class IREmitCXXCtorDtor {
  ASTToIRConverter& Converter_;

public:
  explicit IREmitCXXCtorDtor(ASTToIRConverter& C);

  /// Emit a complete constructor into IRFn.
  void EmitConstructor(const CXXConstructorDecl* Ctor, ir::IRFunction* IRFn);

  /// Emit base class initializer (call base constructor).
  void EmitBaseInitializer(ir::IRValue* This,
                           const CXXCtorInitializer* Init);

  /// Emit member initializer (initialize field).
  void EmitMemberInitializer(ir::IRValue* This,
                             const CXXCtorInitializer* Init);

  /// Emit delegating constructor (call another ctor of same class).
  void EmitDelegatingConstructor(ir::IRValue* This,
                                 const CXXCtorInitializer* Init);

  /// Emit a complete destructor into IRFn.
  void EmitDestructor(const CXXDestructorDecl* Dtor, ir::IRFunction* IRFn);

  /// Emit destructor body (destroy members, call base dtor).
  void EmitDestructorBody(ir::IRValue* This,
                          const CXXDestructorDecl* Dtor);

  /// Emit a call to a destructor on an object.
  void EmitDestructorCall(const CXXDestructorDecl* Dtor,
                          ir::IRValue* Object);
};

//===----------------------------------------------------------------------===//
// IREmitCXXVTable — VTable/RTTI emission
//===----------------------------------------------------------------------===//

/// IREmitCXXVTable — Emits VTable and RTTI as placeholder global variables.
///
/// Key design: VTables are emitted as IRGlobalVariable with IROpaqueType.
/// The actual byte layout will be filled by the backend (Phase C).
class IREmitCXXVTable {
  ASTToIRConverter& Converter_;

  /// Cache: CXXRecordDecl* → VTable IRGlobalVariable
  llvm::DenseMap<const CXXRecordDecl*, ir::IRGlobalVariable*> VTableCache_;

  /// Cache: CXXRecordDecl* → RTTI IRGlobalVariable
  llvm::DenseMap<const CXXRecordDecl*, ir::IRGlobalVariable*> RTTICache_;

  /// Cache: CXXMethodDecl* → vtable index
  llvm::DenseMap<const CXXMethodDecl*, uint64_t> VTableIndexCache_;

public:
  explicit IREmitCXXVTable(ASTToIRConverter& C);

  /// Emit VTable as a placeholder global variable.
  void EmitVTable(const CXXRecordDecl* RD);

  /// Get the VTable type (IROpaqueType placeholder).
  ir::IRType* GetVTableType(const CXXRecordDecl* RD);

  /// Get the vtable index for a virtual method.
  uint64_t GetVTableIndex(const CXXMethodDecl* MD);

  /// Initialize the vptr in an object's struct.
  void InitializeVTablePtr(ir::IRValue* Object, const CXXRecordDecl* RD);

  /// Emit VTable initialization code. No-op in IR layer.
  void EmitVTableInitialization(const CXXRecordDecl* RD);

  /// Emit RTTI typeinfo as a placeholder global variable.
  void EmitRTTI(const CXXRecordDecl* RD);

  /// Emit catch typeinfo (used by exception handling).
  void EmitCatchTypeInfo(const CXXCatchStmt* CS);

  /// Get or create VTable global variable.
  ir::IRGlobalVariable* GetOrCreateVTable(const CXXRecordDecl* RD);
};

//===----------------------------------------------------------------------===//
// IREmitCXXInherit — Inheritance/polymorphism emission
//===----------------------------------------------------------------------===//

/// IREmitCXXInherit — Emits IR for inheritance-related operations.
class IREmitCXXInherit {
  ASTToIRConverter& Converter_;

public:
  explicit IREmitCXXInherit(ASTToIRConverter& C);

  /// Emit pointer adjustment from derived to base (upcast).
  ir::IRValue* EmitCastToBase(ir::IRValue* Object,
                              const CXXRecordDecl* Derived,
                              const CXXRecordDecl* Base);

  /// Emit pointer adjustment from base to derived (downcast).
  ir::IRValue* EmitCastToDerived(ir::IRValue* Object,
                                 const CXXRecordDecl* Base,
                                 const CXXRecordDecl* Derived);

  /// Compute the byte offset from derived to base.
  uint64_t EmitBaseOffset(const CXXRecordDecl* Derived,
                          const CXXRecordDecl* Base);

  /// Emit dynamic_cast runtime check.
  ir::IRValue* EmitDynamicCast(ir::IRValue* Object,
                               const CXXDynamicCastExpr* DCE);

  /// Emit a thunk function for virtual method adjustment.
  void EmitThunk(const CXXMethodDecl* MD);

  /// Emit VTT (Virtual Table Table) for virtual inheritance.
  void EmitVTT(const CXXRecordDecl* RD);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IREMITCXX_H
