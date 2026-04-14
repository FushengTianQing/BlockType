#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <map>
#include <functional>

namespace blocktype {

/// HTTP 响应
struct HTTPResponse {
  long StatusCode;           // HTTP 状态码
  std::string Body;          // 响应体
  std::string ErrorMessage;  // 错误信息
  bool Success;              // 是否成功
};

/// SSE 流式回调类型
/// @param Data 收到的数据片段（已解析的 SSE data 内容）
/// @param Done 是否完成
using SSECallback = std::function<void(llvm::StringRef Data, bool Done)>;

/// HTTP 客户端工具类
class HTTPClient {
public:
  /// 发送 HTTP POST 请求
  /// @param URL 请求 URL
  /// @param Body 请求体（JSON 格式）
  /// @param Headers 请求头
  /// @param TimeoutMs 超时时间（毫秒）
  /// @return HTTP 响应
  static llvm::Expected<HTTPResponse> post(
    llvm::StringRef URL,
    llvm::StringRef Body,
    const std::map<std::string, std::string>& Headers,
    unsigned TimeoutMs = 30000
  );

  /// 发送 SSE 流式 POST 请求
  /// @param URL 请求 URL
  /// @param Body 请求体（JSON 格式）
  /// @param Headers 请求头
  /// @param Callback SSE 数据回调
  /// @param TimeoutMs 超时时间（毫秒）
  /// @return HTTP 响应（包含完整响应体）
  static llvm::Expected<HTTPResponse> postSSE(
    llvm::StringRef URL,
    llvm::StringRef Body,
    const std::map<std::string, std::string>& Headers,
    SSECallback Callback,
    unsigned TimeoutMs = 60000
  );

  /// 发送 HTTP GET 请求
  static llvm::Expected<HTTPResponse> get(
    llvm::StringRef URL,
    const std::map<std::string, std::string>& Headers,
    unsigned TimeoutMs = 30000
  );

  /// URL 编码
  static std::string urlEncode(llvm::StringRef Str);

  /// 解析 SSE 数据行（公开供回调使用）
  static std::string parseSSELine(llvm::StringRef Line);

private:
  /// 初始化 curl（线程安全）
  static void initCurl();
};

} // namespace blocktype
