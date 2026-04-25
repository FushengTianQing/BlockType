add_library(blocktype-ir
  APInt.cpp
  IRBasicBlock.cpp
  IRBuilder.cpp
  IRConstant.cpp
  IRContext.cpp
  IRFormatVersion.cpp
  IRFunction.cpp
  IRInstruction.cpp
  IRModule.cpp
  IRPass.cpp
  IRSerializer.cpp
  IRType.cpp
  IRTypeContext.cpp
  IRValue.cpp
  IRVerifier.cpp
  BackendCapability.cpp
  DialectLoweringPass.cpp
  IRDialect.cpp
  IRTelemetry.cpp
  IRDebugMetadata.cpp
  IRDebugInfo.cpp
  TargetLayout.cpp
  raw_ostream.cpp
)

target_include_directories(blocktype-ir
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(blocktype-ir
  PUBLIC
    blocktype-basic
)

install(TARGETS blocktype-ir
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/blocktype/IR
  DESTINATION include/blocktype
  FILES_MATCHING PATTERN "*.h"
)
