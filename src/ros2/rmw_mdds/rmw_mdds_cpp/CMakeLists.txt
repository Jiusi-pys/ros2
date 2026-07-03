cmake_minimum_required(VERSION 3.5)

project(rmw_mdds_cpp)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake_ros REQUIRED)
find_package(fastcdr REQUIRED)
find_package(rcutils REQUIRED)
find_package(rmw REQUIRED)
find_package(rmw_dds_common REQUIRED)
find_package(rosidl_runtime_cpp REQUIRED)
find_package(rosidl_dynamic_typesupport REQUIRED)
find_package(rosidl_typesupport_c REQUIRED)
find_package(rosidl_typesupport_cpp REQUIRED)
find_package(rosidl_typesupport_fastrtps_c REQUIRED)
find_package(rosidl_typesupport_fastrtps_cpp REQUIRED)
find_package(rosidl_typesupport_introspection_c REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)
find_package(OpenSSL QUIET)
find_package(Threads REQUIRED)

include_directories(include)

add_library(rmw_mdds_cpp
  src/bridge_backend.cpp
  src/context.cpp
  src/broker.cpp
  src/ipc_broker.cpp
  src/ipc_client.cpp
  src/ipc_protocol.cpp
  src/ipc_transport.cpp
  src/identifier.cpp
  src/loan_arena.cpp
  src/message_adapter.cpp
  src/rmw_features.cpp
  src/rmw_guard_condition.cpp
  src/rmw_get_implementation_identifier.cpp
  src/rmw_get_serialization_format.cpp
  src/rmw_init.cpp
  src/rmw_node.cpp
  src/rmw_publish.cpp
  src/rmw_publisher.cpp
  src/rmw_subscription.cpp
  src/rtps_participant.cpp
  src/rtps_protocol.cpp
  src/rtps_transport.cpp
  src/rmw_take.cpp
  src/rmw_unsupported.cpp
  src/rmw_wait.cpp
  src/rmw_wait_set.cpp
  src/string_adapter.cpp
)
target_link_libraries(rmw_mdds_cpp PUBLIC
  rmw::rmw
)
target_link_libraries(rmw_mdds_cpp PRIVATE
  ${CMAKE_DL_LIBS}
  rcutils::rcutils
  rmw_dds_common::rmw_dds_common_library
  Threads::Threads
  fastcdr
  rosidl_dynamic_typesupport::rosidl_dynamic_typesupport
  rosidl_typesupport_c::rosidl_typesupport_c
  rosidl_typesupport_cpp::rosidl_typesupport_cpp
  rosidl_typesupport_fastrtps_c::rosidl_typesupport_fastrtps_c
  rosidl_typesupport_fastrtps_cpp::rosidl_typesupport_fastrtps_cpp
  rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
)
if(OpenSSL_FOUND)
  target_compile_definitions(rmw_mdds_cpp PRIVATE RMW_MDDS_HAS_OPENSSL=1)
  target_link_libraries(rmw_mdds_cpp PRIVATE OpenSSL::Crypto)
endif()

configure_rmw_library(rmw_mdds_cpp)

add_executable(rmw_mdds_broker
  src/bridge_backend.cpp
  src/ipc_broker.cpp
  src/ipc_protocol.cpp
  src/ipc_transport.cpp
  src/rmw_mdds_broker_main.cpp
)
target_include_directories(rmw_mdds_broker PRIVATE src)
target_link_libraries(rmw_mdds_broker PRIVATE
  ${CMAKE_DL_LIBS}
  rmw::rmw
  Threads::Threads
)

ament_export_include_directories("include/${PROJECT_NAME}")
ament_export_libraries(rmw_mdds_cpp)
ament_export_dependencies(
  fastcdr
  rcutils
  rmw
  rosidl_dynamic_typesupport
  rosidl_typesupport_cpp
  rosidl_typesupport_fastrtps_cpp
  rosidl_typesupport_introspection_cpp)

