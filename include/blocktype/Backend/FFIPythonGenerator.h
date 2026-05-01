#ifndef BLOCKTYPE_BACKEND_FFIPYTHONGENERATOR_H
#define BLOCKTYPE_BACKEND_FFIPYTHONGENERATOR_H

#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRFFI.h"

namespace blocktype {
namespace ir {
class IRTypeContext;
class IRModule;
class IRType;
} // namespace ir

namespace backend {

/// FFI Python 绑定自动生成器：根据 IRModule 生成 Python ctypes 绑定代码。
/// 内部委托 ffi::FFITypeMapper::mapToExternalType(T, "Python") 进行类型映射。
class FFIPythonGenerator {
  ir::IRTypeContext& Ctx;

public:
  explicit FFIPythonGenerator(ir::IRTypeContext& C);

  /// 根据 IRModule 生成完整的 Python ctypes 绑定代码。
  std::string generateBindings(const ir::IRModule& M);

  /// 将 IR 类型映射为 Python ctypes 类型名。
  /// 内部委托 ffi::FFITypeMapper::mapToExternalType(T, "Python")
  std::string mapIRTypeToPython(ir::IRType* T);
};

} // namespace backend
} // namespace blocktype

#endif // BLOCKTYPE_BACKEND_FFIPYTHONGENERATOR_H
