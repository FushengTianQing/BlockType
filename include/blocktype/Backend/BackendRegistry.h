#pragma once
#include "blocktype/Backend/BackendBase.h"
#include "blocktype/IR/ADT.h"
#include <cassert>

namespace blocktype::backend {

/// BackendRegistry — 全局单例后端注册表
/// 对称于 FrontendRegistry
class BackendRegistry {
  ir::StringMap<BackendFactory> Registry;
  ir::StringMap<std::string> NameToTriple;  // 后端名→默认Triple映射
  BackendRegistry() = default;

public:
  static BackendRegistry& instance();

  // 禁止拷贝/移动
  BackendRegistry(const BackendRegistry&) = delete;
  BackendRegistry& operator=(const BackendRegistry&) = delete;

  /// 注册后端工厂函数
  void registerBackend(ir::StringRef Name, BackendFactory Factory);

  /// 创建后端实例
  std::unique_ptr<BackendBase> create(
    ir::StringRef Name, const BackendOptions& Opts, DiagnosticsEngine& Diags);

  /// 根据 IRModule 的 TargetTriple 自动选择后端
  std::unique_ptr<BackendBase> autoSelect(
    const ir::IRModule& M, const BackendOptions& Opts, DiagnosticsEngine& Diags);

  /// 查询后端是否已注册
  bool hasBackend(ir::StringRef Name) const;

  /// 获取所有已注册后端名称
  /// 注意：返回 std::string（值语义），避免 StringRef 悬空风险
  ir::SmallVector<std::string, 4> getRegisteredNames() const;
};

} // namespace blocktype::backend
