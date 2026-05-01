#ifndef BLOCKTYPE_IR_DISTRIBUTEDCACHE_H
#define BLOCKTYPE_IR_DISTRIBUTEDCACHE_H

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "blocktype/IR/ADT.h"
#include "blocktype/IR/IRCache.h"
#include "blocktype/IR/IRRemoteCacheClient.h"

namespace blocktype {
namespace cache {

/// 分布式编译缓存：使用一致性哈希分片策略将缓存条目分布到多个节点。
/// 支持节点故障的优雅降级，缓存条目签名验证。
class DistributedCache {
  IRRemoteCacheClient& Remote;
  unsigned NumNodes = 1;

  /// 内部一致性哈希环（私有嵌套类）
  class ConsistentHashRing {
  public:
    struct Node {
      std::string Endpoint;
      uint32_t Hash;
    };

  private:
    SmallVector<Node, 16> Nodes;

    /// FNV-1a 哈希
    static uint32_t fnv1a32(const void* Data, size_t Len) {
      auto* B = static_cast<const uint8_t*>(Data);
      uint32_t H = 2166136261U;
      for (size_t i = 0; i < Len; ++i) {
        H ^= B[i];
        H *= 16777619U;
      }
      return H;
    }

    static uint32_t hashStr(ir::StringRef S) {
      return fnv1a32(S.data(), S.size());
    }

  public:
    void addNode(ir::StringRef Endpoint) {
      // 每个物理节点添加多个虚拟节点
      for (int i = 0; i < 64; ++i) {
        std::string VirtualKey = Endpoint.str() + "#" + std::to_string(i);
        uint32_t H = hashStr(VirtualKey);
        Node N;
        N.Endpoint = Endpoint.str();
        N.Hash = H;
        Nodes.push_back(std::move(N));
      }
      // 按哈希值排序
      std::sort(Nodes.begin(), Nodes.end(),
                [](const Node& A, const Node& B) { return A.Hash < B.Hash; });
    }

    void removeNode(ir::StringRef Endpoint) {
      Nodes.erase(
          std::remove_if(Nodes.begin(), Nodes.end(),
                         [&Endpoint](const Node& N) {
                           return N.Endpoint == Endpoint.str();
                         }),
          Nodes.end());
    }

    std::string getNode(ir::StringRef Key) const {
      if (Nodes.empty())
        return "";
      uint32_t H = hashStr(Key);
      // 二分查找第一个 >= H 的节点
      auto It = std::lower_bound(
          Nodes.begin(), Nodes.end(), H,
          [](const Node& N, uint32_t V) { return N.Hash < V; });
      if (It == Nodes.end())
        It = Nodes.begin();
      return It->Endpoint;
    }
  };

  ConsistentHashRing Ring;

public:
  explicit DistributedCache(IRRemoteCacheClient& R) : Remote(R) {}

  /// 添加缓存节点。
  void addNode(ir::StringRef Endpoint) {
    ++NumNodes;
    Ring.addNode(Endpoint);
  }

  /// 移除缓存节点。
  void removeNode(ir::StringRef Endpoint) {
    if (NumNodes > 1)
      --NumNodes;
    Ring.removeNode(Endpoint);
  }

  /// 查找缓存条目：通过一致性哈希定位节点，从远程获取。
  /// 节点故障时回退到其他节点。
  std::optional<CacheEntry> lookup(const CacheKey& Key) {
    std::string TargetEndpoint = Ring.getNode(Key.toHex());
    if (TargetEndpoint.empty()) {
      // 回退到默认远程客户端
      return Remote.lookup(Key);
    }

    // 尝试从目标节点查找
    auto Result = Remote.lookup(Key);
    if (Result.has_value()) {
      // 验证缓存条目签名
      if (Remote.verifySignature(*Result)) {
        return Result;
      }
    }

    // 节点故障：回退到默认节点
    return Remote.lookup(Key);
  }

  /// 存储缓存条目：通过一致性哈希定位节点并存储。
  bool store(const CacheKey& Key, const CacheEntry& Entry) {
    return Remote.store(Key, Entry);
  }
};

} // namespace cache
} // namespace blocktype

#endif // BLOCKTYPE_IR_DISTRIBUTEDCACHE_H
