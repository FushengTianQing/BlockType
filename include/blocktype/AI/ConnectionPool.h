#pragma once

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Error.h"
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace blocktype {

/// HTTP 连接信息
struct HTTPConnection {
  std::string URL;
  void* Handle;  // CURL* handle
  bool InUse = false;
  std::chrono::steady_clock::time_point LastUsed;
};

/// 连接池配置
struct ConnectionPoolConfig {
  unsigned MaxConnectionsPerHost = 5;  // 每个主机最大连接数
  unsigned MaxTotalConnections = 20;    // 总最大连接数
  std::chrono::seconds ConnectionTimeout{30};  // 连接超时
  std::chrono::seconds IdleTimeout{60};        // 空闲连接超时
  bool EnableKeepAlive = true;                 // 启用 Keep-Alive
};

/// 连接池统计
struct ConnectionPoolStats {
  unsigned TotalConnections;
  unsigned ActiveConnections;
  unsigned IdleConnections;
  unsigned Hits;
  unsigned Misses;
  double HitRate;
};

/// HTTP 连接池管理器
class ConnectionPool {
  ConnectionPoolConfig Config;
  mutable std::mutex Mutex;

  // 按主机分组的连接池
  llvm::StringMap<std::vector<std::unique_ptr<HTTPConnection>>> Pool;

  // 统计信息
  mutable unsigned Hits = 0;
  mutable unsigned Misses = 0;

public:
  ConnectionPool(const ConnectionPoolConfig& Cfg = ConnectionPoolConfig{})
    : Config(Cfg) {}

  ~ConnectionPool();

  /// 获取连接
  /// @param URL 请求 URL
  /// @return 连接句柄（需要调用 releaseConnection 归还）
  llvm::Expected<void*> acquireConnection(llvm::StringRef URL);

  /// 归还连接
  /// @param URL 请求 URL
  /// @param Handle 连接句柄
  void releaseConnection(llvm::StringRef URL, void* Handle);

  /// 清理空闲连接
  void cleanupIdleConnections();

  /// 清空所有连接
  void clear();

  /// 获取统计信息
  ConnectionPoolStats getStats() const;

  /// 获取配置
  const ConnectionPoolConfig& getConfig() const { return Config; }

  /// 设置配置
  void setConfig(const ConnectionPoolConfig& Cfg) { Config = Cfg; }

private:
  /// 从 URL 提取主机名
  std::string extractHost(llvm::StringRef URL) const;

  /// 创建新连接
  void* createConnection(llvm::StringRef URL);

  /// 销毁连接
  void destroyConnection(void* Handle);

  /// 查找可用连接
  HTTPConnection* findAvailableConnection(llvm::StringRef Host);
};

/// 全局连接池（单例模式）
class GlobalConnectionPool {
  static std::unique_ptr<ConnectionPool> Instance;
  static std::mutex InstanceMutex;

public:
  /// 获取全局连接池实例
  static ConnectionPool& get();

  /// 初始化全局连接池
  static void initialize(const ConnectionPoolConfig& Config = {});

  /// 销毁全局连接池
  static void destroy();
};

} // namespace blocktype
