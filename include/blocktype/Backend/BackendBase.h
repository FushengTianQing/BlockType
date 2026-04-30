#pragma once
#include "blocktype/Basic/Diagnostics.h"
#include "blocktype/IR/BackendCapability.h"  // ir::BackendCapability
#include "blocktype/IR/IRModule.h"           // ir::IRModule, ir::IRFeature
#include "blocktype/IR/ADT.h"
#include "blocktype/Backend/BackendOptions.h"
#include <functional>
#include <memory>

namespace blocktype::backend {

/// BackendBase — 所有后端的抽象基类
/// 对称于 FrontendBase（前端基类）
class BackendBase {
protected:
  BackendOptions Opts;
  DiagnosticsEngine& Diags;

public:
  BackendBase(const BackendOptions& Opts, DiagnosticsEngine& Diags);
  virtual ~BackendBase() = default;

  // 禁止拷贝
  BackendBase(const BackendBase&) = delete;
  BackendBase& operator=(const BackendBase&) = delete;

  /// 获取后端名称（如 "llvm", "cranelift"）
  virtual ir::StringRef getName() const = 0;

  /// 将 IRModule 编译为目标文件
  virtual bool emitObject(ir::IRModule& IRModule, ir::StringRef OutputPath) = 0;

  /// 将 IRModule 编译为汇编文件
  virtual bool emitAssembly(ir::IRModule& IRModule, ir::StringRef OutputPath) = 0;

  /// 将 IRModule 输出为 IR 文本
  virtual bool emitIRText(ir::IRModule& IRModule, ir::raw_ostream& OS) = 0;

  /// 检查后端是否能处理指定 TargetTriple
  /// 与 01-总体架构设计.md §1.4.1 一致
  virtual bool canHandle(ir::StringRef TargetTriple) const = 0;

  /// 优化 IRModule（后端可选实现）
  /// 与 01-总体架构设计.md §1.4.1 一致
  virtual bool optimize(ir::IRModule& IRModule) = 0;

  /// 获取后端能力声明
  /// 复用 ir::BackendCapability（Phase A 已实现）
  virtual ir::BackendCapability getCapability() const = 0;

  /// 访问选项
  const BackendOptions& getOptions() const { return Opts; }

  /// 访问诊断引擎
  DiagnosticsEngine& getDiagnostics() const { return Diags; }
};

/// BackendFactory — 后端工厂函数类型
using BackendFactory = std::function<std::unique_ptr<BackendBase>(
  const BackendOptions&, DiagnosticsEngine&)>;

} // namespace blocktype::backend
