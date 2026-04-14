add_executable(blocktype-tests
  Basic/SourceLocationTest.cpp
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