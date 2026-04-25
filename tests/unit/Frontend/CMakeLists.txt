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

# FrontendRegistry unit test (GTest)

add_executable(FrontendRegistryTest
  FrontendRegistryTest.cpp
)

target_link_libraries(FrontendRegistryTest
  PRIVATE
    blocktypeFrontend
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(FrontendRegistryTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(FrontendRegistryTest)

# IRTypeMapper unit test (GTest)

add_executable(IRTypeMapperTest
  IRTypeMapperTest.cpp
)

target_link_libraries(IRTypeMapperTest
  PRIVATE
    blocktypeFrontend
    blocktype-basic
    blocktype-ir
    blocktype-ast
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(IRTypeMapperTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(IRTypeMapperTest)

# ASTToIRConverter unit test (GTest)

add_executable(ASTToIRConverterTest
  ASTToIRConverterTest.cpp
)

target_link_libraries(ASTToIRConverterTest
  PRIVATE
    blocktypeFrontend
    blocktype-basic
    blocktype-ir
    blocktype-ast
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(ASTToIRConverterTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(ASTToIRConverterTest)

# IREmitExpr unit test (GTest)

add_executable(IREmitExprTest
  IREmitExprTest.cpp
)

target_link_libraries(IREmitExprTest
  PRIVATE
    blocktypeFrontend
    blocktype-basic
    blocktype-ir
    blocktype-ast
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(IREmitExprTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(IREmitExprTest)

# IREmitStmt unit test (GTest)

add_executable(IREmitStmtTest
  IREmitStmtTest.cpp
)

target_link_libraries(IREmitStmtTest
  PRIVATE
    blocktypeFrontend
    blocktype-basic
    blocktype-ir
    blocktype-ast
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(IREmitStmtTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(IREmitStmtTest)

# IRMangler unit test (GTest)

add_executable(IRManglerTest
  IRManglerTest.cpp
)

target_link_libraries(IRManglerTest
  PRIVATE
    blocktypeFrontend
    blocktype-basic
    blocktype-ir
    blocktype-ast
    ${LLVM_LIBS}
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(IRManglerTest
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

gtest_discover_tests(IRManglerTest)
