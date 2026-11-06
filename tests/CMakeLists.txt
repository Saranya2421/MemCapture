find_package(GTest CONFIG REQUIRED)

enable_testing()

add_executable(test_safe_file_ops
    test_safe_file_ops.cpp
)

target_include_directories(test_safe_file_ops PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_safe_file_ops
    GTest::gtest
    GTest::gtest_main
)

set_target_properties(test_safe_file_ops PROPERTIES
    CXX_STANDARD 17
)

add_test(NAME SafeFileOpsTests COMMAND test_safe_file_ops)
