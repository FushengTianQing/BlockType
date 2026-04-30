#include "blocktype/IR/QueryContext.h"
#include "blocktype/IR/IRTypeContext.h"
#include "blocktype/IR/IRModule.h"

namespace blocktype {
namespace ir {

QueryContext::QueryContext(IRTypeContext& Ctx)
  : TypeCtx(Ctx), TargetModule(nullptr) {}

QueryResult QueryContext::query(QueryID ID, std::function<QueryResult()> Compute) {
  // 先查缓存
  auto It = Cache.find(ID);
  if (It != Cache.end()) {
    return (*It).second;
  }

  // 未命中：计算
  QueryResult Result = Compute();

  // 存入缓存
  Cache.insert({ID, Result});

  return Result;
}

void QueryContext::invalidate(QueryID ID) {
  // 获取所有传递依赖者
  auto TransitiveDeps = DepGraph.getTransitiveDependents(ID);

  // 失效自身
  Cache.erase(ID);

  // 失效所有传递依赖者
  for (QueryID DepID : TransitiveDeps) {
    Cache.erase(DepID);
  }
}

void QueryContext::invalidateAll() {
  Cache.clear();
}

} // namespace ir
} // namespace blocktype
