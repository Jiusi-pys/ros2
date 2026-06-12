cmake_minimum_required(VERSION 3.16)

project(ros2_ohos_python312_dynload C)

set(CPYTHON_SOURCE_DIR "" CACHE PATH "CPython 3.12 source directory")
set(TARGET_PYTHON_RUNTIME_ROOT "" CACHE PATH "Target Python runtime root")
set(TARGET_PYTHON_VERSION "3.12" CACHE STRING "Target Python version")
set(TARGET_PYTHON_EXTENSION_SUFFIX ".cpython-312-aarch64-linux-ohos.so" CACHE STRING "Target Python extension suffix")
set(TARGET_PYTHON_DYNLOAD_DIR
  "${TARGET_PYTHON_RUNTIME_ROOT}/lib/python${TARGET_PYTHON_VERSION}/lib-dynload"
  CACHE PATH "Target Python lib-dynload install directory")

if(NOT CPYTHON_SOURCE_DIR OR NOT EXISTS "${CPYTHON_SOURCE_DIR}/Modules/arraymodule.c")
  message(FATAL_ERROR "CPython source directory is invalid: ${CPYTHON_SOURCE_DIR}")
endif()
if(NOT TARGET_PYTHON_RUNTIME_ROOT)
  message(FATAL_ERROR "TARGET_PYTHON_RUNTIME_ROOT is required")
endif()

set(TARGET_PYTHON_INCLUDE_DIR "${TARGET_PYTHON_RUNTIME_ROOT}/include/python${TARGET_PYTHON_VERSION}")
set(TARGET_PYTHON_LIBRARY "${TARGET_PYTHON_RUNTIME_ROOT}/lib/libpython${TARGET_PYTHON_VERSION}.so")
if(NOT EXISTS "${TARGET_PYTHON_INCLUDE_DIR}/Python.h")
  message(FATAL_ERROR "Target Python headers not found at ${TARGET_PYTHON_INCLUDE_DIR}")
endif()
if(NOT EXISTS "${TARGET_PYTHON_LIBRARY}")
  message(FATAL_ERROR "Target Python library not found at ${TARGET_PYTHON_LIBRARY}")
endif()

add_library(Python312::Python UNKNOWN IMPORTED)
set_target_properties(Python312::Python PROPERTIES
  IMPORTED_LOCATION "${TARGET_PYTHON_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${TARGET_PYTHON_INCLUDE_DIR}")

function(add_python312_extension module_name)
  cmake_parse_arguments(PYEXT "" "" "SOURCES;LIBRARIES;DEFINITIONS" ${ARGN})
  if(NOT PYEXT_SOURCES)
    message(FATAL_ERROR "No sources for Python extension ${module_name}")
  endif()

  string(REPLACE "." "_" target_name "python312_dynload_${module_name}")
  add_library("${target_name}" MODULE ${PYEXT_SOURCES})
  target_include_directories("${target_name}" PRIVATE
    "${TARGET_PYTHON_INCLUDE_DIR}"
    "${CPYTHON_SOURCE_DIR}"
    "${CPYTHON_SOURCE_DIR}/Include"
    "${CPYTHON_SOURCE_DIR}/Include/internal"
    "${CPYTHON_SOURCE_DIR}/Modules"
    "${CPYTHON_SOURCE_DIR}/Modules/_hacl/include"
    "${CPYTHON_SOURCE_DIR}/Modules/expat")
  target_compile_definitions("${target_name}" PRIVATE
    Py_BUILD_CORE_MODULE
    ${PYEXT_DEFINITIONS})
  target_compile_options("${target_name}" PRIVATE
    -Wno-unused-command-line-argument
    -Wno-deprecated-declarations
    -Wno-missing-field-initializers
    -Wno-unused-function
    -Wno-unused-variable)
  target_link_libraries("${target_name}" PRIVATE Python312::Python ${PYEXT_LIBRARIES})
  set_target_properties("${target_name}" PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${module_name}"
    SUFFIX "${TARGET_PYTHON_EXTENSION_SUFFIX}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib-dynload")
  install(TARGETS "${target_name}" LIBRARY DESTINATION "${TARGET_PYTHON_DYNLOAD_DIR}")
