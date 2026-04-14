cmake_minimum_required(VERSION 3.20)

project(blocktype
  VERSION 0.1.0
  DESCRIPTION "A C++26 compiler with bilingual support (Chinese/English) built with AI"
  LANGUAGES C CXX
)

# C++23 标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 导出 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 版本号
set(BLOCKTYPE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(BLOCKTYPE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(BLOCKTYPE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# 生成版本头文件
configure_file(
  ${PROJECT_SOURCE_DIR}/include/blocktype/Config/Version.h.in
  ${PROJECT_BINARY_DIR}/include/blocktype/Config/Version.h
)

# 包含 CMake 模块
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(LLVM)
include(GTest)
include(AI)

# 添加子目录
add_subdirectory(src/Basic)
add_subdirectory(src/Lex)
add_subdirectory(src/Parse)
add_subdirectory(src/AST)
add_subdirectory(src/Sema)
add_subdirectory(src/CodeGen)
add_subdirectory(src/AI)
add_subdirectory(tools)
add_subdirectory(tests/unit)
add_subdirectory(tests/integration)

# 安装配置
install(DIRECTORY include/blocktype
  DESTINATION include
  FILES_MATCHING PATTERN "*.h"
)

install(FILES ${PROJECT_BINARY_DIR}/include/blocktype/Config/Version.h
  DESTINATION include/blocktype/Config
)

# Lit 测试配置
find_program(LIT_COMMAND lit)
if(LIT_COMMAND)
  message(STATUS "Found lit: ${LIT_COMMAND}")
  
  # 配置 lit.site.cfg.py
  configure_file(
    ${PROJECT_SOURCE_DIR}/tests/lit/lit.site.cfg.py.in
    ${PROJECT_BINARY_DIR}/tests/lit/lit.site.cfg.py
    @ONLY
  )
  
  # 添加 lit 测试目标
  add_custom_target(check-lit
    COMMAND ${LIT_COMMAND} -v ${PROJECT_BINARY_DIR}/tests/lit
    DEPENDS blocktype-driver
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    COMMENT "Running lit tests"
  )
else()
  message(STATUS "lit not found, lit tests will be skipped")
endif()
