cmake_minimum_required(VERSION 3.16)
project(rmw_mdds_action_bag_probe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
if(NOT TARGET Python3::Python OR NOT TARGET Python3::NumPy)
  find_package(Python3 REQUIRED COMPONENTS Interpreter Development NumPy)
endif()
find_package(action_tutorials_interfaces REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)

add_executable(rmw_mdds_action_bag_probe main.cpp)
ament_target_dependencies(rmw_mdds_action_bag_probe
  action_tutorials_interfaces
  rclcpp
  rclcpp_action
)

install(TARGETS rmw_mdds_action_bag_probe
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
