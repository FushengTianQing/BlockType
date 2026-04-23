#===--- Parse/CMakeLists.txt - Parse Test CMake --------------*- CMake -*-===//
#
# Part of the BlockType Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===//

add_executable(blocktype-parse-test
  ParserTest.cpp
  StatementTest.cpp
  ErrorRecoveryTest.cpp
  DeclarationTest.cpp
  ConceptTemplateParamTest.cpp
)

target_include_directories(blocktype-parse-test PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(blocktype-parse-test
  blocktype-parse
  blocktype-ast
  blocktype-lex
  blocktype-basic
  GTest::gtest
  GTest::gtest_main
)

blocktype_add_compile_options(blocktype-parse-test)

gtest_discover_tests(blocktype-parse-test)
