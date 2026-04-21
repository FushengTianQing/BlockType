add_executable(blocktype-basic-test
  DiagnosticsTest.cpp
  SourceLocationTest.cpp
  FixItHintTest.cpp
)

target_link_libraries(blocktype-basic-test
  PRIVATE
    blocktype-basic
    gtest
    gtest_main
)

add_test(NAME BasicTest COMMAND blocktype-basic-test)
