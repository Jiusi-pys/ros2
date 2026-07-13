cmake_minimum_required(VERSION 3.16)
project(rmw_mdds_dynamic_loan_probe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
if(CMAKE_CROSSCOMPILING AND (NOT TARGET Python3::Python OR NOT TARGET Python3::NumPy))
  find_package(Python3 REQUIRED COMPONENTS Interpreter Development NumPy)
elseif(NOT TARGET Python3::Interpreter OR NOT TARGET Python3::Python)
  find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
endif()
if(NOT CMAKE_CROSSCOMPILING AND NOT TARGET Python3::NumPy)
  add_library(Python3::NumPy INTERFACE IMPORTED)
endif()
find_package(rcutils REQUIRED)
find_package(rmw REQUIRED)
find_package(rmw_mdds_cpp REQUIRED)
find_package(rosidl_runtime_cpp REQUIRED)
find_package(rosidl_typesupport_cpp REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(rmw_mdds_dynamic_loan_probe main.cpp)
ament_target_dependencies(rmw_mdds_dynamic_loan_probe
  rcutils
  rmw
  rmw_mdds_cpp
  rosidl_runtime_cpp
  rosidl_typesupport_cpp
  std_msgs
)

add_executable(rmw_mdds_broker_dynamic_loan_probe broker_subscription_main.cpp)
ament_target_dependencies(rmw_mdds_broker_dynamic_loan_probe
  rcutils
  rmw
  rmw_mdds_cpp
  rosidl_runtime_cpp
  rosidl_typesupport_cpp
  std_msgs
)

install(TARGETS
  rmw_mdds_dynamic_loan_probe
  rmw_mdds_broker_dynamic_loan_probe
  DESTINATION lib/${PROJECT_NAME}
)
install(PROGRAMS broker_dynamic_loan_board_runner.sh
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
