#include "blocktype/AI/ResponseCache.h"
#include "llvm/Support/SHA1.h"
#include <sstream>

namespace blocktype {

std::string ResponseCache::generateKey(const AIRequest& Request) const {
  // 基于请求内容生成 SHA1 哈希作为缓存键
  std::ostringstream OSS;
  OSS << static_cast<int>(Request.TaskType) << "|"
      << static_cast<int>(Request.Lang) << "|"
      << Request.Context << "|"
      << Request.Query;
  
  llvm::SHA1 Hash;
  Hash.update(OSS.str());
  
  return llvm::toHex(Hash.final());
}

std::optional<AIResponse> ResponseCache::find(const AIRequest& Request) {
  std::string Key = generateKey(Request);
  
  auto It = Cache.find(Key);
  if (It == Cache.end()) {
    ++Misses;
    return std::nullopt;
  }
  
  // 检查是否过期
  auto Now = std::chrono::steady_clock::now();
  auto Age = std::chrono::duration_cast<std::chrono::seconds>(Now - It->second.Timestamp);
  
  if (Age > TTL) {
    Cache.erase(It);
    ++Misses;
    return std::nullopt;
  }
  
  ++Hits;
  ++It->second.HitCount;
  return It->second.Response;
}

void ResponseCache::insert(const AIRequest& Request, const AIResponse& Response) {
  std::string Key = generateKey(Request);
  
  // 如果缓存已满，清理最旧的条目
  if (Cache.size() >= MaxSize) {
    cleanup();
  }
  
  CacheEntry Entry;
  Entry.Response = Response;
  Entry.Timestamp = std::chrono::steady_clock::now();
  Entry.HitCount = 0;
  
  Cache[Key] = Entry;
}

void ResponseCache::cleanup() {
  auto Now = std::chrono::steady_clock::now();
  
  // 删除过期条目
  for (auto It = Cache.begin(); It != Cache.end(); ) {
    auto Age = std::chrono::duration_cast<std::chrono::seconds>(Now - It->second.Timestamp);
    if (Age > TTL) {
      It = Cache.erase(It);
    } else {
      ++It;
    }
  }
  
  // 如果仍然超过最大大小，删除最少使用的条目
  while (Cache.size() > MaxSize) {
    auto MinIt = Cache.begin();
    for (auto It = Cache.begin(); It != Cache.end(); ++It) {
      if (It->second.HitCount < MinIt->second.HitCount) {
        MinIt = It;
      }
    }
    Cache.erase(MinIt);
  }
}

ResponseCache::Stats ResponseCache::getStats() const {
  Stats S;
  S.Size = Cache.size();
  S.Hits = Hits;
  S.Misses = Misses;
  S.HitRate = (Hits + Misses > 0) ? static_cast<double>(Hits) / (Hits + Misses) : 0.0;
  return S;
}

} // namespace blocktype
