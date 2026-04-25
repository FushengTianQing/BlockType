//===--- FrontendBase.cpp - Abstract Frontend Base Class --------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "blocktype/Frontend/FrontendBase.h"

#include <cassert>

namespace blocktype {
namespace frontend {

FrontendBase::FrontendBase(const FrontendCompileOptions& Opts,
                           DiagnosticsEngine& Diags)
  : Opts_(Opts), Diags_(Diags) {
  // Precondition: TargetTriple must be non-empty before compile() is called.
  // We do not assert here to allow deferred TargetTriple setup.
}

} // namespace frontend
} // namespace blocktype
