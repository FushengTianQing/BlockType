#pragma once
#include "blocktype/Backend/RegisterAllocator.h"  // TargetFunction
#include "blocktype/Backend/BackendOptions.h"
#include "blocktype/IR/ADT.h"

namespace blocktype::backend {

/// 代码发射器抽象基类
class CodeEmitter {
public:
  virtual ~CodeEmitter() = default;

  /// 发射单个函数的目标代码
  virtual bool emit(const TargetFunction& F,
                    const BackendOptions& Opts,
                    ir::raw_ostream& OS) = 0;

  /// 发射整个模块的目标代码
  virtual bool emitModule(const ir::SmallVector<TargetFunction, 16>& Functions,
                          const BackendOptions& Opts,
                          ir::raw_ostream& OS) = 0;
};

} // namespace blocktype::backend
