#ifndef BLOCKTYPE_IR_IRPASS_H
#define BLOCKTYPE_IR_IRPASS_H

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class IRModule;

/// Pass 抽象基类 — 所有 IR 层 Pass 的公共接口。
/// run() 的 bool 返回值语义由子类自行定义。
class Pass {
public:
  virtual ~Pass() = default;

  /// 返回 Pass 的标识名称，用于日志和调试。
  virtual StringRef getName() const = 0;

  /// 在 Module 上执行该 Pass。
  /// 返回值的语义由具体 Pass 定义（VerifierPass: true=通过; 优化Pass: true=修改了IR）。
  virtual bool run(IRModule& M) = 0;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_IRPASS_H
