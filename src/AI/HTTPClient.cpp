#include "blocktype/AI/HTTPClient.h"
#include <curl/curl.h>
#include <sstream>
#include <stdexcept>

namespace blocktype {

namespace {

// curl 写回调函数
size_t writeCallback(void* Contents, size_t Size, size_t Nmemb, void* Userp) {
  size_t TotalSize = Size * Nmemb;
  std::string* Response = static_cast<std::string*>(Userp);
  Response->append(static_cast<char*>(Contents), TotalSize);
  return TotalSize;
}

// SSE 回调上下文
struct SSEContext {
  std::string Buffer;           // 未处理的缓冲区
  std::string FullBody;         // 完整响应体
  blocktype::SSECallback Callback;  // 用户回调
};

// SSE 写回调函数
size_t sseWriteCallback(void* Contents, size_t Size, size_t Nmemb, void* Userp) {
  size_t TotalSize = Size * Nmemb;
  SSEContext* Context = static_cast<SSEContext*>(Userp);

  // 添加到缓冲区
  Context->Buffer.append(static_cast<char*>(Contents), TotalSize);

  // 处理完整的行
  size_t Pos = 0;
  while ((Pos = Context->Buffer.find('\n')) != std::string::npos) {
    std::string Line = Context->Buffer.substr(0, Pos);
    Context->Buffer = Context->Buffer.substr(Pos + 1);

    // 解析 SSE 行
    std::string Data = blocktype::HTTPClient::parseSSELine(Line);
    if (!Data.empty()) {
      // 添加到完整响应
      Context->FullBody += Data;

      // 调用用户回调
      if (Context->Callback) {
        Context->Callback(Data, false);
      }
    }
  }

  return TotalSize;
}

// 全局 curl 初始化标志
static bool CurlInitialized = false;

} // anonymous namespace

void HTTPClient::initCurl() {
  if (!CurlInitialized) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CurlInitialized = true;
  }
}

llvm::Expected<HTTPResponse> HTTPClient::post(
  llvm::StringRef URL,
  llvm::StringRef Body,
  const std::map<std::string, std::string>& Headers,
  unsigned TimeoutMs
) {
  initCurl();
  
  HTTPResponse Response;
  Response.Success = false;
  Response.StatusCode = 0;
  
  CURL* Curl = curl_easy_init();
  if (!Curl) {
    return llvm::make_error<llvm::StringError>(
      "Failed to initialize curl",
      std::make_error_code(std::errc::not_connected)
    );
  }
  
  std::string ResponseBody;
  struct curl_slist* HeaderList = nullptr;
  
  // 设置请求头
  for (const auto& [Key, Value] : Headers) {
    std::string Header = Key + ": " + Value;
    HeaderList = curl_slist_append(HeaderList, Header.c_str());
  }
  
  // 设置 curl 选项
  curl_easy_setopt(Curl, CURLOPT_URL, URL.data());
  curl_easy_setopt(Curl, CURLOPT_POST, 1L);
  curl_easy_setopt(Curl, CURLOPT_POSTFIELDS, Body.data());
  curl_easy_setopt(Curl, CURLOPT_POSTFIELDSIZE, Body.size());
  curl_easy_setopt(Curl, CURLOPT_HTTPHEADER, HeaderList);
  curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &ResponseBody);
  curl_easy_setopt(Curl, CURLOPT_TIMEOUT_MS, TimeoutMs);
  curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYHOST, 2L);
  
  // 执行请求
  CURLcode Res = curl_easy_perform(Curl);
  
  if (Res != CURLE_OK) {
    Response.ErrorMessage = curl_easy_strerror(Res);
    curl_slist_free_all(HeaderList);
    curl_easy_cleanup(Curl);
    return llvm::make_error<llvm::StringError>(
      Response.ErrorMessage,
      std::make_error_code(std::errc::network_unreachable)
    );
  }
  
  // 获取 HTTP 状态码
  curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &Response.StatusCode);
  
  Response.Body = ResponseBody;
  Response.Success = (Response.StatusCode >= 200 && Response.StatusCode < 300);
  
  if (!Response.Success) {
    Response.ErrorMessage = "HTTP " + std::to_string(Response.StatusCode);
  }
  
  curl_slist_free_all(HeaderList);
  curl_easy_cleanup(Curl);
  
  return Response;
}

