//===--- IRTypeMapper.h - AST Type to IR Type Mapping -------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IRTypeMapper class, which maps AST-level types
// (QualType/BuiltinType/PointerType/etc.) to IR-level types
// (IRIntegerType/IRPointerType/etc.).
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_IRTYPEMAPPER_H
#define BLOCKTYPE_FRONTEND_IRTYPEMAPPER_H

#include <cassert>

#include "blocktype/AST/Type.h"
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

namespace blocktype {
namespace frontend {

/// IRTypeMapper - Maps AST QualTypes to IR IRTypes.
///
/// This class bridges the AST layer and the IR layer:
/// - Input: blocktype::QualType (from the AST)
/// - Output: ir::IRType* (from the IR type context)
///
/// Mapping rules:
/// - CVR qualifiers are stripped (const/volatile don't affect IR layout)
/// - Results are cached: same Type* → same IRType*
/// - Recursive types (struct with self-pointer) use IROpaqueType placeholder
///
/// Thread safety: Not thread-safe. Each compilation thread should have
/// its own IRTypeMapper instance.
class IRTypeMapper {
  ir::IRTypeContext& TypeCtx_;
  const ir::TargetLayout& Layout_;
  DiagnosticsEngine& Diags_;

  /// Cache: canonical Type* → mapped IRType*.
  /// Uses pointer identity (not deep equality) for cache lookup.
  ir::DenseMap<const Type*, ir::IRType*> Cache_;

public:
  /// Construct an IRTypeMapper.
  ///
  /// \param TC   IR type context for creating IR types.
  /// \param L    Target layout for platform-dependent type sizes.
  /// \param Diag Diagnostics engine for error reporting.
  explicit IRTypeMapper(ir::IRTypeContext& TC,
                        const ir::TargetLayout& L,
                        DiagnosticsEngine& Diag);

  /// Map an AST QualType to an IR IRType.
  ///
  /// \param T The AST type to map.
  /// \returns The corresponding IR type, or an IROpaqueType("error") on failure.
  ir::IRType* mapType(QualType T);

  /// Map a raw AST Type pointer to an IR IRType.
  /// Strips CVR qualifiers from the QualType before mapping.
  ir::IRType* mapType(const Type* T);

private:
  // === Type-specific mapping methods ===

  ir::IRType* mapBuiltinType(const BuiltinType* BT);
  ir::IRType* mapPointerType(const PointerType* PT);
  ir::IRType* mapReferenceType(const ReferenceType* RT);
  ir::IRType* mapArrayType(const ArrayType* AT);
  ir::IRType* mapFunctionType(const FunctionType* FT);
  ir::IRType* mapRecordType(const RecordType* RT);
  ir::IRType* mapEnumType(const EnumType* ET);
  ir::IRType* mapTypedefType(const TypedefType* TT);
  ir::IRType* mapElaboratedType(const ElaboratedType* ET);
  ir::IRType* mapTemplateSpecializationType(const TemplateSpecializationType* TST);
  ir::IRType* mapDependentType(const DependentType* DT);
  ir::IRType* mapAutoType(const AutoType* AT);
  ir::IRType* mapDecltypeType(const DecltypeType* DT);
  ir::IRType* mapMemberPointerType(const MemberPointerType* MPT);

  /// Emit an error type (IROpaqueType with "error" name).
  ir::IRType* emitErrorType();
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_IRTYPEMAPPER_H
