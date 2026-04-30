#pragma once
#include "blocktype/IR/ADT.h"

namespace blocktype::ir { class IRModule; }

namespace blocktype::backend {

/// 调试信息发射器抽象基类
class DebugInfoEmitter {
public:
  virtual ~DebugInfoEmitter() = default;

  /// 发射调试信息（默认格式由 BackendOptions::DebugInfoFormat 决定）
  virtual bool emit(const ir::IRModule& M, ir::raw_ostream& OS) = 0;

  /// 发射 DWARF5 格式调试信息
  virtual bool emitDWARF5(const ir::IRModule& M, ir::raw_ostream& OS) = 0;

  /// 发射 DWARF4 格式调试信息
  virtual bool emitDWARF4(const ir::IRModule& M, ir::raw_ostream& OS) = 0;

  /// 发射 CodeView 格式调试信息（Windows，远期）
  virtual bool emitCodeView(const ir::IRModule& M, ir::raw_ostream& OS) = 0;
};

/// DWARF5 调试信息发射器
class DWARF5Emitter : public DebugInfoEmitter {
public:
  bool emit(const ir::IRModule& M, ir::raw_ostream& OS) override;
  bool emitDWARF5(const ir::IRModule& M, ir::raw_ostream& OS) override;
  bool emitDWARF4(const ir::IRModule& M, ir::raw_ostream& OS) override;
  bool emitCodeView(const ir::IRModule& M, ir::raw_ostream& OS) override;
};

/// CodeView 调试信息发射器（Windows 后端，stub）
class CodeViewEmitter : public DebugInfoEmitter {
public:
  bool emit(const ir::IRModule& M, ir::raw_ostream& OS) override;
  bool emitDWARF5(const ir::IRModule& M, ir::raw_ostream& OS) override;
  bool emitDWARF4(const ir::IRModule& M, ir::raw_ostream& OS) override;
  bool emitCodeView(const ir::IRModule& M, ir::raw_ostream& OS) override;
};

} // namespace blocktype::backend
