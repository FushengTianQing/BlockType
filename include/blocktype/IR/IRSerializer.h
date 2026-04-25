#ifndef BLOCKTYPE_IR_IRSERIALIZER_H
#define BLOCKTYPE_IR_IRSERIALIZER_H

#include <cstdint>
#include <memory>
#include <string>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRModule;
class IRTypeContext;

// ============================================================================
// 序列化诊断
// ============================================================================

/// 序列化错误的类别
enum class SerializationErrorKind : uint8_t {
  Unknown          = 0,
  InvalidFormat    = 1,  // 格式不合法（语法错误、魔数不匹配等）
  InvalidType      = 2,  // 类型编码无法识别
  InvalidOpcode    = 3,  // 操作码无法识别
  InvalidValue     = 4,  // 值引用无法解析
  IOError          = 5,  // 文件 I/O 错误
  VersionMismatch  = 6,  // 版本不兼容
  TruncatedData    = 7,  // 数据截断（二进制格式不够长）
};

/// 单条序列化诊断信息
struct SerializationDiagnostic {
  SerializationErrorKind Kind = SerializationErrorKind::Unknown;
  unsigned Line = 0;     // 文本格式的行号（二进制格式为 0）
  unsigned Column = 0;   // 文本格式的列号（二进制格式为 0）
  std::string Message;

  SerializationDiagnostic() = default;
  SerializationDiagnostic(SerializationErrorKind K, const std::string& Msg,
                          unsigned L = 0, unsigned C = 0)
    : Kind(K), Line(L), Column(C), Message(Msg) {}
};

// ============================================================================
// IRWriter — 序列化器
// ============================================================================

/// IR 文本/二进制序列化写入器。
///
/// 使用方式：
///   std::string Text;
///   raw_string_ostream OS(Text);
///   bool OK = IRWriter::writeText(Module, OS);
class IRWriter {
public:
  /// 将 IRModule 序列化为人类可读的文本格式（BTIR text format）。
  /// @param M  要序列化的模块
  /// @param OS 输出流
  /// @return true=成功, false=失败
  static bool writeText(const IRModule& M, raw_ostream& OS);

  /// 将 IRModule 序列化为紧凑的二进制格式（BTIR binary format）。
  /// @param M  要序列化的模块
  /// @param OS 输出流
  /// @return true=成功, false=失败
  static bool writeBitcode(const IRModule& M, raw_ostream& OS);
};

// ============================================================================
// IRReader — 反序列化器
// ============================================================================

/// IR 文本/二进制反序列化读取器。
///
/// 使用方式：
///   SerializationDiagnostic Diag;
///   auto M = IRReader::parseText(Text, TCtx, &Diag);
///   if (!M) { /* 检查 Diag */ }
class IRReader {
public:
  /// 从文本格式字符串解析 IRModule。
  /// @param Text  BTIR 文本格式的字符串
  /// @param Ctx   类型上下文（用于创建类型实例，调用者保证生命周期）
  /// @param Diag  可选的错误收集器
  /// @return 解析成功返回 IRModule，失败返回 nullptr
  static std::unique_ptr<IRModule> parseText(
      StringRef Text, IRTypeContext& Ctx,
      SerializationDiagnostic* Diag = nullptr);

  /// 从二进制格式数据解析 IRModule。
  /// @param Data  BTIR 二进制格式的数据
  /// @param Ctx   类型上下文（调用者保证生命周期）
  /// @param Diag  可选的错误收集器
  /// @return 解析成功返回 IRModule，失败返回 nullptr
  static std::unique_ptr<IRModule> parseBitcode(
      StringRef Data, IRTypeContext& Ctx,
      SerializationDiagnostic* Diag = nullptr);

  /// 从文件读取并自动检测格式（文本或二进制）。
  /// 检测逻辑：读取文件前 4 字节，若为 "BTIR" 则按二进制格式解析，否则按文本格式。
  /// @param Path 文件路径
  /// @param Ctx  类型上下文（调用者保证生命周期）
  /// @param Diag 可选的错误收集器
  /// @return 解析成功返回 IRModule，失败返回 nullptr
  static std::unique_ptr<IRModule> readFile(
      StringRef Path, IRTypeContext& Ctx,
      SerializationDiagnostic* Diag = nullptr);
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRSERIALIZER_H
