#ifndef BLOCKTYPE_IR_IRFFI_H
#define BLOCKTYPE_IR_IRFFI_H

#include <memory>
#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRType;
class IRTypeContext;
class IRFunctionType;
class IRFunction;
class IRModule;

namespace ffi {

// ============================================================================
// FFI CallingConvention — 外部函数调用约定
// ============================================================================

/// FFI 专用调用约定。与 ir::CallingConvention 独立，
/// 仅包含外部 FFI 调用所需的 C ABI 相关约定。
enum class CallingConvention : uint8_t {
  C = 0,
  Stdcall = 1,
  Fastcall = 2,
  VectorCall = 3,
  WASM = 4,
  BTInternal = 5,
  ThisCall = 6,
  Swift = 7,
};

// ============================================================================
// FFITypeMapper — 外部语言类型 ↔ IR 类型映射
// ============================================================================

/// 提供外部语言类型与 IR 类型之间的双向映射。
class FFITypeMapper {
public:
  /// 将外部语言类型名映射为 IRType。
  /// @param Language 目标语言（"C", "Rust", "Python", "WASM", "Swift", "Zig"）
  /// @param TypeName 外部类型名（如 "int", "float", "char*" 等）
  /// @param Ctx      IR 类型上下文
  /// @return 对应的 IRType，或 nullptr 如果无法映射
  static IRType* mapExternalType(StringRef Language, StringRef TypeName,
                                  IRTypeContext& Ctx);

  /// 将 IRType 映射为外部语言类型名。
  /// @param T              IR 类型
  /// @param TargetLanguage 目标语言
  /// @return 外部语言类型名字符串
  static std::string mapToExternalType(const IRType* T, StringRef TargetLanguage);

  /// 返回支持的语言列表。
  static SmallVector<std::string, 8> getSupportedLanguages();
};

// ============================================================================
// FFIFunctionDecl — 外部函数声明
// ============================================================================

/// 描述一个外部函数的 FFI 声明信息。
class FFIFunctionDecl {
  std::string Name_;
  std::string MangledName_;
  CallingConvention Conv_;
  IRFunctionType* Signature_;
  std::string SourceLanguage_;
  std::string HeaderFile_;
  bool IsVariadic_;

public:
  FFIFunctionDecl(StringRef N, StringRef MN, CallingConvention C,
                  IRFunctionType* S, StringRef Lang,
                  StringRef HF = "", bool VA = false);

  StringRef getName() const { return Name_; }
  StringRef getMangledName() const { return MangledName_; }
  CallingConvention getCallingConvention() const { return Conv_; }
  IRFunctionType* getSignature() const { return Signature_; }
  StringRef getSourceLanguage() const { return SourceLanguage_; }
  StringRef getHeaderFile() const { return HeaderFile_; }
  bool isVariadic() const { return IsVariadic_; }

  /// 在 IRModule 中创建对应的 IRFunction 声明。
  IRFunction* createIRDeclaration(IRModule& M);

  /// 创建调用包装函数。在 IRModule 中生成一个 wrapper 函数，
  /// 该函数通过 FFI 调用目标外部函数。
  IRFunction* createCallWrapper(IRModule& M, const IRFunction& Caller);
};

// ============================================================================
// FFIModule — FFI 声明集合
// ============================================================================

/// 管理一组 FFI 函数声明。
class FFIModule {
  StringMap<std::unique_ptr<FFIFunctionDecl>> Declarations_;

public:
  /// 添加一个 FFI 声明。
  void addDeclaration(std::unique_ptr<FFIFunctionDecl> Decl);

  /// 从 C 头文件导入声明（占位实现）。
  bool importFromCHeader(StringRef HeaderPath);

  /// 从 Rust crate 导入声明（占位实现）。
  bool importFromRustCrate(StringRef CratePath);

  /// 将 IRFunction 导出为 C API（占位实现）。
  bool exportToCAPI(const IRFunction& F, StringRef ExportName);

  /// 生成目标语言的绑定代码（占位实现）。
  bool generateBindings(StringRef TargetLanguage, raw_ostream& OS);

  /// 按名称查找 FFI 声明。
  const FFIFunctionDecl* lookup(StringRef Name) const;

  /// 返回声明数量。
  unsigned getNumDeclarations() const;
};

} // namespace ffi
} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRFFI_H
