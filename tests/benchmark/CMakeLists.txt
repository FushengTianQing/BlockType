# Benchmark tests for BlockType

add_executable(lexer-benchmark LexerBenchmark.cpp)

target_link_libraries(lexer-benchmark
  PRIVATE
  blocktype-lex
  blocktype-basic
  LLVMSupport
)

set_target_properties(lexer-benchmark PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Parser benchmark
add_executable(parser-benchmark ParserBenchmark.cpp)

target_link_libraries(parser-benchmark
  PRIVATE
  blocktype-parse
  blocktype-ast
  blocktype-lex
  blocktype-basic
)

set_target_properties(parser-benchmark PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
