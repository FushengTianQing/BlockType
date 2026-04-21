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
