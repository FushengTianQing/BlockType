#ifndef BLOCKTYPE_BACKEND_FFIWASMMAPPER_H
#define BLOCKTYPE_BACKEND_FFIWASMMAPPER_H

#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRFFI.h"

namespace blocktype {
namespace ir {
class IRType;
class IRTypeContext;
} // namespace ir

namespace backend {

/// WASM FFI 类型映射器：将 WASM 值类型映射为 IR 类型。
/// 继承 ffi::FFITypeMapper 以支持通过基类统一调用。
///
/// 映射规则：
///   i32       → IRIntegerType(32)
///   i64       → IRIntegerType(64)
///   f32       → IRFloatType(32)
///   f64       → IRFloatType(64)
///   externref → IRPointerType(IRVoidType)
class FFIWasmMapper : public ir::ffi::FFITypeMapper {
public:
  /// 将 WASM 值类型名映射为 IRType。
  /// @param WasmTypeName WASM 类型名（"i32", "i64", "f32", "f64", "externref"）
  /// @param Ctx          IR 类型上下文
  /// @return 对应的 IRType，或 nullptr 如果无法映射
  static ir::IRType* mapWasmType(ir::StringRef WasmTypeName, ir::IRTypeContext& Ctx);

  /// 将 IRType 映射为 WASM 值类型名。
  /// @param T IR 类型
  /// @return WASM 类型名字符串
  static std::string mapToWasmType(const ir::IRType* T);

  /// 检查 WASM 类型是否受支持。
  static bool isSupportedWasmType(ir::StringRef WasmTypeName);
};

} // namespace backend
} // namespace blocktype

#endif // BLOCKTYPE_BACKEND_FFIWASMMAPPER_H
