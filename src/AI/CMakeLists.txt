add_library(blocktype-ai
  AIOrchestrator.cpp
  ResponseCache.cpp
  CostTracker.cpp
  OpenAIProvider.cpp
  ClaudeProvider.cpp
  LocalProvider.cpp
  QwenProvider.cpp
)

target_include_directories(blocktype-ai PUBLIC
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_BINARY_DIR}/include
)

target_link_libraries(blocktype-ai PUBLIC
  ${LLVM_LIBS}
  CURL::libcurl
  nlohmann_json::nlohmann_json
)

blocktype_add_compile_options(blocktype-ai)
