# AST unit tests

add_executable(ast-tests
  ASTTest.cpp
  Stage71Test.cpp
)

target_link_libraries(ast-tests
  PRIVATE
    blocktype-ast
    blocktype-basic
    GTest::gtest_main
)

gtest_discover_tests(ast-tests)
