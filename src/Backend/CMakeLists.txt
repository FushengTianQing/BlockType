# Backend library (BackendBase, BackendRegistry)

add_library(blocktypeBackend
  BackendBase.cpp
  BackendRegistry.cpp
  IRToLLVMConverter.cpp
  LLVMBackend.cpp
  LLVMBackendRegistration.cpp
  InstructionSelector.cpp
  RegisterAllocator.cpp
  GreedyRegAlloc.cpp
  BasicRegAlloc.cpp
  DWARF5Emitter.cpp
  CodeViewEmitter.cpp
)

target_include_directories(blocktypeBackend
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_BINARY_DIR}/include
)

target_link_libraries(blocktypeBackend
  PUBLIC
    blocktype-basic
    blocktype-ir
    ${LLVM_LIBS}
)

set_target_properties(blocktypeBackend PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
  target_compile_options(blocktypeBackend PRIVATE /W4)
else()
  target_compile_options(blocktypeBackend PRIVATE -Wall -Wextra -Wpedantic)
endif()
