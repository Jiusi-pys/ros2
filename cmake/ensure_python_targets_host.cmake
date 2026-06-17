# Injected via -DCMAKE_PROJECT_INCLUDE_BEFORE for the Focal source build.
#
# rosidl_generator_py export files hard-reference the imported targets
# `Python3::Python` and `Python3::NumPy` in their INTERFACE_LINK_LIBRARIES.
# When a downstream package (e.g. rcl) consumes such an export transitively
# without itself having requested the matching find_package(Python3)
# components, those targets are undefined and CMake 4.x turns this into a hard
# error. Resolving the components up-front (at top-level project scope, before
# any other find_package runs) defines the targets for the whole tree.
# Mirrors ohos/cmake/ensure_python_targets.cmake for the host build.
find_package(Python3 QUIET COMPONENTS Interpreter Development NumPy)

# Fallback: hand-create the NumPy interface target if FindPython3 did not
# (e.g. numpy 2.x header layout not picked up by an older find module).
if(NOT TARGET Python3::NumPy AND DEFINED HOST_NUMPY_INCLUDE_DIR AND
    IS_DIRECTORY "${HOST_NUMPY_INCLUDE_DIR}")
  add_library(Python3::NumPy INTERFACE IMPORTED GLOBAL)
  set_target_properties(Python3::NumPy PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${HOST_NUMPY_INCLUDE_DIR}")
endif()
