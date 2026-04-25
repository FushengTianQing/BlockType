//===--- CppFrontend.cpp - C++ Frontend Implementation ------*- C++ -*-===//

#include "blocktype/Frontend/CppFrontend.h"

#include "llvm/ADT/StringRef.h"

#include <fstream>
#include <sstream>

namespace blocktype {
namespace frontend {

//===----------------------------------------------------------------------===//
// Construction
//===----------------------------------------------------------------------===//

CppFrontend::CppFrontend(const FrontendCompileOptions& Opts,
                          DiagnosticsEngine& Diags)
  : FrontendBase(Opts, Diags) {}

//===----------------------------------------------------------------------===//
// compile()
//===----------------------------------------------------------------------===//

std::unique_ptr<ir::IRModule>
CppFrontend::compile(ir::StringRef Filename,
                      ir::IRTypeContext& TypeCtx,
                      const ir::TargetLayout& Layout) {
  // === 1. Read source file ===
  std::string Content = readSourceFile(Filename);
  if (Content.empty()) {
    Diags_.report(SourceLocation(), DiagLevel::Error,
                  "cannot open input file: " + Filename.str());
    return nullptr;
  }

  // === 2. Initialize compilation pipeline ===
  SM_ = std::make_unique<SourceManager>();
  ASTCtx_ = std::make_unique<ASTContext>();
  SemaPtr_ = std::make_unique<Sema>(*ASTCtx_, Diags_);
  PP_ = std::make_unique<Preprocessor>(*SM_, Diags_,
                                        /*HeaderSearch=*/nullptr,
                                        /*LanguageManager=*/nullptr,
                                        /*FileManager=*/nullptr);
  ParserPtr_ = std::make_unique<Parser>(*PP_, *ASTCtx_, *SemaPtr_);

  // === 3. Load source and parse ===
  SM_->createMainFileID(llvm::StringRef(Filename.data(), Filename.size()),
                         llvm::StringRef(Content.data(), Content.size()));
  TranslationUnitDecl* TU = ParserPtr_->parseTranslationUnit();
  if (!TU) {
    Diags_.report(SourceLocation(), DiagLevel::Error,
                  "parsing failed for: " + Filename.str());
    return nullptr;
  }

  // === 4. AST → IR conversion ===
  IRCtx_ = std::make_unique<ir::IRContext>();
  ASTToIRConverter Converter(*IRCtx_, TypeCtx, Layout, Diags_);
  ir::IRConversionResult Result = Converter.convert(TU);

  if (!Result.isUsable()) {
    Diags_.report(SourceLocation(), DiagLevel::Error,
                  "IR conversion failed for: " + Filename.str());
    return nullptr;
  }

  // === 5. Contract verification (optional) ===
  if (Opts_.VerifyIR) {
    ir::IRVerificationResult VR =
        ir::contract::verifyAllContracts(*Result.getModule());
    if (!VR.isValid()) {
      for (const auto& V : VR.getViolations()) {
        Diags_.report(SourceLocation(), DiagLevel::Warning,
                      "IR contract violation: " + V);
      }
    }
  }

  // === 6. Return IRModule ===
  // Note: IRModule references IRTypeContext& (external) and IRValue/IRConstant
  // are allocated via IRContext::create<>(). IRCtx_ must remain alive while
  // the module is in use. Caller must ensure CppFrontend outlives the module.
  return Result.takeModule();
}

//===----------------------------------------------------------------------===//
// canHandle()
//===----------------------------------------------------------------------===//

bool CppFrontend::canHandle(ir::StringRef Filename) const {
  static const char* Extensions[] = {
    ".cpp", ".cc", ".cxx", ".C", ".c", ".h", ".hpp", ".hxx"
  };
  for (auto* Ext : Extensions) {
    if (Filename.endswith(Ext))
      return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// readSourceFile()
//===----------------------------------------------------------------------===//

std::string CppFrontend::readSourceFile(ir::StringRef Filename) {
  std::ifstream IF(Filename.str());
  if (!IF.is_open()) return {};
  std::ostringstream SS;
  SS << IF.rdbuf();
  return SS.str();
}

} // namespace frontend
} // namespace blocktype
