#ifndef BLOCKTYPE_FRONTEND_SMARTRECOMPILER_H
#define BLOCKTYPE_FRONTEND_SMARTRECOMPILER_H

#include "blocktype/Frontend/FileWatcher.h"
#include "blocktype/IR/ProjectionQuery.h"
#include "blocktype/IR/QueryContext.h"

namespace blocktype {
namespace frontend {

/// 智能重编译器：监控文件变化后仅重编译修改的函数。
/// 通过 ProjectionQuery 投影受影响的函数，实现增量编译。
class SmartRecompiler {
  ir::QueryContext& QC;
  ir::ProjectionQuery& PQ;
  FileWatcher& FW;

public:
  SmartRecompiler(ir::QueryContext& Q, ir::ProjectionQuery& P, FileWatcher& F)
      : QC(Q), PQ(P), FW(F) {}

  /// 启动智能重编译：监控 SourceDir 下的文件变化，
  /// 变化时仅重编译受影响的函数。
  void start(ir::StringRef SourceDir) {
    FW.watch(SourceDir, [this](ir::StringRef ChangedFile) {
      (void)ChangedFile;
      // Stub 实现：实际应解析变更文件，提取修改的函数，
      // 通过 PQ.projectFunction 仅重编译受影响的函数
    });
  }

  /// 停止智能重编译。
  void stop() { FW.stop(); }
};

} // namespace frontend
} // namespace blocktype

#endif // BLOCKTYPE_FRONTEND_SMARTRECOMPILER_H
