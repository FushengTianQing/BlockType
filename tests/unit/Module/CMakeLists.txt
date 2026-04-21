add_executable(blocktype-module-test
  ModuleManagerTest.cpp
)

target_link_libraries(blocktype-module-test
  PRIVATE
    blocktype-module
    blocktype-sema
    gtest
    gtest_main
)

add_test(NAME ModuleManagerTest COMMAND blocktype-module-test)
