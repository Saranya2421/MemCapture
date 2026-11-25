find_package(GTest CONFIG REQUIRED)

add_executable(MemCaptureTests
    CgroupPathTest.cpp
    GpuMeasurementPolicyTest.cpp
    GroupManagerTest.cpp
    GroupsFileTest.cpp
    ProcessMetricLogTest.cpp
    ProcessStringLimitTest.cpp
    ReportTemplateTest.cpp
    ../GroupManager.cpp
    ../Process.cpp
    ../MemoryMetric.cpp
    ../ProcessMetric.cpp
    ../Measurement.cpp
    ../Procrank.cpp
    ../Metadata.cpp
    ../JsonReportGenerator.cpp
    ../FileParsers/MemInfo.cpp
    ../FileParsers/Smaps.cpp
)

set_target_properties(MemCaptureTests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(MemCaptureTests PRIVATE "${PROJECT_SOURCE_DIR}")
target_link_libraries(MemCaptureTests PRIVATE
    GTest::gtest_main
    Threads::Threads
    nlohmann_json::nlohmann_json
    pantor::inja
)

add_test(NAME MemCaptureTests COMMAND MemCaptureTests)
set_tests_properties(MemCaptureTests PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

