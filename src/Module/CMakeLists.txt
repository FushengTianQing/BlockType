add_library(blocktype-module
  ModuleManager.cpp
  BMIReader.cpp
  BMIWriter.cpp
  ModuleDependencyGraph.cpp
)

target_link_libraries(blocktype-module
  PUBLIC
    blocktype-ast
    blocktype-sema
)
