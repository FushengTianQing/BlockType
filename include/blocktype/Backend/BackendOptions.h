#pragma once
#include <string>
#include "blocktype/IR/ADT.h"

namespace blocktype::backend {

/// BackendOptions — 后端专用配置
/// 与 FrontendCompileOptions 对称，从 CodeGenOptions 提取的子集
struct BackendOptions {
  std::string TargetTriple;
  std::string OutputPath;
  std::string OutputFormat = "elf";  // elf, mach-o, coff
  unsigned OptimizationLevel = 0;
  bool EmitAssembly = false;
  bool EmitIR = false;
  bool EmitIRBitcode = false;
  bool DebugInfo = false;
  bool DebugInfoForProfiling = false;
  std::string DebugInfoFormat = "dwarf5";
};

} // namespace blocktype::backend
