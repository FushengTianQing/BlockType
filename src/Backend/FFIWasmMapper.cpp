#include "blocktype/Backend/FFIWasmMapper.h"
#include "blocktype/IR/IRType.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/ADT/raw_ostream.h"

namespace blocktype {
namespace backend {

using ir::raw_ostream;
using ir::StringRef;

// ============================================================================
// 辅助：去除字符串两端空白
// ============================================================================
static std::string trimStr(ir::StringRef S) {
  size_t Start = 0;
  while (Start < S.size() && (S[Start] == ' ' || S[Start] == '\t'))
    ++Start;
  size_t End = S.size();
  while (End > Start && (S[End - 1] == ' ' || S[End - 1] == '\t'))
    --End;
  return S.slice(Start, End - Start).str();
}

// ============================================================================
// mapWasmType 实现
// ============================================================================

ir::IRType* FFIWasmMapper::mapWasmType(StringRef WasmTypeName,
                                        ir::IRTypeContext& Ctx) {
  std::string Name = trimStr(WasmTypeName);

  if (Name == "i32") {
    return Ctx.getInt32Ty();
  }
  if (Name == "i64") {
    return Ctx.getInt64Ty();
  }
  if (Name == "f32") {
    return Ctx.getFloatTy();
  }
  if (Name == "f64") {
    return Ctx.getDoubleTy();
  }
  if (Name == "externref") {
    return Ctx.getPointerType(static_cast<ir::IRType*>(Ctx.getVoidType()));
  }

  // 不支持的 WASM 类型
  ir::errs() << "FFIWasmMapper: unsupported WASM type: " << Name << "\n";
  return nullptr;
}

// ============================================================================
// mapToWasmType 实现
// ============================================================================

std::string FFIWasmMapper::mapToWasmType(const ir::IRType* T) {
  if (!T) return "i32"; // 默认

  switch (T->getKind()) {
    case ir::IRType::Integer: {
      auto* IntTy = static_cast<const ir::IRIntegerType*>(T);
      switch (IntTy->getBitWidth()) {
        case 32: return "i32";
        case 64: return "i64";
        default:
          // 非 32/64 位整数无法直接映射到 WASM 值类型
          ir::errs() << "FFIWasmMapper: cannot map integer bitwidth "
                     << IntTy->getBitWidth() << " to WASM type\n";
          return "i32"; // 回退
      }
    }
    case ir::IRType::Float: {
      auto* FloatTy = static_cast<const ir::IRFloatType*>(T);
      switch (FloatTy->getBitWidth()) {
        case 32: return "f32";
        case 64: return "f64";
        default:
          ir::errs() << "FFIWasmMapper: cannot map float bitwidth "
                     << FloatTy->getBitWidth() << " to WASM type\n";
          return "f64"; // 回退
      }
    }
    case ir::IRType::Pointer:
      return "externref";
    case ir::IRType::Void:
      return ""; // WASM 没有显式 void 类型
    default:
      ir::errs() << "FFIWasmMapper: cannot map IR type kind "
                 << static_cast<int>(T->getKind()) << " to WASM type\n";
      return "i32"; // 回退
  }
}

// ============================================================================
// isSupportedWasmType 实现
// ============================================================================

bool FFIWasmMapper::isSupportedWasmType(ir::StringRef WasmTypeName) {
  std::string Name = trimStr(WasmTypeName);
  return Name == "i32" || Name == "i64" ||
         Name == "f32" || Name == "f64" ||
         Name == "externref";
}

} // namespace backend
} // namespace blocktype
