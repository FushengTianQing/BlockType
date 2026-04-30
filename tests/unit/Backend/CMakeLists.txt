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

# LLVMBackendRegistration unit test (GTest) — Phase D.4

add_executable(LLVMBackendRegistrationTest
  LLVMBackendRegistrationTest.cpp
)

target_link_libraries(LLVMBackendRegistrationTest
  PRIVATE
    blocktypeFrontend
    blocktypeBackend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(LLVMBackendRegistrationTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(LLVMBackendRegistrationTest)

# InstructionSelector unit test (GTest) — Phase D-F1

add_executable(InstructionSelectorTest
  InstructionSelectorTest.cpp
)

target_link_libraries(InstructionSelectorTest
  PRIVATE
    blocktypeBackend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(InstructionSelectorTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(InstructionSelectorTest)

# RegisterAllocator unit test (GTest) — Phase D-F2

add_executable(RegisterAllocatorTest
  RegisterAllocatorTest.cpp
)

target_link_libraries(RegisterAllocatorTest
  PRIVATE
    blocktypeBackend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(RegisterAllocatorTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(RegisterAllocatorTest)

# BackendInterfaces unit test (GTest) — Phase D-F3

add_executable(BackendInterfacesTest
  BackendInterfacesTest.cpp
)

target_link_libraries(BackendInterfacesTest
  PRIVATE
    blocktypeBackend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(BackendInterfacesTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(BackendInterfacesTest)

# DebugInfoEmitter unit test (GTest) — Phase D-F4

add_executable(DebugInfoEmitterTest
  DebugInfoEmitterTest.cpp
)

target_link_libraries(DebugInfoEmitterTest
  PRIVATE
    blocktypeBackend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(DebugInfoEmitterTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(DebugInfoEmitterTest)

# CodeView debug info emitter unit test (GTest) — Phase E-F8

add_executable(CodeViewEmitterTest
  CodeViewEmitterTest.cpp
)

target_link_libraries(CodeViewEmitterTest
  PRIVATE
    blocktypeBackend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(CodeViewEmitterTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(CodeViewEmitterTest)
