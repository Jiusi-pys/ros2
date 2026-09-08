"""OHOS ROS-process policy for C++ plugin RTTI across Python-loaded DSOs.

Loaded only from the explicitly configured, hash-bound ROS bootstrap directory.
The shared CPython installation and unrelated Python processes are unchanged.
"""
import os
from pathlib import Path
import sys
import sysconfig


def configure_ros_python():
    directory = os.environ.get('ROS2_PYTHON_BOOTSTRAP_DIR')
    if not directory or not os.environ.get('ROS2_HOME'):
        return
    if Path(directory).resolve() != Path(__file__).resolve().parent:
        return
    abi = sysconfig.get_config_var('SOABI') or ''
    if not abi.endswith('-aarch64-linux-ohos'):
        return
    # OHOS libc++abi compares typeinfo addresses across DSOs. Match native
    # executable loading so class_loader sees one canonical factory typeinfo.
    sys.setdlopenflags(sys.getdlopenflags() | os.RTLD_GLOBAL)


configure_ros_python()