llvm::Expected<HTTPResponse> HTTPClient::get(
  llvm::StringRef URL,
  const std::map<std::string, std::string>& Headers,
  unsigned TimeoutMs
) {
  initCurl();
  
  HTTPResponse Response;
  Response.Success = false;
  Response.StatusCode = 0;
  
  CURL* Curl = curl_easy_init();
  if (!Curl) {
    return llvm::make_error<llvm::StringError>(
      "Failed to initialize curl",
      std::make_error_code(std::errc::not_connected)
    );
  }
  
  std::string ResponseBody;
  struct curl_slist* HeaderList = nullptr;
  
  // 设置请求头
  for (const auto& [Key, Value] : Headers) {
    std::string Header = Key + ": " + Value;
    HeaderList = curl_slist_append(HeaderList, Header.c_str());
  }
  
  // 设置 curl 选项
  curl_easy_setopt(Curl, CURLOPT_URL, URL.data());
  curl_easy_setopt(Curl, CURLOPT_HTTPHEADER, HeaderList);
  curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, writeCallback);
  curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &ResponseBody);
  curl_easy_setopt(Curl, CURLOPT_TIMEOUT_MS, TimeoutMs);
  curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYHOST, 2L);
  
  // 执行请求
  CURLcode Res = curl_easy_perform(Curl);
  
  if (Res != CURLE_OK) {
    Response.ErrorMessage = curl_easy_strerror(Res);
    curl_slist_free_all(HeaderList);
    curl_easy_cleanup(Curl);
    return llvm::make_error<llvm::StringError>(
      Response.ErrorMessage,
      std::make_error_code(std::errc::network_unreachable)
    );
  }
  
  // 获取 HTTP 状态码
  curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &Response.StatusCode);
  
  Response.Body = ResponseBody;
  Response.Success = (Response.StatusCode >= 200 && Response.StatusCode < 300);
  
  if (!Response.Success) {
    Response.ErrorMessage = "HTTP " + std::to_string(Response.StatusCode);
  }
  
  curl_slist_free_all(HeaderList);
  curl_easy_cleanup(Curl);
  
  return Response;
}

std::string HTTPClient::urlEncode(llvm::StringRef Str) {
  initCurl();

  char* Encoded = curl_easy_escape(nullptr, Str.data(), Str.size());
  if (!Encoded) {
    return Str.str();
  }

  std::string Result(Encoded);
  curl_free(Encoded);
  return Result;
}

std::string HTTPClient::parseSSELine(llvm::StringRef Line) {
  // SSE 格式: "data: <content>"
  // 跳过空行和注释
  if (Line.empty() || Line.startswith(":")) {
    return "";
  }

  // 检查是否是 data 行
  if (Line.startswith("data:")) {
    std::string Data = Line.substr(5).str();

    // 去除前导空格
    size_t Start = Data.find_first_not_of(" \t");
    if (Start != std::string::npos) {
      Data = Data.substr(Start);
    }

    // 检查是否是结束标记 [DONE]
    if (Data == "[DONE]") {
      return "";
    }

    return Data;
  }

  return "";
}

llvm::Expected<HTTPResponse> HTTPClient::postSSE(
  llvm::StringRef URL,
  llvm::StringRef Body,
  const std::map<std::string, std::string>& Headers,
  SSECallback Callback,
  unsigned TimeoutMs
) {
  initCurl();

  HTTPResponse Response;
  Response.Success = false;
  Response.StatusCode = 0;

  CURL* Curl = curl_easy_init();
  if (!Curl) {
    return llvm::make_error<llvm::StringError>(
      "Failed to initialize curl",
      std::make_error_code(std::errc::not_connected)
    );
  }

  SSEContext Context;
  Context.Callback = Callback;

  struct curl_slist* HeaderList = nullptr;

  // 设置请求头
  for (const auto& [Key, Value] : Headers) {
    std::string Header = Key + ": " + Value;
    HeaderList = curl_slist_append(HeaderList, Header.c_str());
  }

  // 设置 curl 选项
  curl_easy_setopt(Curl, CURLOPT_URL, URL.data());
  curl_easy_setopt(Curl, CURLOPT_POST, 1L);
  curl_easy_setopt(Curl, CURLOPT_POSTFIELDS, Body.data());
  curl_easy_setopt(Curl, CURLOPT_POSTFIELDSIZE, Body.size());
  curl_easy_setopt(Curl, CURLOPT_HTTPHEADER, HeaderList);
  curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, sseWriteCallback);
  curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &Context);
  curl_easy_setopt(Curl, CURLOPT_TIMEOUT_MS, TimeoutMs);
  curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYHOST, 2L);

  // 执行请求
  CURLcode Res = curl_easy_perform(Curl);

  if (Res != CURLE_OK) {
    Response.ErrorMessage = curl_easy_strerror(Res);
    curl_slist_free_all(HeaderList);
    curl_easy_cleanup(Curl);
    return llvm::make_error<llvm::StringError>(
      Response.ErrorMessage,
      std::make_error_code(std::errc::network_unreachable)
    );
  }

  // 获取 HTTP 状态码
  curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &Response.StatusCode);

  Response.Body = Context.FullBody;
  Response.Success = (Response.StatusCode >= 200 && Response.StatusCode < 300);

  if (!Response.Success) {
    Response.ErrorMessage = "HTTP " + std::to_string(Response.StatusCode);
  }

  // 通知流结束
  if (Callback) {
    Callback("", true);
  }

  curl_slist_free_all(HeaderList);
  curl_easy_cleanup(Curl);

  return Response;
}

} // namespace blocktype