endfunction()

add_python312_extension(array
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/arraymodule.c")
add_python312_extension(math
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/mathmodule.c"
  LIBRARIES m)
add_python312_extension(cmath
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/cmathmodule.c"
  LIBRARIES m)
add_python312_extension(_statistics
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_statisticsmodule.c"
  LIBRARIES m)
add_python312_extension(_struct
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_struct.c")
add_python312_extension(binascii
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/binascii.c"
  LIBRARIES z)
add_python312_extension(zlib
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/zlibmodule.c"
  LIBRARIES z)
add_python312_extension(_datetime
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_datetimemodule.c"
  LIBRARIES m)
add_python312_extension(select
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/selectmodule.c")
add_python312_extension(_socket
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/socketmodule.c")
add_python312_extension(_random
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_randommodule.c")
add_python312_extension(_md5
  SOURCES
    "${CPYTHON_SOURCE_DIR}/Modules/md5module.c"
    "${CPYTHON_SOURCE_DIR}/Modules/_hacl/Hacl_Hash_MD5.c")
add_python312_extension(_sha1
  SOURCES
    "${CPYTHON_SOURCE_DIR}/Modules/sha1module.c"
    "${CPYTHON_SOURCE_DIR}/Modules/_hacl/Hacl_Hash_SHA1.c")
add_python312_extension(_sha2
  SOURCES
    "${CPYTHON_SOURCE_DIR}/Modules/sha2module.c"
    "${CPYTHON_SOURCE_DIR}/Modules/_hacl/Hacl_Hash_SHA2.c")
add_python312_extension(_sha3
  SOURCES
    "${CPYTHON_SOURCE_DIR}/Modules/sha3module.c"
    "${CPYTHON_SOURCE_DIR}/Modules/_hacl/Hacl_Hash_SHA3.c")
add_python312_extension(_blake2
  SOURCES
    "${CPYTHON_SOURCE_DIR}/Modules/_blake2/blake2module.c"
    "${CPYTHON_SOURCE_DIR}/Modules/_blake2/blake2b_impl.c"
    "${CPYTHON_SOURCE_DIR}/Modules/_blake2/blake2s_impl.c")
add_python312_extension(_bisect
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_bisectmodule.c")
add_python312_extension(_heapq
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_heapqmodule.c")
add_python312_extension(_opcode
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_opcode.c")
add_python312_extension(_csv
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_csv.c")
add_python312_extension(_json
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_json.c")
add_python312_extension(_pickle
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_pickle.c")
add_python312_extension(_queue
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_queuemodule.c")
add_python312_extension(_asyncio
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_asynciomodule.c")
add_python312_extension(_contextvars
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_contextvarsmodule.c")
add_python312_extension(fcntl
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/fcntlmodule.c")
add_python312_extension(resource
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/resource.c")
add_python312_extension(termios
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/termios.c")
add_python312_extension(pwd
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/pwdmodule.c")
add_python312_extension(grp
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/grpmodule.c")
add_python312_extension(mmap
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/mmapmodule.c")
add_python312_extension(_posixsubprocess
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/_posixsubprocess.c")
add_python312_extension(unicodedata
  SOURCES "${CPYTHON_SOURCE_DIR}/Modules/unicodedata.c")
add_python312_extension(pyexpat
  SOURCES
    "${CPYTHON_SOURCE_DIR}/Modules/pyexpat.c"
    "${CPYTHON_SOURCE_DIR}/Modules/expat/xmlparse.c"
    "${CPYTHON_SOURCE_DIR}/Modules/expat/xmlrole.c"
    "${CPYTHON_SOURCE_DIR}/Modules/expat/xmltok.c"
  DEFINITIONS
    HAVE_EXPAT_CONFIG_H
    XML_STATIC)
