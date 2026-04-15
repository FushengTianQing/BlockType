# AST module for BlockType

add_library(blocktype-ast
  ASTContext.cpp
  ASTDumper.cpp
  Decl.cpp
  Expr.cpp
  Stmt.cpp
  Type.cpp
)

target_include_directories(blocktype-ast
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include
)

target_link_libraries(blocktype-ast
  PUBLIC
    blocktype-basic
    LLVMSupport
)
