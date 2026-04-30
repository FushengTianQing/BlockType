# Backend unit tests

add_executable(BackendBaseTest
  BackendBaseTest.cpp
)

target_link_libraries(BackendBaseTest
  PRIVATE
    blocktypeBackend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(BackendBaseTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(BackendBaseTest)

# IRToLLVMConverter + LLVMBackend integration test

add_executable(IRToLLVMConverterTest
  IRToLLVMConverterTest.cpp
)

target_link_libraries(IRToLLVMConverterTest
  PRIVATE
    blocktypeBackend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(IRToLLVMConverterTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(IRToLLVMConverterTest)
