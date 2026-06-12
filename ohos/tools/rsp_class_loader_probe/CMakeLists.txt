cmake_minimum_required(VERSION 3.16)
project(rsp_class_loader_probe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(class_loader REQUIRED)
find_package(console_bridge REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)

add_executable(rsp_class_loader_probe main.cpp)
target_link_libraries(rsp_class_loader_probe PRIVATE
  class_loader::class_loader
  console_bridge::console_bridge
  rclcpp::rclcpp
  rclcpp_components::component
)

install(TARGETS rsp_class_loader_probe DESTINATION bin)
