cmake_minimum_required(VERSION 3.16)
project(rosbag_storage_probe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(class_loader REQUIRED)
find_package(rosbag2_storage REQUIRED)

add_executable(rosbag_storage_probe main.cpp)
target_link_libraries(rosbag_storage_probe PRIVATE
  class_loader::class_loader
  rosbag2_storage::rosbag2_storage
)

install(TARGETS rosbag_storage_probe DESTINATION bin)
