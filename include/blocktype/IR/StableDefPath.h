#ifndef BLOCKTYPE_IR_STABLEDEFPATH_H
#define BLOCKTYPE_IR_STABLEDEFPATH_H

#include "blocktype/IR/ADT.h"

#include <cstdint>
#include <optional>

namespace blocktype {
namespace ir {

/// 不透明声明标识符，避免 IR 层直接依赖 AST 的 Decl 类型。
using StableDeclId = uint64_t;

class StableDefPath {
  std::string Path;  // 如 "std::vector::push_back"
  __uint128_t StableHash = 0;
public:
  StableDefPath() = default;
  explicit StableDefPath(StringRef P);
  StringRef getPath() const { return Path; }
  __uint128_t getStableHash() const { return StableHash; }
  bool operator==(const StableDefPath& Other) const { return StableHash == Other.StableHash; }
};

class StableIdMap {
  DenseMap<StableDeclId, StableDefPath> IdToPath;
  DenseMap<__uint128_t, StableDeclId> HashToId;
public:
  void registerDecl(StableDeclId Id, StringRef Path);
  std::optional<StableDefPath> lookupById(StableDeclId Id) const;
  std::optional<StableDeclId> lookupByHash(__uint128_t Hash) const;
};

} // namespace ir
} // namespace blocktype

#endif // BLOCKTYPE_IR_STABLEDEFPATH_H
