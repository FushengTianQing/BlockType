#pragma once

#include "AIInterface.h"
#include "llvm/ADT/StringMap.h"
#include <chrono>
#include <optional>

namespace blocktype {

struct CacheEntry {
  AIResponse Response;
  std::chrono::steady_clock::time_point Timestamp;
  unsigned HitCount = 0;
};

class ResponseCache {
  llvm::StringMap<CacheEntry> Cache;
  unsigned MaxSize;
  std::chrono::seconds TTL;  // 缓存有效期
  mutable unsigned Hits = 0;
  mutable unsigned Misses = 0;
  
public:
  ResponseCache(unsigned MaxSize = 1000, 
                std::chrono::seconds TTL = std::chrono::hours(24))
    : MaxSize(MaxSize), TTL(TTL) {}
  
  /// 查找缓存
  std::optional<AIResponse> find(const AIRequest& Request);
  
  /// 插入缓存
  void insert(const AIRequest& Request, const AIResponse& Response);
  
  /// 清理过期缓存
  void cleanup();
  
  /// 清空缓存
  void clear() { 
    Cache.clear(); 
    Hits = 0;
    Misses = 0;
  }
  
  /// 获取缓存统计
  struct Stats {
    unsigned Size;
    unsigned Hits;
    unsigned Misses;
    double HitRate;
  };
  Stats getStats() const;
  
private:
  /// 生成缓存键
  std::string generateKey(const AIRequest& Request) const;
};

} // namespace blocktype
