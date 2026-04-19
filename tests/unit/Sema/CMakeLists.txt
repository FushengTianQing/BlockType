#===--- Sema/CMakeLists.txt - Sema Test CMake ---------------*- CMake -*-===//
#
# Part of the BlockType Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===//

add_executable(blocktype-sema-test
  SemaTest.cpp
  SymbolTableTest.cpp
  TypeCheckTest.cpp
  OverloadResolutionTest.cpp
  AccessControlTest.cpp
  ConstantExprTest.cpp
  NameLookupTest.cpp
  # TemplateInstantiationTest.cpp  # TODO: Fix after implementing InstantiateClassTemplate
  TemplateDeductionTest.cpp
  # ConceptTest.cpp  # TODO: Fix after removing TemplateArgumentList usage
  # SFINAETest.cpp  # TODO: Fix template-related test issues
  # VariadicTemplateTest.cpp  # TODO: Fix template-related test issues
)

target_include_directories(blocktype-sema-test PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(blocktype-sema-test
  blocktype-sema
  blocktype-ast
  blocktype-basic
  GTest::gtest
  GTest::gtest_main
)

blocktype_add_compile_options(blocktype-sema-test)

gtest_discover_tests(blocktype-sema-test)
