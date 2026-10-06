add_executable(WakeScheduleTest WakeScheduleTest.cpp)

target_include_directories(WakeScheduleTest PRIVATE ../../src/util)

target_link_libraries(WakeScheduleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WakeScheduleTest)
