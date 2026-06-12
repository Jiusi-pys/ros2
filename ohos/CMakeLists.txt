cmake_minimum_required(VERSION 3.16)

project(ros2_ohos_smoke LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ROS2_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "Path to the ROS 2 workspace root")
set(RCUTILS_DIR "${ROS2_ROOT}/src/ros2/rcutils" CACHE PATH "Path to rcutils")
set(RCPPUTILS_DIR "${ROS2_ROOT}/src/ros2/rcpputils" CACHE PATH "Path to rcpputils")
option(ROS2_OHOS_ENABLE_RMW_SMOKE "Build the publish/subscribe smoke binary when rcl/rmw are available" OFF)
set(ROS2_OHOS_RMW_PREFIX "" CACHE PATH "Install prefix containing standalone OHOS builds of rcl/rmw/typesupport packages")
set(ROS2_OHOS_FASTDDS_PREFIX "${ROS2_ROOT}/install/ohos-fastdds" CACHE PATH "Install prefix containing standalone OHOS FastDDS dependencies")

if(NOT EXISTS "${RCUTILS_DIR}/include/rcutils/logging.h")
  message(FATAL_ERROR "rcutils sources not found at ${RCUTILS_DIR}")
endif()
if(NOT EXISTS "${RCPPUTILS_DIR}/include/rcpputils/shared_library.hpp")
  message(FATAL_ERROR "rcpputils sources not found at ${RCPPUTILS_DIR}")
endif()

