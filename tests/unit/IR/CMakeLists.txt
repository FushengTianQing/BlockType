add_executable(blocktype-ir-test
  IRBackendCapabilityTest.cpp
  IRBuilderTest.cpp
  IRDialectTest.cpp
  IRSerializerTest.cpp
  IRTelemetryTest.cpp
  IRDebugInfoTest.cpp
  IRVerifierTest.cpp
  IRDiagnosticTest.cpp
  IRCacheTest.cpp
  IRIntegrityTest.cpp
  IRConversionResultTest.cpp
  IRErrorCodeTest.cpp
  IRPassesTest.cpp
  IRContractTest.cpp
  IRFFITest.cpp
)

target_link_libraries(blocktype-ir-test
  PRIVATE
    blocktype-ir
    gtest
    gtest_main
)

add_test(NAME IRBuilderTest COMMAND blocktype-ir-test)
