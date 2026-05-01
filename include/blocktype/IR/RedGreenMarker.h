#ifndef BLOCKTYPE_IR_REDGREENMARKER_H
#define BLOCKTYPE_IR_REDGREENMARKER_H

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/DependencyGraph.h"
#include "blocktype/IR/QueryContext.h"

namespace blocktype {
namespace ir {

enum class MarkColor { Green, Red, Unknown };

class RedGreenMarker {
  QueryContext& QC;
  DependencyGraph& DG;
  DenseMap<QueryID, MarkColor> Marks;

public:
  explicit RedGreenMarker(QueryContext& Q);
  MarkColor tryMarkGreen(QueryID ID);
  MarkColor getMark(QueryID ID) const;
  void markRed(QueryID ID);
  void reset();
  size_t getGreenCount() const;
  size_t getRedCount() const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_REDGREENMARKER_H
