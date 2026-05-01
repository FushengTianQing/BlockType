#ifndef BLOCKTYPE_IR_QUERYCACHESERIALIZER_H
#define BLOCKTYPE_IR_QUERYCACHESERIALIZER_H

#include "blocktype/IR/ADT.h"

namespace blocktype {
namespace ir {

class QueryContext;

class QueryCacheSerializer {
  StringRef CacheDir;
public:
  explicit QueryCacheSerializer(StringRef Dir);
  bool save(const QueryContext& QC, StringRef ModuleName);
  bool load(QueryContext& QC, StringRef ModuleName);
  bool isValid(StringRef ModuleName) const;
  bool invalidate(StringRef ModuleName);
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_QUERYCACHESERIALIZER_H
