#ifndef BLOCKTYPE_IR_BACKENDCAPABILITY_H
#define BLOCKTYPE_IR_BACKENDCAPABILITY_H

#include "blocktype/IR/IRModule.h" // IRFeature 枚举

namespace blocktype {
namespace ir {

/// BackendCapability — 位掩码类，表示一个后端支持的 IR 特性集合。
/// 与 DialectCapability 正交：DialectCapability 描述后端能处理的 IR Dialect 类型，
/// BackendCapability 描述后端支持的 IR 特性（如浮点运算、异常处理等）。
class BackendCapability {
  uint32_t SupportedFeatures_ = 0;

public:
  void declareFeature(IRFeature F) { SupportedFeatures_ |= static_cast<uint32_t>(F); }
  bool hasFeature(IRFeature F) const { return (SupportedFeatures_ & static_cast<uint32_t>(F)) != 0; }
  bool supportsAll(uint32_t Required) const { return (SupportedFeatures_ & Required) == Required; }
  uint32_t getUnsupported(uint32_t Required) const { return Required & ~SupportedFeatures_; }
  uint32_t getSupportedMask() const { return SupportedFeatures_; }
};

/// 预定义的后端能力工厂函数
namespace BackendCaps {
  /// LLVM 后端：支持所有特性
  inline BackendCapability LLVM() {
    BackendCapability Cap;
    Cap.declareFeature(IRFeature::IntegerArithmetic);
    Cap.declareFeature(IRFeature::FloatArithmetic);
    Cap.declareFeature(IRFeature::VectorOperations);
    Cap.declareFeature(IRFeature::AtomicOperations);
    Cap.declareFeature(IRFeature::ExceptionHandling);
    Cap.declareFeature(IRFeature::DebugInfo);
    Cap.declareFeature(IRFeature::VarArg);
    Cap.declareFeature(IRFeature::SeparateFloatInt);
    Cap.declareFeature(IRFeature::StructReturn);
    Cap.declareFeature(IRFeature::DynamicCast);
    Cap.declareFeature(IRFeature::VirtualDispatch);
    Cap.declareFeature(IRFeature::Coroutines);
    return Cap;
  }

  /// Cranelift 后端：仅支持基础特性
  inline BackendCapability Cranelift() {
    BackendCapability Cap;
    Cap.declareFeature(IRFeature::IntegerArithmetic);
    Cap.declareFeature(IRFeature::FloatArithmetic);
    Cap.declareFeature(IRFeature::VectorOperations);
    return Cap;
  }
} // namespace BackendCaps

} // namespace ir
} // namespace blocktype

#endif
