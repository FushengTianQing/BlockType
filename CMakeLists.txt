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

# 代码覆盖率选项
option(ENABLE_COVERAGE "Enable code coverage reporting" OFF)
if(ENABLE_COVERAGE)
  message(STATUS "Code coverage enabled")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
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

# 添加第三方库
add_subdirectory(third_party/utf8proc)

# 添加子目录
add_subdirectory(src/Basic)
add_subdirectory(src/IR)
add_subdirectory(src/Unicode)
add_subdirectory(src/Lex)
add_subdirectory(src/Parse)
add_subdirectory(src/AST)
add_subdirectory(src/Sema)
add_subdirectory(src/Module)
add_subdirectory(src/Driver)
add_subdirectory(src/CodeGen)
add_subdirectory(src/AI)
add_subdirectory(src/Backend)
add_subdirectory(src/Frontend)
add_subdirectory(tools)
add_subdirectory(tests/unit)
add_subdirectory(tests/integration)
add_subdirectory(tests/benchmark)
add_subdirectory(tests/cpp23)
add_subdirectory(tests/cpp26)
add_subdirectory(tests/fuzz)

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

# 代码覆盖率目标
if(ENABLE_COVERAGE)
  find_program(LCOV_PATH lcov)
  find_program(GENHTML_PATH genhtml)
  
  if(LCOV_PATH AND GENHTML_PATH)
    add_custom_target(coverage
      COMMAND ${LCOV_PATH} --capture --directory . --output-file coverage.info
      COMMAND ${LCOV_PATH} --remove coverage.info '/usr/*' '*/tests/*' '*/third_party/*' --output-file coverage.info.cleaned
      COMMAND ${GENHTML_PATH} -o coverage_report coverage.info.cleaned
      COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in coverage_report/index.html"
      WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
      COMMENT "Generating code coverage report"
    )
    message(STATUS "Coverage target 'coverage' available")
  else()
    message(WARNING "lcov or genhtml not found, coverage target will not be available")
  endif()
endif()
