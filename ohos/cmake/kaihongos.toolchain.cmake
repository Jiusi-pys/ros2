cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT)
  if(DEFINED ENV{ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT})
    set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT "$ENV{ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}")
  else()
    set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT "/home/kaihong/M-DDS_4.1/command-line-tools")
  endif()
endif()
set(ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT
  "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}" CACHE PATH "Path to command-line-tools")

if(NOT DEFINED OHOS_ARCH)
  set(OHOS_ARCH arm64-v8a CACHE STRING "OHOS target ABI")
endif()

set(OHOS_SDK_NATIVE
  "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native"
  CACHE PATH "OpenHarmony native SDK root")
set(HMOS_SDK_NATIVE
  "${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT}/sdk/default/hms/native"
  CACHE PATH "KaihongOS HMS native SDK root")

if(NOT EXISTS "${OHOS_SDK_NATIVE}/build/cmake/ohos.toolchain.cmake")
  message(FATAL_ERROR "OpenHarmony toolchain not found at ${OHOS_SDK_NATIVE}")
endif()
if(NOT EXISTS "${HMOS_SDK_NATIVE}/build/cmake/hmos.toolchain.cmake")
  message(FATAL_ERROR "KaihongOS HMS toolchain not found at ${HMOS_SDK_NATIVE}")
endif()

include("${HMOS_SDK_NATIVE}/build/cmake/hmos.toolchain.cmake")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-command-line-argument" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument" CACHE STRING "" FORCE)
