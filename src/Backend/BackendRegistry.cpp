#include "blocktype/Backend/BackendRegistry.h"

#include <cassert>

namespace blocktype::backend {

BackendRegistry& BackendRegistry::instance() {
  static BackendRegistry Inst;
  return Inst;
}

void BackendRegistry::registerBackend(ir::StringRef Name, BackendFactory Factory) {
  std::string Key = Name.str();
  Registry.insert({std::move(Key), std::move(Factory)});
}

std::unique_ptr<BackendBase> BackendRegistry::create(
    ir::StringRef Name, const BackendOptions& Opts, DiagnosticsEngine& Diags) {
  auto It = Registry.find(Name.str());
  if (It == Registry.end())
    return nullptr;
  return It->Value(Opts, Diags);
}

std::unique_ptr<BackendBase> BackendRegistry::autoSelect(
    const ir::IRModule& M, const BackendOptions& Opts, DiagnosticsEngine& Diags) {
  // 根据 IRModule 的 TargetTriple 选择后端
  // 默认选 "llvm"（LLVM 后端支持所有已知 TargetTriple）
  ir::StringRef Triple = M.getTargetTriple();

  // 遍历 NameToTriple 映射查找匹配
  for (auto& Entry : NameToTriple) {
    if (Triple.startswith(Entry.Value) || Triple == Entry.Value) {
      return create(Entry.Key, Opts, Diags);
    }
  }

  // 默认使用 LLVM 后端
  return create("llvm", Opts, Diags);
}

bool BackendRegistry::hasBackend(ir::StringRef Name) const {
  return Registry.contains(Name.str());
}

ir::SmallVector<std::string, 4> BackendRegistry::getRegisteredNames() const {
  ir::SmallVector<std::string, 4> Names;
  for (const auto& Entry : Registry)
    Names.push_back(Entry.Key);
  return Names;
}

} // namespace blocktype::backend
