add_executable(blocktype-integration-tests
  AI/ProviderIntegrationTest.cpp
)

target_link_libraries(blocktype-integration-tests
  blocktype-basic
  blocktype-ai
  GTest::gtest_main
)

gtest_discover_tests(blocktype-integration-tests)
