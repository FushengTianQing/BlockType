# Google Test 配置

# 查找本地安装的 Google Test
find_package(GTest QUIET)

if(GTest_FOUND)
  message(STATUS "Found GTest: ${GTEST_INCLUDE_DIRS}")
  message(STATUS "GTest libraries: ${GTEST_BOTH_LIBRARIES}")
else()
  # 如果本地没有，尝试从 GitHub 下载
  message(STATUS "GTest not found locally, fetching from GitHub...")
  
  include(FetchContent)
  
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )
  
  # 对于 Windows: 防止覆盖父项目的编译器/链接器设置
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  
  # 禁用 GTest 的测试
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  
  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()
include(GoogleTest)
