# Frontend library (CompilerInstance, CompilerInvocation, FrontendBase, FrontendRegistry, IRTypeMapper, ASTToIRConverter)

add_library(blocktypeFrontend
  CompilerInvocation.cpp
  CompilerInstance.cpp
  FrontendBase.cpp
  FrontendRegistry.cpp
  IRTypeMapper.cpp
  ASTToIRConverter.cpp
  IREmitExpr.cpp
  IREmitStmt.cpp
)

target_include_directories(blocktypeFrontend
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include
)

target_link_libraries(blocktypeFrontend
  PUBLIC
    blocktype-basic
    blocktype-lex
    blocktype-parse
    blocktype-sema
    blocktype-ast
    blocktype-codegen
    ${LLVM_LIBS}
)

set_target_properties(blocktypeFrontend PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
  target_compile_options(blocktypeFrontend PRIVATE /W4)
else()
  target_compile_options(blocktypeFrontend PRIVATE -Wall -Wextra -Wpedantic)
endif()
