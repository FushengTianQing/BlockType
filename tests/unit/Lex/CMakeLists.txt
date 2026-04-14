add_executable(blocktype-lex-test
  LexerTest.cpp
  PreprocessorTest.cpp
  TokenTest.cpp
)

target_include_directories(blocktype-lex-test PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(blocktype-lex-test
  blocktype-lex
  GTest::gtest
  GTest::gtest_main
)

blocktype_add_compile_options(blocktype-lex-test)

gtest_discover_tests(blocktype-lex-test)
