# CMake toolchain file: cross-compile ROS 2 core for OpenHarmony (aarch64-linux-ohos)
# using the OHOS SDK NDK clang shipped in the command-line tools package.
#
# Usage:
#   colcon build ... --cmake-args \
#     -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/ohos-aarch64.toolchain.cmake
#
# Override the SDK location with -DOHOS_NATIVE_SDK=<path-to-native> if needed.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED OHOS_NATIVE_SDK)
  if(DEFINED ENV{OHOS_NATIVE_SDK})
    set(OHOS_NATIVE_SDK "$ENV{OHOS_NATIVE_SDK}")
  else()
    set(OHOS_NATIVE_SDK
      "C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/native")
  endif()
endif()
file(TO_CMAKE_PATH "${OHOS_NATIVE_SDK}" OHOS_NATIVE_SDK)

set(OHOS_TARGET_TRIPLE aarch64-linux-ohos)
set(OHOS_LLVM_BIN "${OHOS_NATIVE_SDK}/llvm/bin")

# Call clang.exe directly; the NDK wrapper scripts are POSIX shell and cannot be
# executed by CMake on Windows. Reproduce their flags here (-target/--sysroot/-D__MUSL__).
set(CMAKE_C_COMPILER "${OHOS_LLVM_BIN}/clang.exe")
set(CMAKE_CXX_COMPILER "${OHOS_LLVM_BIN}/clang++.exe")
set(CMAKE_C_COMPILER_TARGET ${OHOS_TARGET_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${OHOS_TARGET_TRIPLE})
# ASM sources (.S) are compiled with the same clang; without the target triple
# they assemble for the Windows host (x86_64-w64) and fail (mimick_vendor).
set(CMAKE_ASM_COMPILER "${OHOS_LLVM_BIN}/clang.exe")
set(CMAKE_ASM_COMPILER_TARGET ${OHOS_TARGET_TRIPLE})
set(CMAKE_SYSROOT "${OHOS_NATIVE_SDK}/sysroot")

set(CMAKE_AR "${OHOS_LLVM_BIN}/llvm-ar.exe" CACHE FILEPATH "archiver")
set(CMAKE_RANLIB "${OHOS_LLVM_BIN}/llvm-ranlib.exe" CACHE FILEPATH "ranlib")
set(CMAKE_NM "${OHOS_LLVM_BIN}/llvm-nm.exe" CACHE FILEPATH "nm")
set(CMAKE_OBJDUMP "${OHOS_LLVM_BIN}/llvm-objdump.exe" CACHE FILEPATH "objdump")
set(CMAKE_STRIP "${OHOS_LLVM_BIN}/llvm-strip.exe" CACHE FILEPATH "strip")

# The NDK wrapper scripts define __MUSL__ (OHOS libc is musl-based).
set(CMAKE_C_FLAGS_INIT "-D__MUSL__")
set(CMAKE_CXX_FLAGS_INIT "-D__MUSL__")

# Executables must export their weak symbols (template typeinfo etc.):
# class_loader/pluginlib rely on cross-DSO dynamic_cast, which only works if
# the weak typeinfo in the executable interposes the copies in dlopened
# plugins. Without --export-dynamic the exe keeps a private copy and
# createInstance() fails ("Could not create instance of type ...").
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--export-dynamic")

# Shared libraries and executables must resolve their deps from $ORIGIN/../lib
# style rpaths; colcon/ament already set install rpaths, keep them enabled.
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# Marker so packages can apply OHOS-specific workarounds (e.g. musl libc
# differences) without guessing from CMAKE_SYSTEM_NAME.
set(OHOS_CROSS_BUILD TRUE)

# Search roots: the OHOS sysroot plus this workspace's target install prefix
# (paths already inside a root are NOT re-rooted by CMake, so the colcon
# install prefix keeps working). With ONLY mode, host prefixes leaked from
# the pixi/conda environment (e.g. .pixi/envs/default/Library) are re-rooted
# and thus invisible - vendor packages then build their deps for the target
# instead of linking host Windows binaries.
get_filename_component(OHOS_WORKSPACE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CMAKE_FIND_ROOT_PATH
  "${CMAKE_SYSROOT}/usr"
  "${OHOS_WORKSPACE_ROOT}/install_ohos")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Never re-root host programs (Python, generators); libraries/includes/packages
# are restricted to the roots above.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
