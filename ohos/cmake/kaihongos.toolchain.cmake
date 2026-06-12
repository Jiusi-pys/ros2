cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT)
  if(DEFINED ENV{ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT})
    set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT "$ENV{ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}")
  else()
    set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT "/home/kaihong/M-DDS_4.1/command-line-tools")
    if(NOT EXISTS "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native" AND
        EXISTS "/home/kaihong/M-DDS/command-line-tools/sdk/default/openharmony/native")
      set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT "/home/kaihong/M-DDS/command-line-tools")
    endif()
  endif()
endif()
set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT
  "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}" CACHE PATH "Path to command-line-tools")

if(NOT EXISTS "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native" AND
    EXISTS "/home/kaihong/M-DDS/command-line-tools/sdk/default/openharmony/native")
  set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT "/home/kaihong/M-DDS/command-line-tools" CACHE PATH "Path to command-line-tools" FORCE)
endif()

if(NOT EXISTS "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native" AND
    DEFINED CMAKE_C_COMPILER AND EXISTS "${CMAKE_C_COMPILER}")
  get_filename_component(_ros2_ohos_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
  get_filename_component(_ros2_ohos_llvm_dir "${_ros2_ohos_compiler_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_native_dir "${_ros2_ohos_llvm_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_openharmony_dir "${_ros2_ohos_native_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_default_dir "${_ros2_ohos_openharmony_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_sdk_dir "${_ros2_ohos_default_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_root_from_compiler "${_ros2_ohos_sdk_dir}" DIRECTORY)
  if(EXISTS "${_ros2_ohos_root_from_compiler}/sdk/default/openharmony/native")
    set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT "${_ros2_ohos_root_from_compiler}" CACHE PATH "Path to command-line-tools" FORCE)
  endif()
endif()

if(NOT EXISTS "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native" AND
    DEFINED CMAKE_MAKE_PROGRAM AND EXISTS "${CMAKE_MAKE_PROGRAM}")
  get_filename_component(_ros2_ohos_make_dir "${CMAKE_MAKE_PROGRAM}" DIRECTORY)
  get_filename_component(_ros2_ohos_cmake_dir "${_ros2_ohos_make_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_build_tools_dir "${_ros2_ohos_cmake_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_native_root "${_ros2_ohos_build_tools_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_openharmony_dir "${_ros2_ohos_native_root}" DIRECTORY)
  get_filename_component(_ros2_ohos_default_dir "${_ros2_ohos_openharmony_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_sdk_dir "${_ros2_ohos_default_dir}" DIRECTORY)
  get_filename_component(_ros2_ohos_root_from_make "${_ros2_ohos_sdk_dir}" DIRECTORY)
  if(EXISTS "${_ros2_ohos_root_from_make}/sdk/default/openharmony/native")
    set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT "${_ros2_ohos_root_from_make}" CACHE PATH "Path to command-line-tools" FORCE)
  endif()
endif()

if(NOT DEFINED OHOS_ARCH)
  set(OHOS_ARCH arm64-v8a CACHE STRING "OHOS target ABI")
endif()

set(OHOS_SDK_NATIVE
  "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native"
  CACHE PATH "OpenHarmony native SDK root" FORCE)
set(HMOS_SDK_NATIVE
  "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}/sdk/default/hms/native"
  CACHE PATH "KaihongOS HMS native SDK root" FORCE)

if(NOT EXISTS "${OHOS_SDK_NATIVE}/build/cmake/ohos.toolchain.cmake")
  message(FATAL_ERROR "OpenHarmony toolchain not found at ${OHOS_SDK_NATIVE}")
endif()
if(NOT EXISTS "${HMOS_SDK_NATIVE}/build/cmake/hmos.toolchain.cmake")
  message(FATAL_ERROR "KaihongOS HMS toolchain not found at ${HMOS_SDK_NATIVE}")
endif()

include("${HMOS_SDK_NATIVE}/build/cmake/hmos.toolchain.cmake")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-command-line-argument" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument" CACHE STRING "" FORCE)
