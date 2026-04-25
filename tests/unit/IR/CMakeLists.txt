add_executable(blocktype-ir-test
  IRBackendCapabilityTest.cpp
  IRBuilderTest.cpp
  IRDialectTest.cpp
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
