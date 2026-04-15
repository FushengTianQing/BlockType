# Unicode Support Library
add_library(blocktype-unicode STATIC
  UnicodeData.cpp
  UTF8.cpp
  Normalization.cpp
)

target_include_directories(blocktype-unicode PUBLIC
  ${CMAKE_SOURCE_DIR}/include
)

# Set C++ standard
set_target_properties(blocktype-unicode PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
