add_executable(blocktype-tests
  Basic/SourceLocationTest.cpp
  Basic/DiagnosticsTest.cpp
  AI/ResponseCacheTest.cpp
  AI/CostTrackerTest.cpp
  AI/AIOrchestratorTest.cpp
)

target_link_libraries(blocktype-tests
  blocktype-basic
  blocktype-ai
  GTest::gtest_main
)

gtest_discover_tests(blocktype-tests)

# Lex 单元测试
add_subdirectory(Lex)

# AST 单元测试
add_subdirectory(AST)

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