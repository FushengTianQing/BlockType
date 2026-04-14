# 编译选项配置

function(blocktype_add_compile_options target)
  target_compile_options(${target} PRIVATE
    # 警告
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    
    # 调试符号
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
  )
  
  # 设置 C++ 标准
  target_compile_features(${target} PUBLIC cxx_std_23)
  
  # 定义版本宏
  target_compile_definitions(${target} PRIVATE
    BLOCKTYPE_VERSION_MAJOR=${BLOCKTYPE_VERSION_MAJOR}
    BLOCKTYPE_VERSION_MINOR=${BLOCKTYPE_VERSION_MINOR}
    BLOCKTYPE_VERSION_PATCH=${BLOCKTYPE_VERSION_PATCH}
  )
endfunction()
