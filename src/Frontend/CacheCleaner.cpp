#include "blocktype/IR/IRCache.h"
#include "blocktype/IR/ADT.h"

#include <algorithm>
#include <string>

namespace blocktype {
namespace frontend {

using namespace blocktype::cache;

/// CacheCleaner — performs LRU-based cache eviction to enforce size limits.
class CacheCleaner {
  CompilationCacheManager& CCM;
  size_t MaxSizeBytes;

public:
  CacheCleaner(CompilationCacheManager& M, size_t MaxBytes)
    : CCM(M), MaxSizeBytes(MaxBytes) {}

  /// Perform cleanup: evict least-recently-used entries until cache is within
  /// the configured maximum size.
  bool clean() {
    if (!CCM.isEnabled())
      return false;

    // Current implementation: CompilationCacheManager has stubs.
    // When fully implemented, this will:
    // 1. Query total cache size
    // 2. If over MaxSizeBytes, evict LRU entries until under limit
    // 3. Return true if cleanup was performed
    return false;
  }

  /// Get the configured maximum cache size.
  size_t getMaxSize() const { return MaxSizeBytes; }

  /// Check if the cache is within the configured size limit.
  bool isWithinLimit() const {
    if (!CCM.isEnabled())
      return true;
    // Stub: always returns true until storage is fully implemented
    return true;
  }
};

/// Public interface to run cache cleanup with a given size limit.
bool runCacheCleanup(cache::CompilationCacheManager& CCM, size_t MaxSizeBytes) {
  CacheCleaner Cleaner(CCM, MaxSizeBytes);
  return Cleaner.clean();
}

/// Check if cache size is within the given limit.
bool isCacheWithinLimit(cache::CompilationCacheManager& CCM, size_t MaxSizeBytes) {
  CacheCleaner Cleaner(CCM, MaxSizeBytes);
  return Cleaner.isWithinLimit();
}

} // namespace frontend
} // namespace blocktype
