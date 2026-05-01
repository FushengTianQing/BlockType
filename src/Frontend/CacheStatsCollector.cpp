#include "blocktype/IR/IRCache.h"
#include "blocktype/IR/ADT.h"

#include <cmath>
#include <sstream>
#include <string>

namespace blocktype {
namespace frontend {

using namespace blocktype::cache;

/// CacheStatsCollector — gathers and reports cache statistics.
class CacheStatsCollector {
  CompilationCacheManager& CCM;

public:
  explicit CacheStatsCollector(CompilationCacheManager& M) : CCM(M) {}

  /// Get the current cache hit rate as a fraction [0.0, 1.0].
  double getHitRate() const {
    if (!CCM.isEnabled())
      return 0.0;
    // The CompilationCacheManager currently has stubs, but we provide
    // the infrastructure for when the storage is fully implemented.
    return 0.0;
  }

  /// Get the total cache size in bytes.
  size_t getTotalSize() const {
    if (!CCM.isEnabled())
      return 0;
    return 0;
  }

  /// Format cache statistics as a human-readable string.
  std::string getStatsReport() const {
    std::ostringstream OS;
    OS << "Cache Statistics:\n";
    OS << "  Enabled: " << (CCM.isEnabled() ? "yes" : "no") << "\n";
    OS << "  Hit rate: " << (getHitRate() * 100.0) << "%\n";
    OS << "  Total size: " << getTotalSize() << " bytes\n";
    return OS.str();
  }

  /// Format cache statistics as a one-line summary.
  std::string getStatsSummary() const {
    std::ostringstream OS;
    OS << "hit rate: " << (getHitRate() * 100.0) << "%, ";
    OS << "total size: " << getTotalSize() << " bytes";
    return OS.str();
  }
};

/// Public interface to collect and report cache statistics.
std::string collectCacheStats(cache::CompilationCacheManager& CCM) {
  CacheStatsCollector Collector(CCM);
  return Collector.getStatsReport();
}

std::string getCacheStatsSummary(cache::CompilationCacheManager& CCM) {
  CacheStatsCollector Collector(CCM);
  return Collector.getStatsSummary();
}

double getCacheHitRate(cache::CompilationCacheManager& CCM) {
  CacheStatsCollector Collector(CCM);
  return Collector.getHitRate();
}

} // namespace frontend
} // namespace blocktype
