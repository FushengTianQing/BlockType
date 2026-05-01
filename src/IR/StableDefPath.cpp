#include "blocktype/IR/StableDefPath.h"

#include <functional>

// Provide std::hash specialization for __uint128_t so DenseMap can hash it.
namespace std {
template <>
struct hash<__uint128_t> {
  size_t operator()(const __uint128_t& V) const {
    uint64_t Lo = static_cast<uint64_t>(V);
    uint64_t Hi = static_cast<uint64_t>(V >> 64);
    size_t H = std::hash<uint64_t>()(Lo);
    H ^= std::hash<uint64_t>()(Hi) + 0x9e3779b9 + (H << 6) + (H >> 2);
    return H;
  }
};
} // namespace std

namespace blocktype {
namespace ir {

// FNV-1a 128-bit constants
static constexpr __uint128_t FNV128_OFFSET_BASIS =
    (static_cast<__uint128_t>(0x6c62272e07bb0142ULL) << 64) |
     static_cast<__uint128_t>(0x62b821756295c58dULL);

static constexpr __uint128_t FNV128_PRIME =
    (static_cast<__uint128_t>(0x0000000001000000ULL) << 64) |
     static_cast<__uint128_t>(0x000000000000013bULL);

/// Compute a deterministic 128-bit FNV-1a hash from the given string data.
static __uint128_t computeStableHash128(StringRef Data) {
  __uint128_t Hash = FNV128_OFFSET_BASIS;
  for (unsigned char C : Data) {
    Hash ^= static_cast<__uint128_t>(C);
    Hash *= FNV128_PRIME;
  }
  return Hash;
}

StableDefPath::StableDefPath(StringRef P)
    : Path(P.str()), StableHash(computeStableHash128(P)) {}

void StableIdMap::registerDecl(StableDeclId Id, StringRef Path) {
  StableDefPath SDP(Path);
  IdToPath.insert({Id, SDP});
  HashToId.insert({SDP.getStableHash(), Id});
}

std::optional<StableDefPath> StableIdMap::lookupById(StableDeclId Id) const {
  auto It = IdToPath.find(Id);
  if (It != IdToPath.end())
    return (*It).second;
  return std::nullopt;
}

std::optional<StableDeclId> StableIdMap::lookupByHash(__uint128_t Hash) const {
  auto It = HashToId.find(Hash);
  if (It != HashToId.end())
    return (*It).second;
  return std::nullopt;
}

} // namespace ir
} // namespace blocktype
