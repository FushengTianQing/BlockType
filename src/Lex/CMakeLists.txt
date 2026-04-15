add_library(blocktype-lex
  HeaderSearch.cpp
  Lexer.cpp
  Preprocessor.cpp
  TokenKinds.cpp
)

target_include_directories(blocktype-lex PUBLIC
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_BINARY_DIR}/include
)

target_link_libraries(blocktype-lex PUBLIC
  blocktype-basic
  blocktype-unicode
)

blocktype_add_compile_options(blocktype-lex)