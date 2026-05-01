#ifndef BLOCKTYPE_IR_PROJECTIONQUERY_H
#define BLOCKTYPE_IR_PROJECTIONQUERY_H

#include <memory>

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class QueryContext;
class IRModule;
class IRFunction;

class ProjectionQuery {
  QueryContext& QC;
public:
  explicit ProjectionQuery(QueryContext& Q);
  std::unique_ptr<IRModule> projectFunction(const IRModule& M, StringRef FunctionName);
  SmallVector<IRFunction*, 16> getModifiedFunctions(const IRModule& M);
  bool canReuseCompilation(const IRFunction& F);
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_PROJECTIONQUERY_H
