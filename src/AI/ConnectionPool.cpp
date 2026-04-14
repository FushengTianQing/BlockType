#include "blocktype/AI/ConnectionPool.h"
#include <curl/curl.h>
#include <algorithm>

namespace blocktype {

ConnectionPool::~ConnectionPool() {
  clear();
}

std::string ConnectionPool::extractHost(llvm::StringRef URL) const {
  // 简单提取主机名（协议://主机[:端口]）
  size_t Start = URL.find("://");
  if (Start == llvm::StringRef::npos) {
    return "";
  }

  Start += 3;  // 跳过 "://"
  size_t End = URL.find('/', Start);
  if (End == llvm::StringRef::npos) {
    End = URL.size();
  }

  return URL.substr(Start, End - Start).str();
}

void* ConnectionPool::createConnection(llvm::StringRef URL) {
  CURL* Curl = curl_easy_init();
  if (!Curl) {
    return nullptr;
  }

  // 设置通用选项
  curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(Curl, CURLOPT_TIMEOUT_MS, Config.ConnectionTimeout.count() * 1000);

  if (Config.EnableKeepAlive) {
    curl_easy_setopt(Curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(Curl, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(Curl, CURLOPT_TCP_KEEPINTVL, 10L);
  }

  return Curl;
}

void ConnectionPool::destroyConnection(void* Handle) {
  if (Handle) {
    curl_easy_cleanup(static_cast<CURL*>(Handle));
  }
}

HTTPConnection* ConnectionPool::findAvailableConnection(llvm::StringRef Host) {
  auto It = Pool.find(Host);
  if (It == Pool.end()) {
    return nullptr;
  }

  for (auto& Conn : It->second) {
    if (!Conn->InUse) {
      return Conn.get();
    }
  }

  return nullptr;
}

llvm::Expected<void*> ConnectionPool::acquireConnection(llvm::StringRef URL) {
  std::lock_guard<std::mutex> Lock(Mutex);

  std::string Host = extractHost(URL);

  // 查找可用连接
  HTTPConnection* Conn = findAvailableConnection(Host);
  if (Conn) {
    Conn->InUse = true;
    Conn->LastUsed = std::chrono::steady_clock::now();
    Hits++;
    return Conn->Handle;
  }

  Misses++;

  // 检查是否达到最大连接数
  auto& HostPool = Pool[Host];
  if (HostPool.size() >= Config.MaxConnectionsPerHost) {
    return llvm::make_error<llvm::StringError>(
      "Connection pool exhausted for host: " + Host,
      std::make_error_code(std::errc::resource_unavailable_try_again)
    );
  }

  // 创建新连接
  void* Handle = createConnection(URL);
  if (!Handle) {
    return llvm::make_error<llvm::StringError>(
      "Failed to create connection",
      std::make_error_code(std::errc::not_connected)
    );
  }

  // 添加到池中
  auto NewConn = std::make_unique<HTTPConnection>();
  NewConn->URL = URL.str();
  NewConn->Handle = Handle;
  NewConn->InUse = true;
  NewConn->LastUsed = std::chrono::steady_clock::now();

  HostPool.push_back(std::move(NewConn));

  return Handle;
}

void ConnectionPool::releaseConnection(llvm::StringRef URL, void* Handle) {
  std::lock_guard<std::mutex> Lock(Mutex);

  std::string Host = extractHost(URL);
  auto It = Pool.find(Host);
  if (It == Pool.end()) {
    return;
  }

  for (auto& Conn : It->second) {
    if (Conn->Handle == Handle) {
      Conn->InUse = false;
      Conn->LastUsed = std::chrono::steady_clock::now();
      break;
    }
  }
}

void ConnectionPool::cleanupIdleConnections() {
  std::lock_guard<std::mutex> Lock(Mutex);

  auto Now = std::chrono::steady_clock::now();

  for (auto It = Pool.begin(); It != Pool.end(); ++It) {
    auto& Connections = It->second;
    Connections.erase(
      std::remove_if(Connections.begin(), Connections.end(),
        [&](const std::unique_ptr<HTTPConnection>& Conn) {
          if (!Conn->InUse) {
            auto Idle = std::chrono::duration_cast<std::chrono::seconds>(
              Now - Conn->LastUsed
            );
            if (Idle > Config.IdleTimeout) {
              destroyConnection(Conn->Handle);
              return true;
            }
          }
          return false;
        }),
      Connections.end()
    );
  }
}

void ConnectionPool::clear() {
  std::lock_guard<std::mutex> Lock(Mutex);

  for (auto It = Pool.begin(); It != Pool.end(); ++It) {
    auto& Connections = It->second;
    for (auto& Conn : Connections) {
      destroyConnection(Conn->Handle);
    }
  }

  Pool.clear();
}

ConnectionPoolStats ConnectionPool::getStats() const {
  std::lock_guard<std::mutex> Lock(Mutex);

  ConnectionPoolStats Stats;
  Stats.TotalConnections = 0;
  Stats.ActiveConnections = 0;
  Stats.IdleConnections = 0;
  Stats.Hits = Hits;
  Stats.Misses = Misses;
  Stats.HitRate = (Hits + Misses) > 0 ?
    static_cast<double>(Hits) / (Hits + Misses) : 0.0;

  for (auto It = Pool.begin(); It != Pool.end(); ++It) {
    auto& Connections = It->second;
    Stats.TotalConnections += Connections.size();
    for (const auto& Conn : Connections) {
      if (Conn->InUse) {
        Stats.ActiveConnections++;
      } else {
        Stats.IdleConnections++;
      }
    }
  }

  return Stats;
}

// 全局连接池实现
std::unique_ptr<ConnectionPool> GlobalConnectionPool::Instance;
std::mutex GlobalConnectionPool::InstanceMutex;

ConnectionPool& GlobalConnectionPool::get() {
  std::lock_guard<std::mutex> Lock(InstanceMutex);
  if (!Instance) {
    Instance = std::make_unique<ConnectionPool>();
  }
  return *Instance;
}

void GlobalConnectionPool::initialize(const ConnectionPoolConfig& Config) {
  std::lock_guard<std::mutex> Lock(InstanceMutex);
  Instance = std::make_unique<ConnectionPool>(Config);
}

void GlobalConnectionPool::destroy() {
  std::lock_guard<std::mutex> Lock(InstanceMutex);
  Instance.reset();
}

} // namespace blocktype
