#include "blocktype/Backend/FFIWasmComponent.h"

#include <sstream>

#include "blocktype/Backend/FFIWasmMapper.h"
#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRType.h"

namespace blocktype {
namespace backend {

FFIWasmComponent::FFIWasmComponent(ir::IRTypeContext& C) : Ctx(C) {}

std::string FFIWasmComponent::generate(const ir::IRModule& M) {
  std::ostringstream OS;

  // WASM Component Model 头部
  OS << "package blocktype:component@0.1.0\n\n";

  // 接口定义
  OS << "interface blocktype-exports {\n";

  // 为每个函数生成 wasm->func 声明
  for (auto& F : M.getFunctions()) {
    auto Name = F->getName().str();
    auto* FT = F->getFunctionType();
    if (!FT)
      continue;

    // 使用 FFIWasmMapper 映射类型
    std::string RetWasmType = "void";
    if (FT->getReturnType()) {
      RetWasmType = FFIWasmMapper::mapToWasmType(FT->getReturnType());
      if (RetWasmType.empty())
        RetWasmType = "void";
    }

    // wasm->func 声明
    OS << "  " << Name << ": func(";
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
      auto* ParamTy = FT->getParamType(i);
      std::string ParamWasmType = FFIWasmMapper::mapToWasmType(ParamTy);
      if (ParamWasmType.empty())
        ParamWasmType = "externref";
      OS << "arg" << i << ": " << ParamWasmType;
      if (i + 1 < FT->getNumParams())
        OS << ", ";
    }
    OS << ") -> " << RetWasmType;

    // 标注调用约定为 WASM
    (void)ir::ffi::CallingConvention::WASM;

    OS << ";\n";
  }

  OS << "}\n\n";

  // World 定义
  OS << "world blocktype-world {\n";
  OS << "  export blocktype-exports;\n";
  OS << "}\n";

  return OS.str();
}

} // namespace backend
} // namespace blocktype