if(NOT Python3_EXECUTABLE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()

set(GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(GENERATED_LOGGING_HEADER "${GENERATED_INCLUDE_DIR}/rcutils/logging_macros.h")

file(MAKE_DIRECTORY "${GENERATED_INCLUDE_DIR}/rcutils")

add_custom_command(
  OUTPUT "${GENERATED_LOGGING_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_LIST_DIR}/tools/generate_logging_macros.py"
          "--logging-py" "${RCUTILS_DIR}/rcutils/logging.py"
          "--output" "${GENERATED_LOGGING_HEADER}"
  DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/tools/generate_logging_macros.py"
    "${RCUTILS_DIR}/rcutils/logging.py"
  VERBATIM
  COMMENT "Generating rcutils/logging_macros.h without ament/empy")

set(RCUTILS_SOURCES
  "${RCUTILS_DIR}/src/allocator.c"
  "${RCUTILS_DIR}/src/array_list.c"
  "${RCUTILS_DIR}/src/char_array.c"
  "${RCUTILS_DIR}/src/cmdline_parser.c"
  "${RCUTILS_DIR}/src/env.c"
  "${RCUTILS_DIR}/src/error_handling.c"
  "${RCUTILS_DIR}/src/filesystem.c"
  "${RCUTILS_DIR}/src/find.c"
  "${RCUTILS_DIR}/src/format_string.c"
  "${RCUTILS_DIR}/src/hash_map.c"
  "${RCUTILS_DIR}/src/logging.c"
  "${RCUTILS_DIR}/src/process.c"
  "${RCUTILS_DIR}/src/qsort.c"
  "${RCUTILS_DIR}/src/repl_str.c"
  "${RCUTILS_DIR}/src/sha256.c"
  "${RCUTILS_DIR}/src/shared_library.c"
  "${RCUTILS_DIR}/src/snprintf.c"
  "${RCUTILS_DIR}/src/split.c"
  "${RCUTILS_DIR}/src/strcasecmp.c"
  "${RCUTILS_DIR}/src/strdup.c"
  "${RCUTILS_DIR}/src/strerror.c"
  "${RCUTILS_DIR}/src/string_array.c"
  "${RCUTILS_DIR}/src/string_map.c"
  "${RCUTILS_DIR}/src/time.c"
  "${RCUTILS_DIR}/src/time_unix.c"
  "${RCUTILS_DIR}/src/uint8_array.c"
  "${GENERATED_LOGGING_HEADER}")

add_library(ros2_rcutils SHARED ${RCUTILS_SOURCES})
target_include_directories(ros2_rcutils
  PUBLIC
    "${RCUTILS_DIR}/include"
    "${GENERATED_INCLUDE_DIR}")
target_compile_definitions(ros2_rcutils PRIVATE RCUTILS_BUILDING_DLL)
target_compile_options(ros2_rcutils PRIVATE -Wall -Wextra -Wconversion -Wno-sign-conversion -Wpedantic)
target_link_libraries(ros2_rcutils PUBLIC dl m)

set(RCPPUTILS_SOURCES
  "${RCPPUTILS_DIR}/src/asserts.cpp"
  "${RCPPUTILS_DIR}/src/env.cpp"
  "${RCPPUTILS_DIR}/src/filesystem_helper.cpp"
  "${RCPPUTILS_DIR}/src/find_library.cpp"
  "${RCPPUTILS_DIR}/src/process.cpp"
  "${RCPPUTILS_DIR}/src/shared_library.cpp")

add_library(ros2_rcpputils SHARED ${RCPPUTILS_SOURCES})
target_include_directories(ros2_rcpputils
  PUBLIC
    "${RCPPUTILS_DIR}/include"
    "${RCUTILS_DIR}/include"
    "${GENERATED_INCLUDE_DIR}")
target_compile_options(ros2_rcpputils PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ros2_rcpputils PUBLIC ros2_rcutils dl)

add_library(ros2_ohos_dummy SHARED "${CMAKE_CURRENT_LIST_DIR}/src/dummy_shared_library.cpp")
target_compile_options(ros2_ohos_dummy PRIVATE -Wall -Wextra -Wpedantic)

add_library(ros2_ohos_libintl_shim SHARED "${CMAKE_CURRENT_LIST_DIR}/src/libintl_shim.c")
set_target_properties(ros2_ohos_libintl_shim PROPERTIES
  OUTPUT_NAME intl
  VERSION 8.0.0
  SOVERSION 8)
target_compile_options(ros2_ohos_libintl_shim PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ros2_ohos_smoke "${CMAKE_CURRENT_LIST_DIR}/src/smoke_test.cpp")
target_include_directories(ros2_ohos_smoke PRIVATE
  "${RCPPUTILS_DIR}/include"
  "${RCUTILS_DIR}/include"
  "${GENERATED_INCLUDE_DIR}")
target_compile_options(ros2_ohos_smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ros2_ohos_smoke PRIVATE ros2_rcpputils ros2_rcutils dl)

install(TARGETS
  ros2_rcutils
  ros2_rcpputils
  ros2_ohos_dummy
  ros2_ohos_libintl_shim
  ros2_ohos_smoke
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)

install(DIRECTORY "${RCUTILS_DIR}/include/" DESTINATION include/ros2_rcutils)
install(DIRECTORY "${RCPPUTILS_DIR}/include/" DESTINATION include/ros2_rcpputils)
install(FILES "${GENERATED_LOGGING_HEADER}" DESTINATION include/ros2_rcutils/rcutils)

if(ROS2_OHOS_ENABLE_RMW_SMOKE)
  if(NOT ROS2_OHOS_RMW_PREFIX)
    set(ROS2_OHOS_RMW_PREFIX "${CMAKE_INSTALL_PREFIX}")
  endif()

  set(ROS2_OHOS_RMW_PREFIXES
    "${ROS2_OHOS_RMW_PREFIX}"
    "${ROS2_OHOS_FASTDDS_PREFIX}"
    "${ROS2_OHOS_RMW_PREFIX}/opt/libyaml_vendor"
    "${ROS2_OHOS_RMW_PREFIX}/opt/spdlog_vendor")
  foreach(prefix_candidate IN LISTS ROS2_OHOS_RMW_PREFIXES)
    if(EXISTS "${prefix_candidate}")
      list(PREPEND CMAKE_PREFIX_PATH "${prefix_candidate}")
    endif()
  endforeach()
  if(NOT DEFINED FastRTPS_INCLUDE_DIR AND EXISTS "${ROS2_OHOS_FASTDDS_PREFIX}/include")
    set(FastRTPS_INCLUDE_DIR "${ROS2_OHOS_FASTDDS_PREFIX}/include")
  endif()
  if(NOT DEFINED FastRTPS_LIBRARY_RELEASE AND EXISTS "${ROS2_OHOS_FASTDDS_PREFIX}/lib/libfastrtps.so")
    set(FastRTPS_LIBRARY_RELEASE "${ROS2_OHOS_FASTDDS_PREFIX}/lib/libfastrtps.so")
  endif()
  if(NOT DEFINED FastCDR_LIBRARY_RELEASE AND EXISTS "${ROS2_OHOS_FASTDDS_PREFIX}/lib/libfastcdr.so")
    set(FastCDR_LIBRARY_RELEASE "${ROS2_OHOS_FASTDDS_PREFIX}/lib/libfastcdr.so")
  endif()
  if((NOT DEFINED FastRTPS_LIBRARIES OR "${FastRTPS_LIBRARIES}" STREQUAL "")
      AND DEFINED FastRTPS_LIBRARY_RELEASE AND DEFINED FastCDR_LIBRARY_RELEASE)
    set(FastRTPS_LIBRARIES "${FastRTPS_LIBRARY_RELEASE};${FastCDR_LIBRARY_RELEASE}")
  endif()

  set(ROS2_OHOS_PACKAGE_DIRS)
  foreach(prefix_root
      "${ROS2_OHOS_RMW_PREFIX}"
      "${ROS2_OHOS_FASTDDS_PREFIX}")
    file(GLOB prefix_share_cmake_dirs LIST_DIRECTORIES true "${prefix_root}/share/*/cmake")
    list(APPEND ROS2_OHOS_PACKAGE_DIRS ${prefix_share_cmake_dirs})
    file(GLOB prefix_lib_cmake_dirs LIST_DIRECTORIES true
      "${prefix_root}/lib/cmake/*"
      "${prefix_root}/lib/*/cmake")
    list(APPEND ROS2_OHOS_PACKAGE_DIRS ${prefix_lib_cmake_dirs})
  endforeach()
  file(GLOB ROS2_OHOS_VENDOR_CMAKE_DIRS LIST_DIRECTORIES true
    "${ROS2_OHOS_RMW_PREFIX}/opt/*/cmake"
    "${ROS2_OHOS_RMW_PREFIX}/opt/*/lib/cmake/*")
  list(APPEND ROS2_OHOS_PACKAGE_DIRS ${ROS2_OHOS_VENDOR_CMAKE_DIRS})

  foreach(package_dir IN LISTS ROS2_OHOS_PACKAGE_DIRS)
    file(GLOB package_configs
      "${package_dir}/*Config.cmake"
      "${package_dir}/*-config.cmake")
    foreach(package_config IN LISTS package_configs)
      get_filename_component(package_config_name "${package_config}" NAME)
      string(REGEX REPLACE "(-config|Config)\\.cmake$" "" package_name "${package_config_name}")
      if(NOT DEFINED ${package_name}_DIR OR "${${package_name}_DIR}" MATCHES "-NOTFOUND$")
        set(${package_name}_DIR "${package_dir}")
      endif()
    endforeach()
  endforeach()

  find_package(rcl REQUIRED)
  find_package(rosidl_runtime_c REQUIRED)
  find_package(rosidl_typesupport_c REQUIRED)
  find_package(std_msgs REQUIRED)

  add_executable(ros2_ohos_pubsub_smoke "${CMAKE_CURRENT_LIST_DIR}/src/pubsub_smoke.cpp")
  target_compile_options(ros2_ohos_pubsub_smoke PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(ros2_ohos_pubsub_smoke PRIVATE
    rcl::rcl
    rosidl_runtime_c::rosidl_runtime_c
    rosidl_typesupport_c::rosidl_typesupport_c
    std_msgs::std_msgs__rosidl_typesupport_c
    dl)

  install(TARGETS ros2_ohos_pubsub_smoke RUNTIME DESTINATION bin)
endif()
