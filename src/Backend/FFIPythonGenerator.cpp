#include "blocktype/Backend/FFIPythonGenerator.h"

#include <sstream>

#include "blocktype/IR/IRModule.h"
#include "blocktype/IR/IRType.h"

namespace blocktype {
namespace backend {

FFIPythonGenerator::FFIPythonGenerator(ir::IRTypeContext& C) : Ctx(C) {}

std::string FFIPythonGenerator::mapIRTypeToPython(ir::IRType* T) {
  return ir::ffi::FFITypeMapper::mapToExternalType(T, "Python");
}

std::string FFIPythonGenerator::generateBindings(const ir::IRModule& M) {
  std::ostringstream OS;

  // Python ctypes 头部
  OS << "import ctypes\n";
  OS << "import os\n\n";

  // 加载共享库
  OS << "# Load BlockType shared library\n";
  OS << "_lib = ctypes.cdll.LoadLibrary(\n";
  OS << "    os.path.join(os.path.dirname(__file__), \"libblocktype.so\")\n";
  OS << ")\n\n";

  // 为每个函数生成绑定
  for (auto& F : M.getFunctions()) {
    auto Name = F->getName().str();
    auto* FT = F->getFunctionType();
    if (!FT)
      continue;

    // 返回类型映射
    std::string RetType = "None";
    if (FT->getReturnType()) {
      RetType = mapIRTypeToPython(FT->getReturnType());
      if (RetType.empty())
        RetType = "ctypes.c_void_p";
    }

    // 函数声明
    OS << "# " << Name << "\n";
    OS << "_" << Name << " = _lib." << Name << "\n";
    OS << "_" << Name << ".restype = " << RetType << "\n";

    // 参数类型
    OS << "_" << Name << ".argtypes = [";
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
      auto* ParamTy = FT->getParamType(i);
      std::string ParamStr = mapIRTypeToPython(ParamTy);
      if (ParamStr.empty())
        ParamStr = "ctypes.c_void_p";
      OS << ParamStr;
      if (i + 1 < FT->getNumParams())
        OS << ", ";
    }
    OS << "]\n\n";

    // Python 包装函数
    OS << "def " << Name << "(";
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
      OS << "arg" << i;
      if (i + 1 < FT->getNumParams())
        OS << ", ";
    }
    OS << "):\n";
    OS << "    \"\"\"FFI binding for " << Name << ".\"\"\"\n";
    OS << "    return _" << Name << "(";
    for (unsigned i = 0; i < FT->getNumParams(); ++i) {
      OS << "arg" << i;
      if (i + 1 < FT->getNumParams())
        OS << ", ";
    }
    OS << ")\n\n";
  }

  return OS.str();
}

} // namespace backend
} // namespace blocktype
