//===--- CppFrontend.h - C++ Frontend -----------------------*- C++ -*-===//
//
// Part of the BlockType Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef BLOCKTYPE_FRONTEND_CPPFRONTEND_H
#define BLOCKTYPE_FRONTEND_CPPFRONTEND_H

#include <memory>
#include <string>

#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/Basic/SourceManager.h"
#include "blocktype/Frontend/FrontendBase.h"
#include "blocktype/Frontend/FrontendCompileOptions.h"
#include "blocktype/AST/ASTContext.h"
#include "blocktype/Lex/Preprocessor.h"
#include "blocktype/Parse/Parser.h"
#include "blocktype/Sema/Sema.h"
#include "blocktype/Frontend/ASTToIRConverter.h"
#include "blocktype/IR/IRContext.h"
#include "blocktype/IR/IRContract.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/TargetLayout.h"

namespace blocktype {
namespace frontend {

/// CppFrontend - C/C++ language frontend implementation.
///
/// Parses C/C++ source files via the existing Lexer+Parser+Sema pipeline,
/// then converts the resulting AST to IR via ASTToIRConverter.
/// Optionally verifies IR contracts when Opts.VerifyIR is true.
class CppFrontend : public FrontendBase {
  // === Compilation pipeline components (created in compile()) ===
  std::unique_ptr<SourceManager> SM_;
  std::unique_ptr<ASTContext> ASTCtx_;
  std::unique_ptr<Sema> SemaPtr_;
  std::unique_ptr<Preprocessor> PP_;
  std::unique_ptr<Parser> ParserPtr_;

  // === IR context (owned, lifetime-bound to this frontend) ===
  std::unique_ptr<ir::IRContext> IRCtx_;

public:
  CppFrontend(const FrontendCompileOptions& Opts, DiagnosticsEngine& Diags);

  ~CppFrontend() override = default;

  ir::StringRef getName() const override { return "cpp"; }
  ir::StringRef getLanguage() const override { return "c++"; }

  std::unique_ptr<ir::IRModule> compile(
    ir::StringRef Filename,
    ir::IRTypeContext& TypeCtx,
    const ir::TargetLayout& Layout) override;

  bool canHandle(ir::StringRef Filename) const override;

private:
  static std::string readSourceFile(ir::StringRef Filename);
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_CPPFRONTEND_H
