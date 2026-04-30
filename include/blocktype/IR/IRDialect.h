#ifndef BLOCKTYPE_IR_IRDIALECT_H
#define BLOCKTYPE_IR_IRDIALECT_H

#include <cstdint>

#include "blocktype/IR/IRType.h" // dialect::DialectID

namespace blocktype {
namespace ir {
namespace dialect {

/// DialectCapability — 位掩码类，表示一个后端支持的 Dialect 集合。
/// 与 BackendCapability 正交：BackendCapability 描述后端的平台能力，
/// DialectCapability 描述后端能处理的 IR Dialect 类型。
class DialectCapability {
  uint32_t SupportedDialects_ = 0;

public:
  /// 声明支持某个 Dialect
  void declareDialect(DialectID D) {
    SupportedDialects_ |= (1u << static_cast<uint8_t>(D));
  }

  /// 查询是否支持某个 Dialect
  bool hasDialect(DialectID D) const {
    return (SupportedDialects_ & (1u << static_cast<uint8_t>(D))) != 0;
  }

  /// 查询是否支持所有必需的 Dialect（位掩码形式）
  bool supportsAll(uint32_t Required) const {
    return (SupportedDialects_ & Required) == Required;
  }

  /// 返回不支持的 Dialect 位掩码
  uint32_t getUnsupported(uint32_t Required) const {
    return Required & ~SupportedDialects_;
  }

  /// 返回完整的能力位掩码
  uint32_t getSupportedMask() const { return SupportedDialects_; }
};

/// 预定义的后端 Dialect 能力
namespace BackendDialectCaps {

/// LLVM 后端：支持全部 5 种 Dialect
inline DialectCapability LLVM() {
  DialectCapability Cap;
  Cap.declareDialect(DialectID::Core);
  Cap.declareDialect(DialectID::Cpp);
  Cap.declareDialect(DialectID::Target);
  Cap.declareDialect(DialectID::Debug);
  Cap.declareDialect(DialectID::Metadata);
  return Cap;
}

/// Cranelift 后端：仅支持 Core + Debug
inline DialectCapability Cranelift() {
  DialectCapability Cap;
  Cap.declareDialect(DialectID::Core);
  Cap.declareDialect(DialectID::Debug);
  return Cap;
}

} // namespace BackendDialectCaps

} // namespace dialect

/// 获取 DialectID 的字符串名称
inline const char* getDialectName(dialect::DialectID D) {
  switch (D) {
    case dialect::DialectID::Core:     return "Core";
    case dialect::DialectID::Cpp:      return "Cpp";
    case dialect::DialectID::Target:   return "Target";
    case dialect::DialectID::Debug:    return "Debug";
    case dialect::DialectID::Metadata: return "Metadata";
  }
  return "Unknown";
}

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRDIALECT_H
