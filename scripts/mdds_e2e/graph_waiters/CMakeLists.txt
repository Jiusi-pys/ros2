cmake_minimum_required(VERSION 3.16)
project(mdds_graph_waiters LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(example_interfaces REQUIRED)
add_executable(graph_waiters graph_waiters.cpp)
target_link_libraries(graph_waiters PRIVATE rclcpp::rclcpp
  std_msgs::std_msgs__rosidl_typesupport_cpp
  example_interfaces::example_interfaces__rosidl_typesupport_cpp)
add_executable(graph_remote_waiters graph_waiters.cpp)
target_compile_definitions(graph_remote_waiters PRIVATE GRAPH_REMOTE_WAITER=1)
target_link_libraries(graph_remote_waiters PRIVATE rclcpp::rclcpp
  std_msgs::std_msgs__rosidl_typesupport_cpp
  example_interfaces::example_interfaces__rosidl_typesupport_cpp)
