add_executable(blocktype-tests
  Basic/SourceLocationTest.cpp
  Basic/DiagnosticsTest.cpp
  Basic/FixItHintTest.cpp
  Basic/UTF8ValidatorTest.cpp
  AI/ResponseCacheTest.cpp
  AI/CostTrackerTest.cpp
  AI/AIOrchestratorTest.cpp
)

target_link_libraries(blocktype-tests
  blocktype-basic
  blocktype-ai
  GTest::gtest_main
)

gtest_discover_tests(blocktype-tests
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

# Lex 单元测试
add_subdirectory(Lex)

# AST 单元测试
add_subdirectory(AST)

# Parse 单元测试
add_subdirectory(Parse)

# Sema 单元测试
add_subdirectory(Sema)

# CodeGen 单元测试
add_subdirectory(CodeGen)

# D19: Performance tests
add_executable(PerformanceTest
  PerformanceTest.cpp
)

target_link_libraries(PerformanceTest
  PRIVATE
  blocktype-lex
  blocktype-basic
  blocktype-unicode
  GTest::gtest_main
)

gtest_discover_tests(PerformanceTest)

# Copy performance test to benchmark directory for consistency
set_target_properties(PerformanceTest PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)