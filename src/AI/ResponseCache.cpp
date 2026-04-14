#include "blocktype/AI/ResponseCache.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/Path.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace blocktype {

using json = nlohmann::json;

// CacheEntry 序列化方法

std::string CacheEntry::toJson() const {
  json J;
  
  // 序列化 Response
  J["response"]["success"] = Response.Success;
  J["response"]["content"] = Response.Content;
  J["response"]["provider"] = Response.Provider;
  J["response"]["tokens_used"] = Response.TokensUsed;
  J["response"]["latency_ms"] = Response.LatencyMs;
  J["response"]["error_message"] = Response.ErrorMessage;
  J["response"]["suggestions"] = Response.Suggestions;
  
  // 序列化 Timestamp（转换为时间戳）
  auto TimestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
    Timestamp.time_since_epoch()
  ).count();
  J["timestamp_ms"] = TimestampMs;
  
  // 序列化 HitCount
  J["hit_count"] = HitCount;
  
  return J.dump(2);
}

std::optional<CacheEntry> CacheEntry::fromJson(const std::string& JsonStr) {
  try {
    auto J = json::parse(JsonStr);
    
    CacheEntry Entry;
    
    // 反序列化 Response
    Entry.Response.Success = J["response"]["success"].get<bool>();
    Entry.Response.Content = J["response"]["content"].get<std::string>();
    Entry.Response.Provider = J["response"]["provider"].get<std::string>();
    Entry.Response.TokensUsed = J["response"]["tokens_used"].get<unsigned>();
    Entry.Response.LatencyMs = J["response"]["latency_ms"].get<double>();
    Entry.Response.ErrorMessage = J["response"]["error_message"].get<std::string>();
    Entry.Response.Suggestions = J["response"]["suggestions"].get<std::vector<std::string>>();
    
    // 反序列化 Timestamp
    auto TimestampMs = J["timestamp_ms"].get<int64_t>();
    Entry.Timestamp = std::chrono::steady_clock::time_point(
      std::chrono::milliseconds(TimestampMs)
    );
    
    // 反序列化 HitCount
    Entry.HitCount = J["hit_count"].get<unsigned>();
    
    return Entry;
  } catch (const std::exception& E) {
    // 解析失败
    return std::nullopt;
  }
}

// ResponseCache 方法

std::string ResponseCache::generateKey(const AIRequest& Request) const {
  // 基于请求内容生成 SHA1 哈希作为缓存键
  std::ostringstream OSS;
  OSS << static_cast<int>(Request.TaskType) << "|"
      << static_cast<int>(Request.Lang) << "|"
      << Request.Context << "|"
      << Request.Query;
  
  llvm::SHA1 Hash;
  Hash.update(OSS.str());
  
  // 将 SHA1 结果转换为十六进制字符串
  auto Result = Hash.final();
  std::ostringstream HexSS;
  for (uint8_t Byte : Result) {
    HexSS << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(Byte);
  }
  return HexSS.str();
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
  std::vector<std::string> KeysToRemove;
  for (auto It = Cache.begin(); It != Cache.end(); ++It) {
    auto Age = std::chrono::duration_cast<std::chrono::seconds>(Now - It->second.Timestamp);
    if (Age > TTL) {
      KeysToRemove.push_back(It->first().str());
    }
  }
  
  for (const auto& Key : KeysToRemove) {
    Cache.erase(Key);
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

// 持久化方法

bool ResponseCache::ensureCacheDirectory() const {
  if (CacheFilePath.empty()) {
    return false;
  }
  
  // 获取目录路径
  llvm::StringRef DirPath = llvm::sys::path::parent_path(CacheFilePath);
  
  if (DirPath.empty()) {
    return true;  // 当前目录
  }
  
  // 创建目录
  std::error_code EC = llvm::sys::fs::create_directories(DirPath);
  
  return !EC;
}

bool ResponseCache::saveToFile() const {
  if (CacheFilePath.empty() || Cache.empty()) {
    return false;
  }
  
  // 确保目录存在
  if (!ensureCacheDirectory()) {
    return false;
  }
  
  try {
    // 构建 JSON 数组
    json J = json::array();
    
    for (auto It = Cache.begin(); It != Cache.end(); ++It) {
      json EntryJson;
      EntryJson["key"] = It->first().str();
      EntryJson["data"] = json::parse(It->second.toJson());
      J.push_back(EntryJson);
    }
    
    // 写入文件
    std::ofstream File(CacheFilePath);
    if (!File.is_open()) {
      return false;
    }
    
    File << J.dump(2);
    File.close();
    
    return true;
  } catch (const std::exception& E) {
    return false;
  }
}

bool ResponseCache::loadFromFile(bool ClearExisting) {
  if (CacheFilePath.empty()) {
    return false;
  }
  
  try {
    // 读取文件
    std::ifstream File(CacheFilePath);
    if (!File.is_open()) {
      return false;
    }
    
    std::stringstream Buffer;
    Buffer << File.rdbuf();
    File.close();
    
    // 解析 JSON
    auto J = json::parse(Buffer.str());
    
    if (!J.is_array()) {
      return false;
    }
    
    // 清空现有缓存
    if (ClearExisting) {
      Cache.clear();
    }
    
    // 加载缓存条目
    for (const auto& EntryJson : J) {
      std::string Key = EntryJson["key"].get<std::string>();
      std::string DataStr = EntryJson["data"].dump();
      
      auto Entry = CacheEntry::fromJson(DataStr);
      if (Entry) {
        // 检查是否过期
        auto Now = std::chrono::steady_clock::now();
        auto Age = std::chrono::duration_cast<std::chrono::seconds>(
          Now - Entry->Timestamp
        );
        
        if (Age <= TTL) {
          Cache[Key] = *Entry;
        }
      }
    }
    
    return true;
  } catch (const std::exception& E) {
    return false;
  }
}

} // namespace blocktype
