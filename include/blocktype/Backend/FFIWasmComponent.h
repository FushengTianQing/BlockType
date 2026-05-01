#ifndef BLOCKTYPE_BACKEND_FFIWASMCOMPONENT_H
#define BLOCKTYPE_BACKEND_FFIWASMCOMPONENT_H

#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRFFI.h"

namespace blocktype {
namespace ir {
class IRTypeContext;
class IRModule;
} // namespace ir

namespace backend {

/// FFI WASM Component Model 生成器：根据 IRModule 生成 WASM Component Model 绑定代码。
/// 内部使用 FFIWasmMapper 和 ffi::CallingConvention::WASM。
class FFIWasmComponent {
  ir::IRTypeContext& Ctx;

public:
  explicit FFIWasmComponent(ir::IRTypeContext& C);

  /// 生成 WASM Component Model 绑定代码。
  std::string generate(const ir::IRModule& M);
};

} // namespace backend
} // namespace blocktype

#endif // BLOCKTYPE_BACKEND_FFIWASMCOMPONENT_H
