#pragma once

#include "AIInterface.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FileSystem.h"
#include <chrono>
#include <optional>
#include <fstream>

namespace blocktype {

struct CacheEntry {
  AIResponse Response;
  std::chrono::steady_clock::time_point Timestamp;
  unsigned HitCount = 0;
  
  /// 序列化到 JSON
  std::string toJson() const;
  
  /// 从 JSON 反序列化
  static std::optional<CacheEntry> fromJson(const std::string& Json);
};

class ResponseCache {
  llvm::StringMap<CacheEntry> Cache;
  unsigned MaxSize;
  std::chrono::seconds TTL;  // 缓存有效期
  mutable unsigned Hits = 0;
  mutable unsigned Misses = 0;
  std::string CacheFilePath;  // 缓存文件路径
  
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
  
  /// 持久化功能
  
  /// 设置缓存文件路径
  void setCacheFile(llvm::StringRef Path) {
    CacheFilePath = Path.str();
  }
  
  /// 保存缓存到文件
  /// @return 成功返回 true，失败返回 false
  bool saveToFile() const;
  
  /// 从文件加载缓存
  /// @param ClearExisting 是否清空现有缓存
  /// @return 成功返回 true，失败返回 false
  bool loadFromFile(bool ClearExisting = true);
  
  /// 自动保存（析构时）
  ~ResponseCache() {
    if (!CacheFilePath.empty()) {
      saveToFile();
    }
  }
  
private:
  /// 生成缓存键
  std::string generateKey(const AIRequest& Request) const;
  
  /// 确保缓存目录存在
  bool ensureCacheDirectory() const;
};

} // namespace blocktype
