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

} // namespace blocktype