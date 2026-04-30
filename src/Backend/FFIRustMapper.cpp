#include "blocktype/Backend/FFIRustMapper.h"
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
// mapRustType 实现
// ============================================================================

ir::IRType* FFIRustMapper::mapRustType(StringRef RustTypeName,
                                        ir::IRTypeContext& Ctx) {
  std::string Name = trimStr(RustTypeName);

  // 检查不支持的类型
  if (Name.find("Option<") != std::string::npos) {
    ir::errs() << "FFIRustMapper: Option<T> is not supported: " << Name << "\n";
    return nullptr;
  }
  if (Name.find("Result<") != std::string::npos) {
    ir::errs() << "FFIRustMapper: Result<T, E> is not supported: " << Name << "\n";
    return nullptr;
  }

  // repr(Rust) 结构体 → IROpaqueType（不支持直接映射，返回 opaque 并发警告）
  if (Name.find("repr(Rust)") != std::string::npos) {
    ir::errs() << "FFIRustMapper: repr(Rust) structs are mapped to IROpaqueType (no zero-cost guarantee): " << Name << "\n";
    // 提取结构体名
    size_t SpacePos = Name.find(' ', 11); // "repr(Rust) " 之后
    if (SpacePos == std::string::npos) SpacePos = 11;
    std::string StructName = Name.substr(SpacePos);
    while (!StructName.empty() && StructName.front() == ' ')
      StructName.erase(StructName.begin());
    return Ctx.getOpaqueType(ir::StringRef(StructName));
  }

  // repr(C) 结构体 → IRStructType（零开销映射）
  if (Name.find("repr(C)") != std::string::npos) {
    // 提取结构体名
    size_t SpacePos = Name.find(' ', 8); // "repr(C) " 之后
    if (SpacePos == std::string::npos) SpacePos = 8;
    std::string StructName = Name.substr(SpacePos);
    while (!StructName.empty() && StructName.front() == ' ')
      StructName.erase(StructName.begin());
    // repr(C) 结构体创建为 IRStructType（空字段列表，后续可通过 setStructBody 设置）
    return Ctx.getStructType(ir::StringRef(StructName),
                              ir::SmallVector<ir::IRType*, 16>{});
  }

  // 基本整数类型
  if (Name == "i8" || Name == "u8")   return Ctx.getInt8Ty();
  if (Name == "i16" || Name == "u16") return Ctx.getInt16Ty();
  if (Name == "i32" || Name == "u32") return Ctx.getInt32Ty();
  if (Name == "i64" || Name == "u64") return Ctx.getInt64Ty();
  if (Name == "i128" || Name == "u128") return Ctx.getInt128Ty();
  if (Name == "isize" || Name == "usize") return Ctx.getInt64Ty(); // 64-bit 平台
  if (Name == "bool") return Ctx.getInt1Ty();

  // 浮点类型
  if (Name == "f32") return Ctx.getFloatTy();
  if (Name == "f64") return Ctx.getDoubleTy();

  // 指针类型: *const T 或 *mut T
  if (Name.find("*const ") == 0 || Name.find("*mut ") == 0) {
    // 提取指向的类型
    size_t SpacePos = Name.find(' ');
    if (SpacePos != std::string::npos) {
      std::string PointeeTypeName = Name.substr(SpacePos + 1);
      ir::IRType* PointeeType = mapRustType(StringRef(PointeeTypeName), Ctx);
      if (PointeeType) {
        return Ctx.getPointerType(PointeeType);
      }
    }
    return nullptr;
  }

  // 引用类型: &T 或 &mut T
  if (Name.find("&") == 0) {
    std::string RefTypeName = Name.substr(1);
    // 去除 "mut " 前缀
    if (RefTypeName.find("mut ") == 0) {
      RefTypeName = RefTypeName.substr(4);
    }
    ir::IRType* RefType = mapRustType(ir::StringRef(RefTypeName), Ctx);
    if (RefType) {
      return Ctx.getPointerType(RefType);
    }
    return nullptr;
  }

  // 未识别的类型
  ir::errs() << "FFIRustMapper: unsupported Rust type: " << Name << "\n";
  return nullptr;
}

// ============================================================================
// mapToRustType 实现
// ============================================================================

std::string FFIRustMapper::mapToRustType(const ir::IRType* T) {
  if (!T) return "()";

  switch (T->getKind()) {
    case ir::IRType::Void:
      return "()";
    case ir::IRType::Bool:
      return "bool";
    case ir::IRType::Integer: {
      auto* IntTy = static_cast<const ir::IRIntegerType*>(T);
      switch (IntTy->getBitWidth()) {
        case 1: return "bool";
        case 8: return "u8";
        case 16: return "u16";
        case 32: return "u32";
        case 64: return "u64";
        case 128: return "u128";
        default: return "u" + std::to_string(IntTy->getBitWidth());
      }
    }
    case ir::IRType::Float: {
      auto* FloatTy = static_cast<const ir::IRFloatType*>(T);
      switch (FloatTy->getBitWidth()) {
        case 32: return "f32";
        case 64: return "f64";
        default: return "f" + std::to_string(FloatTy->getBitWidth());
      }
    }
    case ir::IRType::Pointer: {
      auto* PtrTy = static_cast<const ir::IRPointerType*>(T);
      return "*const " + mapToRustType(PtrTy->getPointeeType());
    }
    case ir::IRType::Struct:
      return static_cast<const ir::IRStructType*>(T)->getName().str();
    case ir::IRType::Opaque:
      return static_cast<const ir::IROpaqueType*>(T)->getName().str();
    default:
      return "()";
  }
}

// ============================================================================
// isSupportedRustType 实现
// ============================================================================

bool FFIRustMapper::isSupportedRustType(StringRef RustTypeName) {
  std::string Name = trimStr(RustTypeName);

  // 不支持的类型
  if (Name.find("Option<") != std::string::npos) return false;
  if (Name.find("Result<") != std::string::npos) return false;
  if (Name.find("repr(Rust)") != std::string::npos) return false;

  // 基本类型
  if (Name == "i8" || Name == "u8" || Name == "i16" || Name == "u16" ||
      Name == "i32" || Name == "u32" || Name == "i64" || Name == "u64" ||
      Name == "i128" || Name == "u128" || Name == "isize" || Name == "usize" ||
      Name == "bool" || Name == "f32" || Name == "f64") {
    return true;
  }

  // 指针类型
  if (Name.find("*const ") == 0 || Name.find("*mut ") == 0) {
    return true;
  }

  // 引用类型
  if (Name.find("&") == 0) {
    return true;
  }

  // repr(C) 结构体
  if (Name.find("repr(C)") != std::string::npos) {
    return true;
  }

  return false;
}

} // namespace backend
} // namespace blocktype
