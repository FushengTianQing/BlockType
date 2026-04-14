# AI 功能依赖配置

# libcurl (HTTP 客户端)
find_package(CURL QUIET)
if(CURL_FOUND)
  message(STATUS "Found CURL: ${CURL_LIBRARIES}")
  message(STATUS "CURL include: ${CURL_INCLUDE_DIRS}")
else()
  message(STATUS "CURL not found, AI features will be limited")
endif()

# nlohmann/json (JSON 解析)
# 优先使用本地安装的版本
find_package(nlohmann_json QUIET)

if(nlohmann_json_FOUND)
  message(STATUS "Found nlohmann_json: ${nlohmann_json_INCLUDE_DIRS}")
else()
  # 如果本地没有，尝试从 GitHub 下载
  message(STATUS "nlohmann_json not found locally, fetching from GitHub...")
  
  include(FetchContent)
  
  FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )
  
  FetchContent_MakeAvailable(json)
endif()

message(STATUS "nlohmann/json configured")
