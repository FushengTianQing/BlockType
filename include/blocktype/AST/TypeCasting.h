//===--- TypeCasting.h - Type Casting Support -----------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides dyn_cast and cast support for the Type system.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/Support/Casting.h"

namespace blocktype {

// Forward declarations
class Type;

//===----------------------------------------------------------------------===//
// Type Casting Support
//===----------------------------------------------------------------------===//

/// isa<T>(Type) - Check if a Type is of type T.
template <typename T>
inline bool isa(const Type *Ty) {
  return T::classof(Ty);
}

/// dyn_cast<T>(Type) - Cast a Type to T, returning nullptr if not of type T.
template <typename T>
inline T *dyn_cast(Type *Ty) {
  return isa<T>(Ty) ? static_cast<T *>(Ty) : nullptr;
}

template <typename T>
inline const T *dyn_cast(const Type *Ty) {
  return isa<T>(Ty) ? static_cast<const T *>(Ty) : nullptr;
}

/// cast<T>(Type) - Cast a Type to T. Asserts if Type is not of type T.
template <typename T>
inline T *cast(Type *Ty) {
  assert(isa<T>(Ty) && "Invalid cast!");
  return static_cast<T *>(Ty);
}

template <typename T>
inline const T *cast(const Type *Ty) {
  assert(isa<T>(Ty) && "Invalid cast!");
  return static_cast<const T *>(Ty);
}

} // namespace blocktype
