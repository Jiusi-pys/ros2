cmake_minimum_required(VERSION 3.8)
project(rmw_mdds_red_tests)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(std_msgs REQUIRED)

  # All lanes run host-only / single-process: select rmw_mdds and force its
  # in-process broker mode (no two-board DSoftBus, no external broker process).
  set(_red_test_env
    "RMW_IMPLEMENTATION=rmw_mdds_cpp"
    "RMW_MDDS_BROKER=0")

  # Generous timeout: the QoS lanes deliberately spin for a few seconds to let
  # leases expire / deadlines accrue.
  set(_red_test_timeout 60)

  macro(add_red_test _name _src)
    ament_add_gtest(${_name} ${_src}
      ENV ${_red_test_env}
      TIMEOUT ${_red_test_timeout})
    if(TARGET ${_name})
      target_include_directories(${_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/test")
      ament_target_dependencies(${_name} rclcpp std_msgs)
    endif()
  endmacro()

  add_red_test(test_qos_incompatible_red       test/test_qos_incompatible_red.cpp)
  add_red_test(test_qos_liveliness_lost_red    test/test_qos_liveliness_lost_red.cpp)
  add_red_test(test_qos_message_lost_red       test/test_qos_message_lost_red.cpp)
  add_red_test(test_reliability_mismatch_reject_red
                                               test/test_reliability_mismatch_reject_red.cpp)
  add_red_test(test_content_filter_red         test/test_content_filter_red.cpp)
endif()

ament_package()
