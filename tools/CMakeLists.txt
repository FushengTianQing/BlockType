add_executable(blocktype
  driver.cpp
)

target_link_libraries(blocktype PRIVATE
  blocktype-basic
  blocktype-codegen
  blocktype-ai
  ${LLVM_LIBS}
)

blocktype_add_compile_options(blocktype)