add_library(blocktype-parse
  OperatorPrecedence.cpp
  ParseExpr.cpp
  ParseExprCXX.cpp
  ParseStmt.cpp
  Parser.cpp
)

target_include_directories(blocktype-parse
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include
)

target_link_libraries(blocktype-parse
  PUBLIC
    blocktype-ast
    blocktype-lex
    blocktype-basic
)
