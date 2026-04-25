//===--- CppFrontendRegistration.cpp - Static Registration --*- C++ -*-===//

#include "blocktype/Frontend/CppFrontend.h"
#include "blocktype/Frontend/FrontendRegistry.h"

namespace blocktype {
namespace frontend {

/// Factory function: creates CppFrontend instances.
static std::unique_ptr<FrontendBase>
createCppFrontend(const FrontendCompileOptions& Opts,
                  DiagnosticsEngine& Diags) {
  return std::make_unique<CppFrontend>(Opts, Diags);
}

/// Static registrator — auto-registers the C++ frontend at program start.
static struct CppFrontendRegistrator {
  CppFrontendRegistrator() {
    auto& Reg = FrontendRegistry::instance();
    Reg.registerFrontend("cpp", createCppFrontend);
    Reg.addExtensionMapping(".cpp", "cpp");
    Reg.addExtensionMapping(".cc",  "cpp");
    Reg.addExtensionMapping(".cxx", "cpp");
    Reg.addExtensionMapping(".C",   "cpp");
    Reg.addExtensionMapping(".c",   "cpp");
    Reg.addExtensionMapping(".h",   "cpp");
    Reg.addExtensionMapping(".hpp", "cpp");
    Reg.addExtensionMapping(".hxx", "cpp");
  }
} CppFrontendRegistratorInstance;

} // namespace frontend
} // namespace blocktype
