#!/bin/sh
# Global CLI entry for the provenance-checked generic ROS 2 deployment.
run_generic_ros2() {
  deployment_prefix=$1
  shift
  requested_rmw="${RMW_IMPLEMENTATION:-rmw_fastrtps_cpp}"
  case "$requested_rmw" in
    rmw_fastrtps_cpp|rmw_cyclonedds_cpp) ;;
    *) echo 'ERROR: this deployment supports Fast DDS/Cyclone DDS, not MDDS' >&2; return 70 ;;
  esac
  if [ ! -f "$deployment_prefix/env.sh" ] || [ -L "$deployment_prefix/env.sh" ]; then
    echo 'ERROR: generic ROS deployment environment is missing' >&2
    return 70
  fi
  unset PYTHONHOME PYTHONPATH PYTHONUSERBASE LD_PRELOAD LD_LIBRARY_PATH
  unset AMENT_PREFIX_PATH CMAKE_PREFIX_PATH COLCON_PREFIX_PATH ROS2_HOME
  . "$deployment_prefix/env.sh" || return 70
  export ROS_DISTRO=jazzy ROS_VERSION=2 ROS_PYTHON_VERSION=3
  export RMW_IMPLEMENTATION="$requested_rmw"
  if [ "${1:-}" = doctor ]; then
    doctor_site=/data/local/tmp/ros2-core-config/doctor-python-0ff9eadde0e55db78bcbc08da1bf132b5dffc6393952344b73513d714fff3a07
    if [ ! -d "$doctor_site" ] || [ -L "$doctor_site" ] || \
       [ "$(sha256sum "$doctor_site/doctor_manifest.sha256" 2>/dev/null | cut -d ' ' -f1)" != 0ff9eadde0e55db78bcbc08da1bf132b5dffc6393952344b73513d714fff3a07 ] || \
       ! (cd "$doctor_site" && sha256sum -c doctor_manifest.sha256 >/dev/null 2>&1); then
      echo 'ERROR: hash-bound doctor dependencies are missing or damaged' >&2
      return 70
    fi
    # Append only for doctor: never replace the core runtime's existing deps.
    export PYTHONPATH="$PYTHONPATH:$doctor_site"
  fi
  if [ -z "${ROSDISTRO_INDEX_URL:-}" ] && [ -f /data/local/tmp/ros2-core-config/rosdistro/index-v4.yaml ]; then
    export ROSDISTRO_INDEX_URL=file:///data/local/tmp/ros2-core-config/rosdistro/index-v4.yaml
  fi
  ros2 "$@"
}
run_generic_ros2 /data/local/tmp/ros2-generic "$@"