register_rmw_implementation(
  "c:rosidl_typesupport_c:rosidl_typesupport_introspection_c"
  "cpp:rosidl_typesupport_cpp:rosidl_typesupport_introspection_cpp")

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_path(RMW_MDDS_STD_MSGS_INCLUDE_DIR
    NAMES std_msgs/msg/string.hpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/std_msgs
  )
  find_path(RMW_MDDS_GEOMETRY_MSGS_INCLUDE_DIR
    NAMES geometry_msgs/msg/pose_stamped.hpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/geometry_msgs
  )
  find_library(RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP
    NAMES std_msgs__rosidl_typesupport_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_MSGS_TYPESUPPORT_C
    NAMES std_msgs__rosidl_typesupport_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_MSGS_GENERATOR_C
    NAMES std_msgs__rosidl_generator_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_C
    NAMES std_msgs__rosidl_typesupport_introspection_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP
    NAMES std_msgs__rosidl_typesupport_introspection_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_MSGS_TYPESUPPORT_FASTRTPS_CPP
    NAMES std_msgs__rosidl_typesupport_fastrtps_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_GEOMETRY_MSGS_TYPESUPPORT_CPP
    NAMES geometry_msgs__rosidl_typesupport_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_GEOMETRY_MSGS_TYPESUPPORT_INTROSPECTION_CPP
    NAMES geometry_msgs__rosidl_typesupport_introspection_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_GEOMETRY_MSGS_GENERATOR_C
    NAMES geometry_msgs__rosidl_generator_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_path(RMW_MDDS_STD_SRVS_INCLUDE_DIR
    NAMES std_srvs/srv/detail/trigger__type_support.hpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/std_srvs
  )
  find_library(RMW_MDDS_STD_SRVS_TYPESUPPORT_CPP
    NAMES std_srvs__rosidl_typesupport_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_SRVS_TYPESUPPORT_C
    NAMES std_srvs__rosidl_typesupport_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_SRVS_TYPESUPPORT_INTROSPECTION_CPP
    NAMES std_srvs__rosidl_typesupport_introspection_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_SRVS_TYPESUPPORT_INTROSPECTION_C
    NAMES std_srvs__rosidl_typesupport_introspection_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_STD_SRVS_GENERATOR_C
    NAMES std_srvs__rosidl_generator_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_path(RMW_MDDS_SERVICE_MSGS_INCLUDE_DIR
    NAMES service_msgs/msg/detail/service_event_info__struct.h
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/service_msgs
  )
  find_library(RMW_MDDS_SERVICE_MSGS_GENERATOR_C
    NAMES service_msgs__rosidl_generator_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_path(RMW_MDDS_BUILTIN_INTERFACES_INCLUDE_DIR
    NAMES builtin_interfaces/msg/detail/time__struct.h
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/builtin_interfaces
  )
  find_library(RMW_MDDS_BUILTIN_INTERFACES_GENERATOR_C
    NAMES builtin_interfaces__rosidl_generator_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_path(RMW_MDDS_EXAMPLE_INTERFACES_INCLUDE_DIR
    NAMES example_interfaces/srv/detail/add_two_ints__type_support.hpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/example_interfaces
  )
  find_library(RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_CPP
    NAMES example_interfaces__rosidl_typesupport_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_INTROSPECTION_CPP
    NAMES example_interfaces__rosidl_typesupport_introspection_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_EXAMPLE_INTERFACES_GENERATOR_C
    NAMES example_interfaces__rosidl_generator_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_path(RMW_MDDS_TEST_MSGS_INCLUDE_DIR
    NAMES test_msgs/srv/detail/arrays__type_support.hpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/test_msgs
  )
  find_library(RMW_MDDS_TEST_MSGS_TYPESUPPORT_CPP
    NAMES test_msgs__rosidl_typesupport_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_TEST_MSGS_TYPESUPPORT_INTROSPECTION_CPP
    NAMES test_msgs__rosidl_typesupport_introspection_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_TEST_MSGS_GENERATOR_C
    NAMES test_msgs__rosidl_generator_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_path(RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_INCLUDE_DIR
    NAMES type_description_interfaces/msg/field_type.hpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include/type_description_interfaces
  )
  find_library(RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_TYPESUPPORT_CPP
    NAMES type_description_interfaces__rosidl_typesupport_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_TYPESUPPORT_INTROSPECTION_CPP
    NAMES type_description_interfaces__rosidl_typesupport_introspection_cpp
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  find_library(RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_GENERATOR_C
    NAMES type_description_interfaces__rosidl_generator_c
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib
  )
  if(NOT RMW_MDDS_STD_MSGS_INCLUDE_DIR OR
      NOT RMW_MDDS_GEOMETRY_MSGS_INCLUDE_DIR OR
      NOT RMW_MDDS_STD_MSGS_TYPESUPPORT_C OR
      NOT RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP OR
      NOT RMW_MDDS_STD_MSGS_GENERATOR_C OR
      NOT RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_C OR
      NOT RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP OR
      NOT RMW_MDDS_STD_MSGS_TYPESUPPORT_FASTRTPS_CPP OR
      NOT RMW_MDDS_GEOMETRY_MSGS_TYPESUPPORT_CPP OR
      NOT RMW_MDDS_GEOMETRY_MSGS_TYPESUPPORT_INTROSPECTION_CPP OR
      NOT RMW_MDDS_GEOMETRY_MSGS_GENERATOR_C OR
      NOT RMW_MDDS_STD_SRVS_INCLUDE_DIR OR
      NOT RMW_MDDS_STD_SRVS_TYPESUPPORT_C OR
      NOT RMW_MDDS_STD_SRVS_TYPESUPPORT_CPP OR
      NOT RMW_MDDS_STD_SRVS_TYPESUPPORT_INTROSPECTION_C OR
      NOT RMW_MDDS_STD_SRVS_TYPESUPPORT_INTROSPECTION_CPP OR
      NOT RMW_MDDS_STD_SRVS_GENERATOR_C OR
      NOT RMW_MDDS_SERVICE_MSGS_INCLUDE_DIR OR
      NOT RMW_MDDS_SERVICE_MSGS_GENERATOR_C OR
      NOT RMW_MDDS_BUILTIN_INTERFACES_INCLUDE_DIR OR
      NOT RMW_MDDS_BUILTIN_INTERFACES_GENERATOR_C OR
      NOT RMW_MDDS_EXAMPLE_INTERFACES_INCLUDE_DIR OR
      NOT RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_CPP OR
      NOT RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_INTROSPECTION_CPP OR
      NOT RMW_MDDS_EXAMPLE_INTERFACES_GENERATOR_C OR
      NOT RMW_MDDS_TEST_MSGS_INCLUDE_DIR OR
      NOT RMW_MDDS_TEST_MSGS_TYPESUPPORT_CPP OR
      NOT RMW_MDDS_TEST_MSGS_TYPESUPPORT_INTROSPECTION_CPP OR
      NOT RMW_MDDS_TEST_MSGS_GENERATOR_C OR
      NOT RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_INCLUDE_DIR OR
      NOT RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_TYPESUPPORT_CPP OR
      NOT RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_TYPESUPPORT_INTROSPECTION_CPP OR
      NOT RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_GENERATOR_C)
    message(FATAL_ERROR
      "std_msgs/geometry_msgs/std_srvs/example_interfaces/test_msgs/type_description_interfaces "
      "headers and typesupport libraries are required for tests")
  endif()

  ament_add_gtest(test_identity test/test_identity.cpp)
  target_link_libraries(test_identity rmw::rmw rmw_mdds_cpp)

  ament_add_gtest(test_symbol_surface test/test_symbol_surface.cpp)
  target_compile_definitions(test_symbol_surface PRIVATE
    "RMW_MDDS_LIBRARY_PATH=\"$<TARGET_FILE:rmw_mdds_cpp>\"")
  target_link_libraries(test_symbol_surface ${CMAKE_DL_LIBS})

  ament_add_gtest(test_logging test/test_logging.cpp)
  target_link_libraries(test_logging rmw::rmw rmw_mdds_cpp)

  ament_add_gtest(test_lifecycle test/test_lifecycle.cpp)
  target_sources(test_lifecycle PRIVATE
    src/rtps_participant.cpp
    src/rtps_transport.cpp
    src/rtps_protocol.cpp
  )
  target_include_directories(test_lifecycle PRIVATE
    src
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
  )
  target_link_libraries(test_lifecycle
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
  )

  ament_add_gtest(test_qos test/test_qos.cpp)
  target_link_libraries(test_qos rmw::rmw rmw_mdds_cpp)

  ament_add_gtest(test_event test/test_event.cpp)
  target_include_directories(test_event PRIVATE
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_GEOMETRY_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_BUILTIN_INTERFACES_INCLUDE_DIR}
  )
  target_link_libraries(test_event
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
    rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
    ${CMAKE_INSTALL_PREFIX}/lib/librosidl_typesupport_introspection_c.so
    ${CMAKE_INSTALL_PREFIX}/lib/librosidl_typesupport_introspection_cpp.so
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
    ${RMW_MDDS_GEOMETRY_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_GEOMETRY_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_GEOMETRY_MSGS_GENERATOR_C}
    ${RMW_MDDS_BUILTIN_INTERFACES_GENERATOR_C}
  )

  ament_add_gtest(test_bridge_required test/test_bridge_required.cpp)
  target_include_directories(test_bridge_required PRIVATE
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_STD_SRVS_INCLUDE_DIR}
  )
  target_link_libraries(test_bridge_required
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_SRVS_GENERATOR_C}
  )

  ament_add_gtest(test_pubsub_inproc test/test_pubsub_inproc.cpp)
  target_include_directories(test_pubsub_inproc PRIVATE
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_EXAMPLE_INTERFACES_INCLUDE_DIR}
    ${RMW_MDDS_SERVICE_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_BUILTIN_INTERFACES_INCLUDE_DIR}
    ${RMW_MDDS_TEST_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_INCLUDE_DIR}
  )
  target_link_libraries(test_pubsub_inproc
    rmw::rmw
    rmw_mdds_cpp
    rosidl_typesupport_c::rosidl_typesupport_c
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_dynamic_typesupport::rosidl_dynamic_typesupport
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    fastcdr
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_C}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_C}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_FASTRTPS_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
    ${RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_CPP}
    ${RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_EXAMPLE_INTERFACES_GENERATOR_C}
    ${RMW_MDDS_TEST_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_TEST_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_TEST_MSGS_GENERATOR_C}
    ${RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_TYPESUPPORT_CPP}
    ${RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_TYPE_DESCRIPTION_INTERFACES_GENERATOR_C}
  )

  ament_add_gtest(test_graph test/test_graph.cpp)
  target_include_directories(test_graph PRIVATE
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_STD_SRVS_INCLUDE_DIR}
  )
  target_link_libraries(test_graph
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_c::rosidl_typesupport_c
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_SRVS_GENERATOR_C}
  )

  ament_add_gtest(test_ipc_protocol test/test_ipc_protocol.cpp)
  target_sources(test_ipc_protocol PRIVATE
    src/ipc_protocol.cpp
  )
  target_include_directories(test_ipc_protocol PRIVATE
    src
  )
  target_link_libraries(test_ipc_protocol
    rmw::rmw
    rmw_mdds_cpp
  )

  ament_add_gtest(test_rtps_protocol test/test_rtps_protocol.cpp)
  target_sources(test_rtps_protocol PRIVATE
    src/rtps_protocol.cpp
  )
  target_include_directories(test_rtps_protocol PRIVATE
    src
  )
  target_link_libraries(test_rtps_protocol
    rmw::rmw
    rmw_mdds_cpp
  )

  ament_add_gtest(test_rtps_transport test/test_rtps_transport.cpp)
  target_sources(test_rtps_transport PRIVATE
    src/rtps_transport.cpp
    src/rtps_protocol.cpp
  )
  target_include_directories(test_rtps_transport PRIVATE
    src
  )
  target_link_libraries(test_rtps_transport
    rmw::rmw
    rmw_mdds_cpp
  )

  ament_add_gtest(test_rtps_participant test/test_rtps_participant.cpp)
  target_sources(test_rtps_participant PRIVATE
    src/rtps_participant.cpp
    src/rtps_transport.cpp
    src/rtps_protocol.cpp
  )
  target_include_directories(test_rtps_participant PRIVATE
    src
  )
  target_link_libraries(test_rtps_participant
    rmw::rmw
    rmw_mdds_cpp
  )

  ament_add_gtest(test_ipc_transport test/test_ipc_transport.cpp)
  target_sources(test_ipc_transport PRIVATE
    src/ipc_protocol.cpp
    src/ipc_transport.cpp
  )
  target_include_directories(test_ipc_transport PRIVATE
    src
  )
  target_link_libraries(test_ipc_transport
    rmw::rmw
    rmw_mdds_cpp
  )

  add_library(fake_mdds_bridge SHARED test/fake_mdds_bridge.cpp)

  ament_add_gtest(test_ipc_broker test/test_ipc_broker.cpp)
  target_compile_definitions(test_ipc_broker PRIVATE
    "FAKE_MDDS_BRIDGE_PATH=\"$<TARGET_FILE:fake_mdds_bridge>\"")
  target_sources(test_ipc_broker PRIVATE
    src/bridge_backend.cpp
    src/ipc_broker.cpp
    src/ipc_protocol.cpp
    src/ipc_transport.cpp
  )
  target_include_directories(test_ipc_broker PRIVATE
    src
  )
  target_link_libraries(test_ipc_broker
    ${CMAKE_DL_LIBS}
    fake_mdds_bridge
    rmw::rmw
    rmw_mdds_cpp
  )

  ament_add_gtest(test_broker_mode test/test_broker_mode.cpp)
  target_sources(test_broker_mode PRIVATE
    src/bridge_backend.cpp
    src/ipc_broker.cpp
    src/ipc_protocol.cpp
    src/ipc_transport.cpp
  )
  target_include_directories(test_broker_mode PRIVATE
    src
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
  )
  target_link_libraries(test_broker_mode
    ${CMAKE_DL_LIBS}
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
  )

  ament_add_gtest(
    test_broker_process test/test_broker_process.cpp
    SKIP_LINKING_MAIN_LIBRARIES)
  target_sources(test_broker_process PRIVATE
    src/ipc_protocol.cpp
    src/ipc_transport.cpp
  )
  target_include_directories(test_broker_process PRIVATE
    src
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_STD_SRVS_INCLUDE_DIR}
    ${RMW_MDDS_SERVICE_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_BUILTIN_INTERFACES_INCLUDE_DIR}
  )
  target_link_libraries(test_broker_process
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_c::rosidl_typesupport_c
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_C}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_INTROSPECTION_C}
    ${RMW_MDDS_STD_SRVS_GENERATOR_C}
    ${RMW_MDDS_SERVICE_MSGS_GENERATOR_C}
    ${RMW_MDDS_BUILTIN_INTERFACES_GENERATOR_C}
  )

  ament_add_gtest(test_service_inproc test/test_service_inproc.cpp)
  target_include_directories(test_service_inproc PRIVATE
    ${RMW_MDDS_EXAMPLE_INTERFACES_INCLUDE_DIR}
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_STD_SRVS_INCLUDE_DIR}
    ${RMW_MDDS_SERVICE_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_BUILTIN_INTERFACES_INCLUDE_DIR}
    ${RMW_MDDS_TEST_MSGS_INCLUDE_DIR}
  )
  target_link_libraries(test_service_inproc
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_c::rosidl_typesupport_c
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_C}
    ${RMW_MDDS_STD_SRVS_TYPESUPPORT_INTROSPECTION_C}
    ${RMW_MDDS_STD_SRVS_GENERATOR_C}
    ${RMW_MDDS_SERVICE_MSGS_GENERATOR_C}
    ${RMW_MDDS_BUILTIN_INTERFACES_GENERATOR_C}
    ${RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_CPP}
    ${RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_EXAMPLE_INTERFACES_GENERATOR_C}
    ${RMW_MDDS_TEST_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_TEST_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_TEST_MSGS_GENERATOR_C}
  )

  ament_add_gtest(test_bridge_backend test/test_bridge_backend.cpp)
  target_compile_definitions(test_bridge_backend PRIVATE
    "FAKE_MDDS_BRIDGE_PATH=\"$<TARGET_FILE:fake_mdds_bridge>\"")
  target_include_directories(test_bridge_backend PRIVATE
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_GEOMETRY_MSGS_INCLUDE_DIR}
    ${RMW_MDDS_EXAMPLE_INTERFACES_INCLUDE_DIR}
    ${RMW_MDDS_BUILTIN_INTERFACES_INCLUDE_DIR}
  )
  target_link_libraries(test_bridge_backend
    fake_mdds_bridge
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
    ${RMW_MDDS_GEOMETRY_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_GEOMETRY_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_GEOMETRY_MSGS_GENERATOR_C}
    ${RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_CPP}
    ${RMW_MDDS_EXAMPLE_INTERFACES_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_EXAMPLE_INTERFACES_GENERATOR_C}
  )

  ament_add_gtest(test_bridge_backend_loaned test/test_bridge_backend_loaned.cpp)
  target_compile_definitions(test_bridge_backend_loaned PRIVATE
    "FAKE_MDDS_BRIDGE_PATH=\"$<TARGET_FILE:fake_mdds_bridge>\"")
  target_sources(test_bridge_backend_loaned PRIVATE
    src/bridge_backend.cpp
  )
  target_include_directories(test_bridge_backend_loaned PRIVATE
    src
  )
  target_link_libraries(test_bridge_backend_loaned
    ${CMAKE_DL_LIBS}
    fake_mdds_bridge
    rmw::rmw
  )

  ament_add_gtest(test_loan_arena test/test_loan_arena.cpp)
  target_sources(test_loan_arena PRIVATE src/loan_arena.cpp)
  target_include_directories(test_loan_arena PRIVATE src)
  target_link_libraries(test_loan_arena rmw_mdds_cpp)

  ament_add_gtest(test_bridge_loaned_rmw test/test_bridge_loaned_rmw.cpp)
  target_sources(test_bridge_loaned_rmw PRIVATE src/string_adapter.cpp)
  target_compile_definitions(test_bridge_loaned_rmw PRIVATE
    "FAKE_MDDS_BRIDGE_PATH=\"$<TARGET_FILE:fake_mdds_bridge>\"")
  target_include_directories(test_bridge_loaned_rmw PRIVATE
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
    ${CMAKE_INSTALL_PREFIX}/include/rosidl_typesupport_introspection_c
    ${CMAKE_INSTALL_PREFIX}/include/rosidl_typesupport_introspection_cpp
  )
  target_link_libraries(test_bridge_loaned_rmw
    fake_mdds_bridge
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
    rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_C}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_C}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
  )

  ament_add_gtest(test_bridge_loaned_take_rmw test/test_bridge_loaned_take_rmw.cpp)
  target_compile_definitions(test_bridge_loaned_take_rmw PRIVATE
    "FAKE_MDDS_BRIDGE_PATH=\"$<TARGET_FILE:fake_mdds_bridge>\"")
  target_include_directories(test_bridge_loaned_take_rmw PRIVATE
    ${RMW_MDDS_STD_MSGS_INCLUDE_DIR}
  )
  target_link_libraries(test_bridge_loaned_take_rmw
    fake_mdds_bridge
    rmw::rmw
    rmw_mdds_cpp
    rosidl_runtime_cpp::rosidl_runtime_cpp
    rosidl_dynamic_typesupport::rosidl_dynamic_typesupport
    rosidl_typesupport_cpp::rosidl_typesupport_cpp
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_CPP}
    ${RMW_MDDS_STD_MSGS_TYPESUPPORT_INTROSPECTION_CPP}
    ${RMW_MDDS_STD_MSGS_GENERATOR_C}
  )
endif()

ament_package()

install(
  DIRECTORY include/
  DESTINATION include/${PROJECT_NAME}
)

install(
  TARGETS rmw_mdds_cpp
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(
  TARGETS rmw_mdds_broker
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
