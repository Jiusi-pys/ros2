# OHOS cross-build shim for CMake's FindPkgConfig.
#
# The host machine's application-control policy (WDAC) blocks every
# pkg-config / pkgconf binary (os error 4551), so the real FindPkgConfig
# cannot run. This module shadows it when build_ohos.sh passes
# -DCMAKE_MODULE_PATH=<workspace>/cmake and answers the only pkg-config
# queries the ROS 2 tracing stack makes (lttng-ust, lttng-ctl, liburcu*)
# straight from the target sysroot at install_ohos/. Any other module
# reports "not found" - identical to a machine without pkg-config.

set(PKG_CONFIG_FOUND TRUE)
set(PKG_CONFIG_VERSION_STRING "ohos-shim-1.0")

get_filename_component(_OHOS_WS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_OHOS_DEPS_PREFIX "${_OHOS_WS_ROOT}/install_ohos")

# module name -> [version, library file name]
set(_OHOS_PKG_lttng-ust_VERSION "2.13.8")
set(_OHOS_PKG_lttng-ust_LIB "liblttng-ust.so")
set(_OHOS_PKG_lttng-ctl_VERSION "2.13.15")
set(_OHOS_PKG_lttng-ctl_LIB "liblttng-ctl.so")
set(_OHOS_PKG_liburcu_VERSION "0.14.1")
set(_OHOS_PKG_liburcu_LIB "liburcu.so")
set(_OHOS_PKG_liburcu-common_VERSION "0.14.1")
set(_OHOS_PKG_liburcu-common_LIB "liburcu-common.so")
set(_OHOS_PKG_liburcu-bp_VERSION "0.14.1")
set(_OHOS_PKG_liburcu-bp_LIB "liburcu-bp.so")
set(_OHOS_PKG_liburcu-cds_VERSION "0.14.1")
set(_OHOS_PKG_liburcu-cds_LIB "liburcu-cds.so")
set(_OHOS_PKG_liburcu-mb_VERSION "0.14.1")
set(_OHOS_PKG_liburcu-mb_LIB "liburcu-mb.so")
set(_OHOS_PKG_liburcu-memb_VERSION "0.14.1")
set(_OHOS_PKG_liburcu-memb_LIB "liburcu-memb.so")
set(_OHOS_PKG_liburcu-signal_VERSION "0.14.1")
set(_OHOS_PKG_liburcu-signal_LIB "liburcu-signal.so")

function(pkg_check_modules _prefix)
  set(_required FALSE)
  set(_modules "")
  foreach(_arg IN LISTS ARGN)
    if(_arg MATCHES "^(REQUIRED|QUIET|GLOBAL|NO_CMAKE_PATH|NO_CMAKE_ENVIRONMENT_PATH|IMPORTED_TARGET|IMPOROVED_UTF8|NO_MODULE)$")
      if(_arg STREQUAL "REQUIRED")
        set(_required TRUE)
      endif()
    elseif(_arg MATCHES "^([A-Za-z0-9_.+-]+)[ \t]*(=|[><]=?)?[ \t]*(.*)$")
      list(APPEND _modules "${CMAKE_MATCH_1}")
    endif()
  endforeach()

  set(_found TRUE)
  set(_libs "")
  set(_version "")
  foreach(_mod IN LISTS _modules)
    if(DEFINED _OHOS_PKG_${_mod}_LIB AND
        EXISTS "${_OHOS_DEPS_PREFIX}/lib/${_OHOS_PKG_${_mod}_LIB}")
      list(APPEND _libs "${_OHOS_DEPS_PREFIX}/lib/${_OHOS_PKG_${_mod}_LIB}")
      set(_version "${_OHOS_PKG_${_mod}_VERSION}")
    else()
      set(_found FALSE)
      if(_required)
        message(FATAL_ERROR
          "pkg_check_modules(${_prefix}): module '${_mod}' not available in "
          "${_OHOS_DEPS_PREFIX} (ohos FindPkgConfig shim; run "
          "scripts/build_target_deps.sh first)")
      endif()
    endif()
  endforeach()

  set(${_prefix}_FOUND ${_found} PARENT_SCOPE)
  if(_found)
    set(${_prefix}_VERSION "${_version}" PARENT_SCOPE)
    set(${_prefix}_LIBRARIES "${_libs}" PARENT_SCOPE)
    set(${_prefix}_LINK_LIBRARIES "${_libs}" PARENT_SCOPE)
    set(${_prefix}_LIBRARY_DIRS "${_OHOS_DEPS_PREFIX}/lib" PARENT_SCOPE)
    set(${_prefix}_INCLUDE_DIRS "${_OHOS_DEPS_PREFIX}/include" PARENT_SCOPE)
    set(${_prefix}_CFLAGS_OTHER "" PARENT_SCOPE)
    set(${_prefix}_LDFLAGS_OTHER "" PARENT_SCOPE)
  endif()
endfunction()

macro(pkg_search_module _prefix)
  # Not needed by the ROS 2 tracing stack; behave like "not found".
  set(${_prefix}_FOUND FALSE)
endmacro()
