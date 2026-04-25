# Infrastructure Sharing Test

add_executable(InfrastructureSharingTest
  InfrastructureSharingTest.cpp
)

target_link_libraries(InfrastructureSharingTest
  PRIVATE
    blocktypeFrontend
    blocktype-basic
    blocktype-lex
    blocktype-parse
    blocktype-ast
    blocktype-sema
    blocktype-codegen
    ${LLVM_LIBS}
)

target_include_directories(InfrastructureSharingTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_test(NAME InfrastructureSharingTest
  COMMAND InfrastructureSharingTest
)

# FrontendBase unit test (GTest)

add_executable(FrontendBaseTest
  FrontendBaseTest.cpp
)

target_link_libraries(FrontendBaseTest
  PRIVATE
    blocktypeFrontend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(FrontendBaseTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(FrontendBaseTest)
