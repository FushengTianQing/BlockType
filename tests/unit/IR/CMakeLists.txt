add_executable(blocktype-ir-test
  IRBuilderTest.cpp
  IRSerializerTest.cpp
  IRVerifierTest.cpp
)

target_link_libraries(blocktype-ir-test
  PRIVATE
    blocktype-ir
    gtest
    gtest_main
)

add_test(NAME IRBuilderTest COMMAND blocktype-ir-test)
