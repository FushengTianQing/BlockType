add_library(blocktype-basic
  Diagnostics.cpp
  FileManager.cpp
  Language.cpp
  SourceManager.cpp
  Translation.cpp
  Unicode.cpp
  UTF8Validator.cpp
)

target_include_directories(blocktype-basic PUBLIC
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_BINARY_DIR}/include
)

target_link_libraries(blocktype-basic PUBLIC
  ${LLVM_LIBS}
  blocktype-unicode
)

blocktype_add_compile_options(blocktype-basic)