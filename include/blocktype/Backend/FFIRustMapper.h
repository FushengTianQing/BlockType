#ifndef BLOCKTYPE_BACKEND_FFIRUSTMAPPER_H
#define BLOCKTYPE_BACKEND_FFIRUSTMAPPER_H

#include <string>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRFFI.h"

namespace blocktype {
namespace ir {
class IRType;
class IRTypeContext;
} // namespace ir

namespace backend {

/// Rust FFI 类型映射器：将 Rust 类型映射为 IR 类型。
/// 继承 ffi::FFITypeMapper 以支持通过基类统一调用。
///
/// 支持的映射：
///   repr(C) struct → IRStructType（零开销映射）
///   repr(Rust) struct → IROpaqueType（发警告）
///   i32/u32 → IRIntegerType(32)
///   i64/u64 → IRIntegerType(64)
///   f32 → IRFloatType(32)
///   f64 → IRFloatType(64)
///   *const T/*mut T → IRPointerType(mapRustType(T))
///   Option<T> → 不支持，报错
///   Result<T, E> → 不支持，报错
class FFIRustMapper : public ir::ffi::FFITypeMapper {
public:
  /// 将 Rust 类型名映射为 IRType。
  /// @param RustTypeName Rust 类型名（如 "i32", "f64", "*const i32", "repr(C) Foo"）
  /// @param Ctx          IR 类型上下文
  /// @return 对应的 IRType，或 nullptr 如果无法映射
  static ir::IRType* mapRustType(ir::StringRef RustTypeName, ir::IRTypeContext& Ctx);

  /// 将 IRType 映射为 Rust 类型名。
  /// @param T IR 类型
  /// @return Rust 类型名字符串
  static std::string mapToRustType(const ir::IRType* T);

  /// 检查 Rust 类型是否受支持。
  static bool isSupportedRustType(ir::StringRef RustTypeName);
};

} // namespace backend
} // namespace blocktype

#endif // BLOCKTYPE_BACKEND_FFIRUSTMAPPER_H
