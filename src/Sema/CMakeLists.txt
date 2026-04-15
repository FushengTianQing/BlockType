add_library(blocktype-sema
  Scope.cpp
)

target_link_libraries(blocktype-sema
  PUBLIC
    blocktype-ast
)