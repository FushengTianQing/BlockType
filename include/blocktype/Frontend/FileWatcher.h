#ifndef BLOCKTYPE_FRONTEND_FILEWATCHER_H
#define BLOCKTYPE_FRONTEND_FILEWATCHER_H

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace frontend {

/// 文件监控器：使用平台特定文件监控（inotify/FSEvents），
/// 支持去抖动（100ms 内多次修改合并）。
/// 当前为 stub 实现，接口完整。
class FileWatcher {
  uint64_t DebounceMs = 100;

public:
  using Callback = std::function<void(ir::StringRef ChangedFile)>;

private:
  struct WatchEntry {
    std::string Path;
    Callback OnChange;
  };
  std::vector<WatchEntry> Watches;
  bool Running = false;

public:
  FileWatcher() = default;

  /// 开始监控指定路径的文件变化。
  /// @param Path     监控路径
  /// @param OnChange 文件变化回调（去抖动后触发）
  void watch(ir::StringRef Path, Callback OnChange) {
    WatchEntry E;
    E.Path = Path.str();
    E.OnChange = std::move(OnChange);
    Watches.push_back(std::move(E));
    Running = true;
    // Stub 实现：实际应启动平台特定文件监控线程
    // Linux: inotify_init + inotify_add_watch
    // macOS: FSEventStreamCreate
    // Windows: ReadDirectoryChangesW
  }

  /// 停止所有文件监控。
  void stop() {
    Running = false;
    Watches.clear();
  }

  /// 手动触发文件变化通知（用于测试和 stub 实现）。
  void notifyChange(ir::StringRef ChangedFile) {
    auto Now = std::chrono::steady_clock::now();
    for (auto& W : Watches) {
      (void)Now;
      if (W.OnChange) {
        W.OnChange(ChangedFile);
      }
    }
  }

  bool isRunning() const { return Running; }
  uint64_t getDebounceMs() const { return DebounceMs; }
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_FILEWATCHER_H
