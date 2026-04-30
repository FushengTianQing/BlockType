#===--- CodeGen/CMakeLists.txt - CodeGen Test CMake ----------*- CMake -*-===#

add_executable(blocktype-codegen-test
  ABIInfoTest.cpp
  CodeGenTypesTest.cpp
  CodeGenConstantTest.cpp
  CodeGenExprTest.cpp
  CodeGenStmtTest.cpp
  CodeGenFunctionTest.cpp
  CodeGenClassTest.cpp
  CodeGenDebugInfoTest.cpp
  BuiltinFunctionsTest.cpp
  CallingConventionTest.cpp
  ManglerTest.cpp
  TargetInfoTest.cpp
  OptimizationTest.cpp
  TargetOptimizationTest.cpp
)

target_include_directories(blocktype-codegen-test PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(blocktype-codegen-test PRIVATE
  -Wall -Wextra -Wpedantic -Wno-error -Wno-deprecated-declarations -Wno-unused-parameter
)

target_compile_features(blocktype-codegen-test PUBLIC cxx_std_23)

target_link_libraries(blocktype-codegen-test
  blocktypeFrontend
  blocktypeBackend
  blocktype-ir
  blocktype-codegen
  blocktype-sema
  blocktype-ast
  blocktype-basic
  ${LLVM_LIBS}
  GTest::gtest
  GTest::gtest_main
)

gtest_discover_tests(blocktype-codegen-test)
