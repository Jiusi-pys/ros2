# KaihongOS Migration Summary

## Overview

This workspace was migrated to a standalone OpenHarmony/KaihongOS CMake flow using `command-line-tools` and `OpenHarmony/prebuilts`. The result is a local ROS 2 porting environment that can build, deploy, and validate native binaries on RK3588S boards over HDC without depending on an in-tree OpenHarmony GN integration.

## Current Colcon / Deployment Status

- `ohos/colcon_rk3588s.sh` is a selected-package RK3588S/OHOS colcon
  migration wrapper, not a proven full-workspace replacement yet.
- Current colcon-wrapper validation covers:
  - `rcutils`
  - `ament_index_python`
  - `ament_index_cpp`
  - `ros2cli`
  - `ros2pkg`
  - `rcl`
  - `rcl_action`
  - `rcl_lifecycle`
  - `rclcpp`
  - `rclcpp_action`
  - `rclcpp_components`
  - `rclcpp_lifecycle`
  - `rpyutils`
  - `rclpy`
  - `rosidl_generator_py`
  - `action_tutorials_interfaces`
  - `ros2action`
  - `ros2doctor`
  - `ros2interface`
  - `ros2node`
  - `ros2param`
  - `ros2run`
  - `ros2service`
  - `ros2topic`
  - `ros2lifecycle`
  - `ros2component`
  - `ros2bag`
  - `ros2cli_common_extensions`
  - `std_msgs`
  - `example_interfaces`
  - `geometry_msgs`
  - `sensor_msgs`
  - `nav_msgs`
  - `tf2`
  - `tf2_py`
  - `tf2_ros`
  - `osrf_pycommon`
  - `launch`
  - `launch_xml`
  - `launch_yaml`
  - `launch_ros`
  - `ros2launch`
  - `class_loader`
  - `pluginlib`
  - `composition`
- The `rclpy` lane was fixed by forwarding these OHOS target variables through
  the wrapper:
  - `RCLPY_OHOS_TARGET_PYTHON_INCLUDE_DIR`
  - `RCLPY_OHOS_TARGET_PYTHON_LIBRARY`
  - `RCLPY_OHOS_TARGET_PYTHON_EXTENSION_SUFFIX`
  - `RCLPY_OHOS_TARGET_PYBIND11_INCLUDE_DIR`
- Generator packages now work because the wrapper also places the underlay
  target `lib/python3.12/site-packages` tree on the host build `PYTHONPATH`.
  Generated CMake extras are rewritten away from host `python3.11`
  `site-packages` paths during wrapper post-processing.
- The default generated overlay `install/ohos-colcon-rk3588s` has been rebuilt
  as this 45-package selected ROS 2 CLI/runtime-capable set, including
  `rpyutils` and `tf2_py`. A
  no-argument
  `ohos/colcon_rk3588s.sh` run now uses this same package set, so a clean
  default rebuild reproduces the validated overlay instead of shrinking it to
  the old two-package sanity lane.
- `ohos/stage_colcon_runtime_closure.sh` stages the additional runtime closure
  needed for a self-contained deployed CLI overlay: generated interface
  packages, underlay shared libraries, vendor shared libraries, host Python
  helper packages, and the OHOS `psutil`/`numpy` stubs. After this step,
  `install/ohos-colcon-rk3588s` has 58 ament-index package resource entries.
- A full workspace colcon build has not been proven; the current workspace
  reports 366 colcon-visible packages with `colcon list`.
- `install/ohos-ros2` is still the validated standalone per-package CMake
  prefix and currently has 194 ament-index package resource entries after
  removing stale generated `SUMMARY.md` resource-index files.
- The rewritten chunked full-prefix deploy helper has a real board-B
  single-command validation. The normal unchunked full-prefix deploy helper is
  hardened with gzip archives, marker-based remote success checks, byte-count
  verification, and package-count validation, but it has not been separately
  re-proven for the whole current 194-entry prefix on this host.
- `polled_camera` remains skipped because its source package has
  `COLCON_IGNORE` and is still ROS 1/catkin.
- HDC can return process status `-1` or timeout/dumped-core messages after
  valid stdout; board-side output and explicit success markers remain the
  reliable validation signal.
- On the currently attached boards, direct HDC commands are more reliable than
  helper paths that capture HDC stdout/stderr. Captured probes can print
  `Connect server failed` while direct shell/file commands still complete.
  `eth0` is up without carrier on both boards; `eth1` is the live cross-board
  link (`192.168.77.10` / `192.168.77.11`).

## Delivered Work

- Added standalone OHOS build entrypoints:
  - `ohos/build_ohos.sh`
  - `ohos/build_ros2_bootstrap.sh`
  - `ohos/build_ros2_package.sh`
  - `ohos/build_fastdds_stack.sh`
- Added device deployment and test helpers:
  - `ohos/deploy_smoke.sh`
  - `ohos/deploy_fastdds_smoke.sh`
  - `ohos/deploy_ros2_prefix.sh`
  - `ohos/deploy_ros2_prefix_chunked.sh`
  - `ohos/test_cross_board_fastdds.sh`
  - `ohos/test_cross_board_pubsub.sh`
- Added board-local validation helpers:
  - `ohos/tools/run_rclcpp_service_roundtrip.sh`
  - `ohos/tools/run_rclcpp_action_roundtrip.sh`
  - `ohos/tools/run_rclcpp_action_binary_roundtrip.sh`
  - `ohos/tools/run_tf2_static_echo_roundtrip.sh`
  - `ohos/tools/run_composition_dlopen_roundtrip.sh`
  - `ohos/tools/run_component_cli_roundtrip.sh`
  - `ohos/tools/run_urdf_robot_state_publisher_probe.sh`
- Added standalone smoke binaries:
  - `ros2_ohos_smoke`
  - `ros2_ohos_pubsub_smoke`
- Added runtime helper:
  - `ohos/print_pubsub_runtime_libs.sh`

## Latest Validation

- Rebuilt the default RK3588S/OHOS colcon overlay with `rpyutils` and `tf2_py`
  included:
  - clean no-argument wrapper build succeeded from a clean install base.
  - persistent `install/ohos-colcon-rk3588s` now has 45 selected package
    resource entries before runtime-closure staging, and 58 package resource
    entries after `ohos/stage_colcon_runtime_closure.sh`.
  - local validation found only `lib/python3.12` directly under the overlay
    `lib/` tree and no generated `.pyc` / `.pyo` files.
  - board A and board B manual deployments verified 58 package resources and
    `lib/python3.12/site-packages/tf2_py/_tf2_py.cpython-312-aarch64-linux-ohos.so`.
- Added an OHOS `libintl.so.8` compatibility shim target
  (`ros2_ohos_libintl_shim`) because the current target Python 3.12 runtime
  links against `libintl.so.8`, but the board image and local runtime tree do
  not provide it.
- Added `PYTHONHOME` export to generated Python wrappers so deployed wrappers
  can resolve a relocated Python runtime under `/data/local/release/usr`.
- Fresh-board Python runtime closure is now staged by:
  - `ohos/stage_python312_stdlib.sh`
  - `ohos/build_python312_dynload.sh`
  - `ohos/stage_colcon_runtime_closure.sh`
  The deployed runtime provides the pure CPython 3.12.7 stdlib,
  `_sysconfigdata__linux_aarch64-linux-ohos.py`, and 34 target
  `lib-dynload` modules including `array`, `math`, `_sha2`, `_socket`, and
  `pyexpat`.
- Confirmed FastDDS UDP transport over the live `eth1` cross-board link:
  - board A (`3e01ff55454d202020104033bf453b00`) subscriber on
    `192.168.77.10`.
  - board B (`3e01ff55454d202020104433991c3b00`) publisher on
    `192.168.77.11`.
  - publisher output included `Message: HelloWorld  with index: 1 SENT`.
  - subscriber log included `Message HelloWorld  1 RECEIVED`.
- Confirmed ROS 2 FastDDS transport over the same live `eth1` link:
  - board A ran `ros2 topic echo --once /codex_cross_probe
    std_msgs/msg/String --no-daemon`.
  - board B ran `ros2 topic pub --once /codex_cross_probe
    std_msgs/msg/String "{data: hello_cross_board_fastdds}"`.
  - board A received `data: hello_cross_board_fastdds`.
- Confirmed board-side ROS 2 CLI/RMW startup from the 58-resource overlay:
  - `ros2 pkg prefix tf2_py` returned `/data/local/tmp/ohos-colcon-rk3588s`.
  - `ros2 interface show builtin_interfaces/msg/Time` printed the expected
    `Time` fields.
  - `RMW_IMPLEMENTATION=rmw_fastrtps_cpp ros2 topic list --no-daemon` listed
    `/parameter_events` and `/rosout` when `HOME` and `ROS_LOG_DIR` pointed at
    writable `/data/local/tmp` paths.
  - local board pub/echo delivered `data: hello_from_ohos`.
- Validated `rclpy` + `ros2cli` on RK3588S with a live `std_msgs/msg/String`
  node launched from `/data/local/tmp/ohos-prefix/bin/rclpy_cli_node.py`.
- Added `std_srvs` and `example_interfaces` Python bindings and mirrored their
  generated Python packages into the target `python3.12` runtime tree.
- Added `geometry_msgs` Python bindings and mirrored them into the target
  `python3.12` runtime tree.
- Added `sensor_msgs`, `tf2_msgs`, `tf2_py`, and `tf2_ros_py` into the target
  `python3.12` runtime tree.
- Added `tf2_geometry_msgs` into the target `python3.12` runtime tree.
- Confirmed `ros2` wrapper commands on-device:
  - `ros2 node list` returned `/rclpy_cli_node`
  - `ros2 node info /rclpy_cli_node` reported the expected `std_msgs/msg/String`
    publisher/subscriber and parameter services
  - `ros2 topic info /rclpy_cli_topic` reported `Type: std_msgs/msg/String`
  - `ros2 param get` / `ros2 param set` updated `demo_text`
  - `ros2 topic echo /rclpy_cli_topic --once` returned `data: updated_from_cli_145`
  - `ros2 topic pub --once /rclpy_cli_in std_msgs/msg/String '{data: from_cli_input_145}'`
    reached the node and logged `rclpy_cli_in_received=from_cli_input_145`
- Confirmed `ros2 service` against a live `rclpy` `std_srvs/Trigger` server:
  - `ros2 service list` included `/rclpy_cli_trigger`
  - `ros2 service type /rclpy_cli_trigger` returned `std_srvs/srv/Trigger`
  - `ros2 service call /rclpy_cli_trigger std_srvs/srv/Trigger '{}'` returned
    `success=True` and `message='trigger_count=1'`
- Confirmed `ros2 action` against a live `rclpy`
  `example_interfaces/action/Fibonacci` server:
  - `ros2 action list` included `/rclpy_cli_fibonacci`
  - `ros2 action info /rclpy_cli_fibonacci` reported `Action servers: 1`
  - board-local `ros2 action send_goal --feedback /rclpy_cli_fibonacci example_interfaces/action/Fibonacci "{order: 5}"` returned feedback and final result
    `sequence: [0, 1, 1, 2, 3, 5]` with status `SUCCEEDED`
- Repeated the same `ros2 service` and `ros2 action` validation on board B
  (`ec290041543253394320030498801a00`) after bootstrapping
  `/data/local/tmp/ohos-prefix` with a chunked gzip prefix transfer and
  background board-side reassembly/extraction.
- Confirmed `ros2 interface show geometry_msgs/msg/Twist` on both boards after
  deploying `geometry_msgs` runtime content into `ohos-prefix`.
- Confirmed `tf2` runtime content on both boards after deploying:
  - `tf2`
  - `tf2_msgs`
  - `message_filters`
  - `tf2_ros`
  - `tf2_py`
  - `tf2_ros_py`
  - `tf2_geometry_msgs`
- Confirmed on both boards:
  - Python imports for `tf2_ros`, `tf2_py`, and `tf2_msgs.msg.TFMessage`
  - `ros2 interface show tf2_msgs/msg/TFMessage`
  - `lib/tf2_ros/tf2_echo` starts and prints its usage banner when invoked
    without frame arguments
- Confirmed `import tf2_geometry_msgs` on both boards after deploying the final
  tf2 geometry runtime delta.
- Confirmed a functional `tf2_ros` round-trip on both boards using:
  - `lib/tf2_ros/static_transform_publisher --x 1 --y 2 --z 3 --yaw 0.5 --frame-id map --child-frame-id laser`
  - `lib/tf2_ros/tf2_echo map laser -r 2`
  - echo output resolved the published transform and printed:
    - `Translation: [1.000, 2.000, 3.000]`
    - quaternion and RPY rotation corresponding to `yaw=0.5`
- Board A needed one additional runtime library for this path:
  - `libstatic_transform_broadcaster_node.so`
  - `libconsole_bridge.so.0.4`
  before `static_transform_publisher` could start successfully.
- Confirmed `class_loader` / `rclcpp_components` runtime on both boards using:
  - `lib/composition/dlopen_composition`
  - `lib/libtalker_component.so`
  - `lib/liblistener_component.so`
  - runtime output reported:
    - `Load library ...`
    - `Instantiate class rclcpp_components::NodeFactoryTemplate<...>`
    - `Publishing: 'Hello World: N'`
    - `I heard: [Hello World: N]`
- Confirmed `ros2 component` CLI on both boards against
  `lib/rclcpp_components/component_container`:
  - `ros2 component types composition`
  - `ros2 component load /ComponentManager composition composition::Talker`
  - `ros2 component load /ComponentManager composition composition::Listener`
  - `ros2 component list /ComponentManager`
  - `ros2 node list` then showed:
    - `/ComponentManager`
    - `/talker`
    - `/listener`
- Board A needed one small runtime completion for this path:
  - `lib/rclcpp_components/component_container`
  - `chmod 755` on the on-device executable
- Confirmed `pluginlib` / `ros2plugin` runtime on both boards using:
  - `lib/pluginlib/list_plugins urdf_parser_plugin urdf::URDFParser`
  - `ros2 plugin list --package urdf`
  - both reported the registered URDF parser plugin:
    - `urdf_xml_parser/URDFXMLParser`
- Rebuilt the remaining direct runtime chain for `robot_state_publisher`:
  - `urdfdom`
  - `urdf`
  - `kdl_parser`
  - `robot_state_publisher`
- Fixed the `urdf` dependency slice so the rebuilt OHOS libraries no longer
  carry host-absolute `DT_NEEDED` entries for TinyXML2. The repaired chain now
  links against `libtinyxml2.so` by SONAME on target:
  - `liburdfdom_model.so.4.0`
  - `liburdfdom_model_state.so.4.0`
  - `liburdfdom_sensor.so.4.0`
  - `liburdfdom_world.so.4.0`
  - `liburdf.so`
  - `liburdf_xml_parser.so`
  - `libkdl_parser.so`
  - `librobot_state_publisher_node.so`
- Added a board-local `robot_state_publisher` probe helper at
  `ohos/tools/run_urdf_robot_state_publisher_probe.sh`. It now uses
  `lib/tf2_ros/tf2_echo base_link link1 -r 1` as the transform proof path,
  which is stable on both boards.
- Added `ohos/tools/rsp_class_loader_probe` to diagnose OHOS component-loader
  failures directly against `class_loader`.
- Confirmed with the class-loader probe that the generic
  `rclcpp_components_register_node(... EXECUTABLE ...)` standalone launcher path
  does not instantiate `NodeFactoryTemplate<...>` reliably on these OHOS
  boards, even though factory registration and ownership are correct.
- Added an OHOS-specific direct `main()` entrypoint in
  `src/ros/robot_state_publisher/src/robot_state_publisher_main.cpp` and kept
  component registration separately in `CMakeLists.txt`. This preserves
  container-based component use while avoiding the broken standalone
  `class_loader` path on OHOS.
- Confirmed `robot_state_publisher` runtime on both boards using the rebuilt
  direct executable:
  - board-local probe showed `/robot_state_publisher` in `ros2 node list`
  - `ros2 topic list` reported `/robot_description`, `/tf`, and `/tf_static`
  - `ros2 topic echo /robot_description --once` returned the expected URDF
  - `lib/tf2_ros/tf2_echo base_link link1 -r 1` resolved the fixed transform
    from the probe URDF on both boards and printed:
    - `Translation: [0.000, 0.000, 1.000]`
    - identity quaternion / zero RPY rotation
- Added an OHOS-specific direct launcher for
  `src/ros2/demos/demo_nodes_cpp_native` and confirmed on board B that:
  - `lib/demo_nodes_cpp_native/talker` starts successfully
  - `ros2 topic echo /chatter --once` returns `data: 'Hello World: 9'`
  - the talker log reports the Fast DDS participant / writer pointers and the
    expected `Publishing: 'Hello World: N'` sequence
- Repeated the same `demo_nodes_cpp_native` runtime validation on board A
  (`ec29004133314d38433031a72cc63c00`) with the same successful `/chatter`
  echo result and talker log output.
- Built `action_tutorials_interfaces` into the OHOS prefix and added OHOS-
  specific direct launchers for `action_tutorials_cpp`.
- Confirmed `action_tutorials_cpp` runtime on board B using:
  - `lib/action_tutorials_cpp/fibonacci_action_server`
  - `lib/action_tutorials_cpp/fibonacci_action_client`
  - client output reported goal acceptance, feedback progression, and final
    result `0 1 1 2 3 5 8 13 21 34 55`
  - server output reported goal receipt, repeated `Publish feedback`, and
    `Goal succeeded`
- Repeated the same `action_tutorials_cpp` runtime validation on board A
  (`ec29004133314d38433031a72cc63c00`) with the same successful client/server
  result sequence and goal completion.
- Confirmed `examples_rclcpp_minimal_subscriber` OHOS direct launcher runtime
  on board B using:
  - `lib/examples_rclcpp_minimal_subscriber/wait_set_subscriber`
  - `ros2 topic pub --once /topic std_msgs/msg/String '{data: from_wait_set_test_226}'`
  - subscriber log recorded `I heard: 'from_wait_set_test_226'`
  - the expected periodic wait-set timeout warnings still appear between
    messages and are consistent with the upstream example behavior
- Repeated the same `examples_rclcpp_minimal_subscriber` runtime validation on
  board A (`ec29004133314d38433031a72cc63c00`) with the same successful
  `wait_set_subscriber` receive log for `from_wait_set_test_232`.
- Confirmed `examples_rclcpp_wait_set` OHOS direct launcher runtime on board B
  using:
  - `lib/examples_rclcpp_wait_set/wait_set_talker`
  - `lib/examples_rclcpp_wait_set/wait_set_listener`
  - listener log recorded both:
    - `I heard: 'Hello, world! N' (wait-set)`
    - `I heard: 'Hello, world! N' (executor)`
  - talker log recorded repeated `Publisher: 'Hello, world! N'`
- Confirmed `quality_of_service_demo_cpp` OHOS direct launcher runtime on
  board B using:
  - `lib/quality_of_service_demo_cpp/message_lost_listener`
  - `lib/quality_of_service_demo_cpp/message_lost_talker`
  - talker log recorded repeated `Publishing an image, sent at [...]`
  - listener log recorded:
    - `I heard an image. Message single trip latency: [...]`
- Repeated the same `quality_of_service_demo_cpp` runtime validation on board A
  (`ec29004133314d38433031a72cc63c00`) with the same successful
  message-lost talker/listener exchange.
- Confirmed `logging_demo` runtime on board B using:
  - `lib/logging_demo/logging_demo_main`
  - board log showed `logger_usage_demo` timer and publish output and later
    switched to DEBUG severity as expected
- Repeated the same `logging_demo` runtime validation on board A
  (`ec29004133314d38433031a72cc63c00`) with the same successful
  `logger_usage_demo` publish output and DEBUG-threshold transition.
- Built `demo_nodes_py` into the OHOS prefix and deployed the runtime delta to
  board A (`ec29004133314d38433031a72cc63c00`).
- Confirmed `demo_nodes_py` runtime on board A using:
  - `bin/talker`
  - board-local `ros2 node list`
  - board-local `ros2 topic list`
  - board-local `ros2 topic info /chatter`
  - board-local `ros2 topic echo /chatter --once`
  - talker log recorded repeated `Publishing: "Hello World: N"`
  - `ros2 node list` reported `/talker`
  - `ros2 topic list` reported `/chatter`
  - `ros2 topic info /chatter` reported `Type: std_msgs/msg/String`
  - `ros2 topic echo /chatter --once` returned a live sample such as:
    - `data: 'Hello World: 60'`
- Built both lifecycle demo packages into the OHOS prefix:
  - `lifecycle`
  - `lifecycle_py`
- Confirmed the C++ `lifecycle` demo runtime on board A using:
  - `lib/lifecycle/lifecycle_talker`
  - `lib/lifecycle/lifecycle_service_client`
  - runtime required the same native library path used by other raw board-side
    C++ probes:
    - `LD_LIBRARY_PATH=/data/local/tmp/ohos-prefix/lib:/data/local/tmp:/data/local/release/usr/lib`
  - `lifecycle_service_client` successfully drove the default transition script:
    - `configure -> inactive`
    - `activate -> active`
    - `deactivate -> inactive`
    - `activate -> active`
    - `deactivate -> inactive`
    - `cleanup -> unconfigured`
    - `shutdown -> finalized`
  - `lifecycle_talker` log confirmed:
    - `on_configure() is called.`
    - inactive publish warnings before activation
    - `on_activate() is called.`
    - active publishes on `/lifecycle_chatter`
  - `on_deactivate() is called.`
  - `on cleanup is called.`
  - `on shutdown is called from state unconfigured.`
- Confirmed the Python `lifecycle_py` demo runtime on board A using:
  - `bin/lifecycle_talker`
  - `lib/lifecycle/lifecycle_service_client`
  - the same `lifecycle_service_client` transition script successfully drove the
    Python lifecycle node through configure / activate / deactivate / cleanup /
    shutdown
  - Python talker log confirmed:
    - `on_configure() is called.`
    - inactive publish messages before activation
    - `on_activate() is called.`
    - active publishes such as `Lifecycle HelloWorld #N`
    - `on_deactivate() is called.`
    - `on_cleanup() is called.`
    - `on_shutdown() is called.`
- Built the pure-Python `examples_rclpy` basic lane into the OHOS prefix:
  - `examples/rclpy/topics/minimal_publisher`
  - `examples/rclpy/topics/minimal_subscriber`
  - `examples/rclpy/services/minimal_service`
  - `examples/rclpy/services/minimal_client`
- Confirmed the `examples_rclpy` minimal pub/sub runtime on board A using:
  - `bin/publisher_member_function`
  - `bin/subscriber_member_function`
  - subscriber log recorded repeated receives such as:
    - `I heard: "Hello World: 379"`
    - `I heard: "Hello World: 380"`
    - `I heard: "Hello World: 381"`
- Confirmed the `examples_rclpy` minimal service/client runtime on board A
  using:
  - `examples_rclpy_minimal_service/service_member_function.py`
  - `examples_rclpy_minimal_client/client.py`
  - client output returned:
    - `Result of add_two_ints: for 41 + 1 = 42`
- Confirmed `topic_statistics_demo` runtime on board A using:
  - `lib/topic_statistics_demo/display_topic_statistics string --publish-period 1000`
  - runtime required the same native library path used by other raw board-side
    C++ probes:
    - `LD_LIBRARY_PATH=/data/local/tmp/ohos-prefix/lib:/data/local/tmp:/data/local/release/usr/lib`
  - startup log confirmed:
    - `Talker starting up`
    - `Listener starting up`
    - `TopicStatisticsListener starting up`
  - statistics listener log reported live periodic metrics including:
    - `Metric name: message_age source: string_listener unit: ms`
    - `Metric name: message_period source: string_listener unit: ms`
    - stable publish-period samples near `1000 ms`
- Built the pure-Python `examples_rclpy` action lane into the OHOS prefix:
  - `examples/rclpy/actions/minimal_action_server`
  - `examples/rclpy/actions/minimal_action_client`
- Confirmed the `examples_rclpy` minimal action runtime on board A using:
  - `bin/server_not_composable`
  - `bin/client_not_composable`
  - client log reported:
    - `Waiting for action server...`
    - `Sending goal request...`
    - `Goal accepted :)`
    - repeated Fibonacci feedback arrays
    - final result:
      - `Goal succeeded! Result: array('i', [0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55])`
  - server log reported:
    - `Executing goal...`
    - repeated `Publishing feedback: array('i', [...])`
    - final result:
      - `Returning result: array('i', [0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55])`
- Confirmed an initial minimal `image_transport` board check on both boards
  using:
  - `lib/image_transport/list_transports`
  - package and plugin metadata resolve correctly on-device
  - at this point output still reported `image_transport/raw` as not available with
    `*** Plugins are not built. ***`, so this is a partial validation of the
    executable / package metadata path, not a full transport runtime proof. The
    later closure below resolves the raw transport runtime gap.
- Confirmed a minimal `point_cloud_transport` board check on board B using:
  - `lib/point_cloud_transport/list_transports`
  - package and plugin metadata resolve correctly on-device
  - output reported the raw transport plugin:
    - `Lookup name: point_cloud_transport/raw_pub`
    - `Transport name: point_cloud_transport/raw`
    - `Declared transports: point_cloud_transport/raw`
- Repeated the same `point_cloud_transport` `list_transports` validation on
  board A (`ec29004133314d38433031a72cc63c00`) with the same successful raw
  transport plugin listing.
- Built the rosbag prerequisite lane into the OHOS prefix:
  - `yaml_cpp_vendor`
  - `sqlite3_vendor`
  - `rosbag2_storage`
  - `rosbag2_cpp`
  - `rosbag2_storage_sqlite3`
- Extended the rosbag lane further into the default backend path:
  - `liblz4_vendor`
  - `zstd_vendor`
  - `mcap_vendor`
  - `rosbag2_storage_mcap`
  - `rosbag2_examples_cpp`
- Extended the higher-level rosbag stack into:
  - `keyboard_handler`
  - `rosbag2_interfaces`
  - `rosbag2_transport`
  - `rosbag2_py`
  - `rosbag2_examples_py`
- Fixed two vendor-path issues that blocked the rosbag lane in the OHOS build:
  - `yaml_cpp_vendor` now exports the actual installed `yaml-cpp` config path
    under `opt/yaml_cpp_vendor/lib/cmake/yaml-cpp`
  - `rosbag2_storage_sqlite3` now has an OHOS-specific fallback that imports
    `SQLite::SQLite3` directly from `opt/sqlite3_vendor` when the normal
    `find_package(SQLite3)` path cannot resolve it
- Fixed the default-backend rosbag lane with vendor-root fallback imports:
  - `mcap_vendor` now exports absolute LZ4 / Zstandard vendor root hints
  - `rosbag2_storage_mcap` seeds `LZ4::lz4` and `zstd::zstd` directly from the
    OHOS vendor installs before consuming `mcap_vendor`
- Probed the default rosbag backend path and confirmed the next blocker is
  no longer vendor discovery; the build side now reaches the MCAP storage
  plugin and the C++ rosbag examples.
- Confirmed the first rosbag runtime proof on board B using:
  - `lib/rosbag2_examples_cpp/simple_bag_recorder`
  - board-local `ros2 topic pub --once /chatter example_interfaces/msg/String '{data: bag_test_124}'`
  - recorder created:
    - `/my_bag/metadata.yaml`
    - `/my_bag/my_bag_0.mcap`
- Repeated the same `simple_bag_recorder` runtime validation on board A
  (`ec29004133314d38433031a72cc63c00`) with the same successful bag creation:
  - `/my_bag/metadata.yaml`
  - `/my_bag/my_bag_0.mcap`
- Confirmed Python rosbag runtime on board B using:
  - `bin/data_generator_executable`
  - `bin/data_generator_node`
  - `bin/rosbag2csv`
  - SQLite-backed write created:
    - `/big_synthetic_bag/big_synthetic_bag_0.db3`
    - `/big_synthetic_bag/metadata.yaml`
  - `rosbag2csv -i /big_synthetic_bag` created `/big_synthetic_bag.csv`
  - MCAP-backed write created:
    - `/timed_synthetic_bag/timed_synthetic_bag_0.mcap`
    - `/timed_synthetic_bag/metadata.yaml`
- Confirmed the same Python rosbag runtime on board A using:
  - `bin/data_generator_executable`
  - `bin/data_generator_node`
  - `bin/rosbag2csv`
  - SQLite-backed write created:
    - `/big_synthetic_bag/big_synthetic_bag_0.db3`
    - `/big_synthetic_bag/metadata.yaml`
  - `rosbag2csv -i /big_synthetic_bag` created `/big_synthetic_bag.csv`
  - MCAP-backed write created:
    - `/timed_synthetic_bag/timed_synthetic_bag_0.mcap`
    - `/timed_synthetic_bag/metadata.yaml`
- Confirmed `rosbag2_transport` minimal runtime on board B using:
  - `lib/rosbag2_transport/recorder`
  - `lib/rosbag2_transport/player`
  - both executables now start on-device with the OHOS direct-launcher path
  - they fail later on missing input URI / noninteractive stdin rather than on
    the old `NodeFactoryTemplate<...>` standalone component-launch failure
- Confirmed `rosbag2_transport` actual recording runtime on board B using:
  - `lib/rosbag2_transport/recorder`
  - `lib/demo_nodes_cpp/talker`
  - recorder subscribed to discovered topics and created:
    - `/transport_bag/metadata.yaml`
    - `/transport_bag/transport_bag_0.mcap`
- Confirmed `rosbag2_transport` playback runtime on board B using:
  - `lib/rosbag2_transport/player`
  - `lib/demo_nodes_cpp/listener`
  - listener received replayed `/chatter` messages from the recorded bag
- Repeated the `rosbag2_transport` recording proof on board A
  (`ec29004133314d38433031a72cc63c00`) with the same successful bag creation:
  - `/transport_bag/metadata.yaml`
  - `/transport_bag/transport_bag_0.mcap`
- Repeated the same `point_cloud_transport` `list_transports` validation on
  board A (`ec29004133314d38433031a72cc63c00`) with the same successful raw
  transport plugin listing.
- Confirmed `demo_nodes_cpp` OHOS direct launcher runtime on board B using:
  - `lib/demo_nodes_cpp/talker`
  - `lib/demo_nodes_cpp/listener`
  - talker log recorded repeated `Publishing: 'Hello World: N'`
  - listener log recorded `I heard: [Hello World: N]`
- Repeated the same `demo_nodes_cpp` runtime validation on board A
  (`ec29004133314d38433031a72cc63c00`) with the same successful talker/listener
  message exchange.
- Confirmed `rclcpp` minimal service round-trip on both boards using the
  installed example binaries:
  - server: `lib/examples_rclcpp_minimal_service/service_main`
  - client: `lib/examples_rclcpp_minimal_client/client_main`
  - client output returned `result of 41 + 1 = 42`
  - server log recorded `request: 41 + 1`
- Confirmed `rclcpp` minimal action round-trip on both boards using:
  - server: `lib/examples_rclcpp_minimal_action_server/action_server_not_composable`
  - client path: board-local `ros2 action send_goal --feedback /fibonacci example_interfaces/action/Fibonacci "{order: 5}"`
  - client output reported accepted goal, feedback, final result
    `sequence: [0, 1, 1, 2, 3, 5]`, and `SUCCEEDED`
  - server log recorded goal receipt, feedback publication, and `Goal Succeeded`
- Confirmed the direct `rclcpp` action client binary on both boards using:
  - client: `lib/examples_rclcpp_minimal_action_client/action_client_not_composable`
  - server: `lib/examples_rclcpp_minimal_action_server/action_server_not_composable`
  - client output reported `Sending goal`, `Waiting for result`, `result received`,
    and the final Fibonacci sequence ending in `55`
  - server log recorded goal receipt, repeated feedback publication, and
    `Goal Succeeded`
- Root-caused a deployment gap where `std_msgs` Python type support failed on
  the board because `libstd_msgs__rosidl_generator_py.so` had not been synced
  into `/data/local/tmp/ohos-prefix/lib`.
- Root-caused the same class of gap for `example_interfaces`, where the package
  directory and native `libexample_interfaces__rosidl_*` libraries were present
  locally but missing on the board runtime until they were pushed explicitly.
- Added `ohos/deploy_ros2_prefix.sh` to ship the full standalone ROS 2 prefix to
  a board in one step so generated Python support libraries are not omitted.
- Added local patch-carrying support:
  - `ohos/apply_workspace_patches.sh`
  - `ohos/patches/`

## Local Source Changes

Key local child-repo adaptations were made in:

- `src/ros2/rcutils`
- `src/ament/ament_cmake`
- `src/ros2/rosidl_typesupport_fastrtps`
- `src/eProsima/foonathan_memory_vendor`
- `src/ros2/rmw_fastrtps`
- `src/ros2/rmw_dds_common`

Important middleware findings:

- The OHOS-specific `SUBNET` participant override in `rmw_fastrtps_shared_cpp/src/participant.cpp` broke remote participant discovery. Reverting to the normal Fast DDS builtin transport path restored cross-device ROS 2 discovery.
- Graph-disable support was added for diagnostics, but the main working cross-device ROS 2 path now uses the normal graph-enabled flow.

## Validation Completed

Validated on two RK3588S boards:

- `ec29004133314d38433031a72cc63c00`
- `ec290041543253394320030498801a00`

Successful checks:

- utility smoke on both boards
- FastDDS single-board and cross-board communication
- ROS 2 single-board pub/sub
- ROS 2 manual cross-board pub/sub in both directions
- subscriber filtering fix: ignored unrelated payloads and later accepted the expected payload
- expanded ROS 2 RK3588S core build now includes:
  - `unique_identifier_msgs`
  - `action_msgs`
  - `composition_interfaces`
  - `action_tutorials_interfaces`
  - `lifecycle_msgs`
  - `rosgraph_msgs`
  - `statistics_msgs`
  - `std_srvs`
  - `example_interfaces`
  - `geometry_msgs`
  - `sensor_msgs`
  - `tf2_msgs`
  - `tf2_geometry_msgs`
  - `composition`
  - `tinyxml2_vendor`
  - `pluginlib`
  - `urdfdom_headers`
  - `urdfdom`
  - `urdf_parser_plugin`
  - `urdf`
  - `ros2plugin`
  - `kdl_parser`
  - `robot_state_publisher`
  - `rcl_action`
  - `rcl_lifecycle`
  - `libstatistics_collector`
  - `rclcpp`
  - `rclcpp_action`
  - `rclcpp_lifecycle`
  - `console_bridge_vendor`
  - `class_loader`
  - `rclcpp_components`
  - `demo_nodes_cpp_native`
  - `action_tutorials_cpp`
  - `examples_rclcpp_minimal_subscriber`
  - `examples_rclcpp_wait_set`
  - `quality_of_service_demo_cpp`
  - `demo_nodes_cpp`
  - `logging_demo`
  - `image_transport`
  - `point_cloud_transport`
  - `rosbag2_storage`
  - `rosbag2_cpp`
  - `rosbag2_storage_sqlite3`
  - `mcap_vendor`
  - `rosbag2_storage_mcap`
  - `rosbag2_examples_cpp`
  - `keyboard_handler`
  - `rosbag2_interfaces`
  - `rosbag2_transport`
  - `rosbag2_py`
  - `rosbag2_examples_py`
- target Python 3.12 stack now includes:
  - `rclpy`
  - `rosidl_runtime_py`
  - `rosidl_generator_py`
  - Python bindings for `builtin_interfaces`
  - Python bindings for `service_msgs`
  - Python bindings for `rcl_interfaces`
  - Python bindings for `unique_identifier_msgs`
  - Python bindings for `lifecycle_msgs`
  - Python bindings for `composition_interfaces`
  - Python bindings for `type_description_interfaces`
  - Python bindings for `std_srvs`
  - Python bindings for `example_interfaces`
  - Python bindings for `sensor_msgs`
  - Python bindings for `geometry_msgs`
  - Python bindings for `tf2_msgs`
  - `tf2_geometry_msgs`
  - `tf2_py`
  - `tf2_ros_py`
  - `ros2cli`
  - `ros2node`
  - `ros2topic`
  - `ros2service`
  - `ros2param`
  - `ros2interface`
  - `ros2pkg`
  - `ros2run`
  - `ros2action`
  - `ros2component`
  - `ros2lifecycle`
  - `ros2multicast`
  - `ros2doctor`
- host-side colcon wrapper added for RK3588S/OHOS selected-package builds:
  - `ohos/colcon_rk3588s.sh`
- board-side Python command validation succeeded with:
  - `import rclpy`
  - `import rcl_interfaces.msg`
  - `ros2 --help`
  - `ros2 node -h`
  - `ros2 topic -h`
  - `ros2 doctor -h`
  - deployed `ros2` launcher wrapper works on-board without manual environment exports
  - live `rclpy` node discovered by `ros2 node list`
  - `ros2 node info /rclpy_cli_node`
  - `ros2 topic info /rclpy_cli_topic`
  - `ros2 param list /rclpy_cli_node`
  - `ros2 service list`
  - `ros2 service type /rclpy_cli_trigger`
  - `ros2 service call /rclpy_cli_trigger std_srvs/srv/Trigger '{}'`
  - `ros2 action list`
  - `ros2 action info /rclpy_cli_fibonacci`
  - board-local `ros2 action send_goal --feedback /rclpy_cli_fibonacci example_interfaces/action/Fibonacci "{order: 5}"`
  - `ros2 interface show geometry_msgs/msg/Twist`
  - `examples_rclcpp_minimal_service/service_main`
  - `examples_rclcpp_minimal_client/client_main`
  - `examples_rclcpp_minimal_action_server/action_server_not_composable`
  - `examples_rclcpp_minimal_action_client/action_client_not_composable`
  - board-local `ros2 action send_goal --feedback /fibonacci example_interfaces/action/Fibonacci "{order: 5}"`
  - `ros2 interface show tf2_msgs/msg/TFMessage`
  - `lib/tf2_ros/tf2_echo`
  - `import tf2_geometry_msgs`
  - `lib/tf2_ros/static_transform_publisher`
  - `lib/composition/dlopen_composition`
  - `lib/rclcpp_components/component_container`
  - `ros2 component types composition`
  - `ros2 component load /ComponentManager composition composition::Talker`
  - `ros2 component load /ComponentManager composition composition::Listener`
  - `ros2 component list /ComponentManager`
  - `lib/pluginlib/list_plugins urdf_parser_plugin urdf::URDFParser`
  - `ros2 plugin list --package urdf`
  - `lib/demo_nodes_cpp_native/talker`
  - `ros2 topic echo /chatter --once`
  - `lib/action_tutorials_cpp/fibonacci_action_server`
  - `lib/action_tutorials_cpp/fibonacci_action_client`
  - `lib/examples_rclcpp_minimal_subscriber/wait_set_subscriber`
  - `lib/examples_rclcpp_wait_set/wait_set_talker`
  - `lib/examples_rclcpp_wait_set/wait_set_listener`
  - `lib/quality_of_service_demo_cpp/message_lost_listener`
  - `lib/quality_of_service_demo_cpp/message_lost_talker`
  - `lib/demo_nodes_cpp/talker`
  - `lib/demo_nodes_cpp/listener`
  - `lib/logging_demo/logging_demo_main`
  - `lib/image_transport/list_transports`
  - `lib/point_cloud_transport/list_transports`
  - `lib/rosbag2_examples_cpp/simple_bag_recorder`
  - `bin/data_generator_executable`
  - `bin/data_generator_node`
  - `bin/rosbag2csv`

Representative evidence:

```text
fastdds_e2e_ok
pubsub_smoke_ok
subscriber_received
payload=test_payload
subscriber_ignored_payload=wrong_payload
payload=expected_payload
subscriber_wait_publisher_count=1
payload=rk3588s_rcl_payload
rclpy_import_ok
rcl_interfaces_import_ok
ros2 is an extensible command-line tool for ROS 2.
Various node related sub-commands
Various topic related sub-commands
Check ROS setup and other potential issues
/rclpy_cli_node
Type: builtin_interfaces/msg/Time
Publisher count: 1
demo_text
use_sim_time
```

## Install Outputs

- `install/ohos-ros2`: standalone ROS 2 prefix
- `install/ohos-fastdds`: standalone FastDDS prefix
- `install/ohos-arm64`: default OHOS smoke install tree

`./ohos/build_ohos.sh` now builds and installs `ros2_ohos_pubsub_smoke` into the default `install/ohos-arm64` tree.

## Current Notes

- Manual board-to-board ROS 2 validation is the strongest confirmed path.
- HDC file transfer and shell execution work, but long scripted multi-step runs can still be timing-sensitive.
- `ohos/test_cross_board_pubsub.sh` now uses the default install tree and quotes topic/payload safely for remote execution.
- The active OHOS SDK root on this machine is `/home/kaihong/M-DDS`; build scripts now fall back to it when `/home/kaihong/M-DDS_4.1` is absent.
- `console_bridge` was sourced from the Ubuntu source package locally because GitHub access for `console_bridge_vendor` was unavailable in this environment.
- `ohos/colcon_rk3588s.sh` is a host-side colcon wrapper for selected package
  builds. It is validated for `rcutils`, `ament_index_python`, `ros2cli`,
  `ros2pkg`, `ament_index_cpp`, `rcl`, `rcl_action`, `rcl_lifecycle`,
  `rclcpp`, `rclcpp_action`, `rclcpp_components`, `rclcpp_lifecycle`,
  `rclpy`, `rosidl_generator_py`, and `action_tutorials_interfaces`; a full
  workspace colcon build has not been proven. The current workspace reports
  366 colcon-visible packages with `colcon list`. The per-package
  CMake builder remains the validated path for the full RK3588S standalone
  stack in `install/ohos-ros2`.
- Board-side Python 3.12 currently needs:
  - `LD_PRELOAD=/data/local/release/usr/lib/libpython3.12.so.1.0`
  - `LD_LIBRARY_PATH=/data/local/release/usr/lib`
  to load standard extension modules like `array`, `_csv`, and `math` reliably in this environment.
- The installed `ros2` and `ament_index` console scripts are now rewritten as OHOS-aware shell wrappers so normal CLI invocation on the board no longer requires manually exporting `LD_PRELOAD`, `LD_LIBRARY_PATH`, or `PYTHONPATH`.
- Live graph-query validation now works against a running `rclpy` node on the board when the matching `ROS_DOMAIN_ID`, discovery range, and RMW implementation are set for the CLI invocation.
- Older deploy-helper gap, now narrowed by later validation:
  - `ohos/deploy_ros2_prefix_chunked.sh` was later validated as a real board-B
    single-command full-prefix deploy.
  - `ohos/deploy_ros2_interface_runtime.sh` was later hardened and verified
    with common-interface packages on board B.
  - `ohos/deploy_ros2_prefix.sh` still has not been separately re-proven as an
    unchunked one-shot deploy for the whole current 194-resource-entry prefix on
    this host.
- Downstream CMake consumers of interface packages now rely on the injected
  `ohos/cmake/ensure_python_targets.cmake` prelude so exported
  `Python3::Python` and `Python3::NumPy` targets exist before
  `find_package(example_interfaces)` or `find_package(geometry_msgs)` runs.
- `ohos/build_ros2_python_package.sh` now derives the installed Python package
  name from `package.xml` when locating `*.egg-info/entry_points.txt`. This
  fixes OHOS wrapper generation for packages whose Python distribution name does
  not match the source directory basename, such as:
  - `examples_rclpy_minimal_publisher`
  - `examples_rclpy_minimal_subscriber`
  - `examples_rclpy_minimal_service`
  - `examples_rclpy_minimal_client`
- After this helper fix, regenerated installed wrappers such as:
  - `bin/publisher_member_function`
  - `bin/subscriber_member_function`
  now export the expected OHOS Python runtime environment automatically instead
  of keeping the raw `easy_install` launcher format.
- `ohos/build_ros2_python_package.sh` now also emits package-prefixed alias
  wrappers for every Python `console_scripts` entry point. This provides stable
  unique board-side entrypoints in the flat standalone prefix even when
  multiple packages install generic script names such as `client` and `server`.
  Examples:
  - `bin/examples_rclpy_minimal_client__client`
  - `bin/examples_rclpy_minimal_service__service`
  - `bin/examples_rclpy_minimal_action_client__client`
  - `bin/examples_rclpy_minimal_action_server__server`
- Fixed a second Python-wrapper regression in
  `ohos/build_ros2_python_package.sh`: for packages without
  `console_scripts`, the fallback path used to rewrite the shebang of every
  file under `install/ohos-ros2/bin`, which could corrupt unrelated OHOS shell
  wrappers back to a direct Python shebang.
- The helper now limits that fallback shebang rewrite to only the scripts
  listed in the current package's install record
  (`/tmp/ros2-python-package-record.txt`).
- Extended the Python wrapper template again so generated shell wrappers now
  also export:
  - vendored library directories under `\${PREFIX}/opt/*/lib`
  - `AMENT_PREFIX_PATH`
  - `CMAKE_PREFIX_PATH`
  - `COLCON_PREFIX_PATH`
- Repaired the current host shell wrappers in place to add those same vendor /
  prefix environment exports, then backfilled the refreshed wrapper-only tarball
  to both boards.
- Repaired the already-corrupted shell wrappers in the host install prefix by
  restoring `#!/bin/sh` on wrappers that already contained the OHOS shell-body
  prologue (`SCRIPT_DIR=...`). Verified repairs on:
  - `bin/ros2`
  - `bin/lifecycle_talker`
  - `bin/examples_rclpy_minimal_action_client__client`
- Backfilled the repaired shell-wrapper set to both boards by syncing the
  wrapper-only tarball extracted from the host prefix. Verified on both boards
  that refreshed entrypoints now begin with `#!/bin/sh`, including:
  - `/data/local/tmp/ohos-prefix/bin/ros2`
  - `/data/local/tmp/ohos-prefix/bin/demo_nodes_py__add_two_ints_server`
- Synced the new alias wrappers to board A so the installed runtime prefix now
  contains both the original script names and package-qualified direct
  invocation names for the colliding `examples_rclpy` service and action
  packages.
- Built the additional pure-Python `examples_rclpy` support lane into the OHOS
  prefix:
  - `examples/rclpy/executors`
  - `examples/rclpy/guard_conditions`
- Confirmed `examples_rclpy_guard_conditions` runtime on board A using:
  - `bin/examples_rclpy_guard_conditions__trigger_guard_condition`
  - log reported the expected self-triggered shutdown sequence:
    - `waiting for 'spin_once' to finish...`
    - `timer callback triggered guard condition`
    - `guard callback called shutdown`
- Confirmed `examples_rclpy_executors` runtime on board A using:
  - `bin/examples_rclpy_executors__talker`
  - `bin/examples_rclpy_executors__listener`
  - talker log recorded repeated publishes such as:
    - `Publishing: "Hello World: 10"`
    - `Publishing: "Hello World: 11"`
  - listener log recorded the matching receives:
    - `I heard: "Hello World: 10"`
    - `I heard: "Hello World: 11"`
- Confirmed the remaining `examples_rclpy_executors` entrypoints on board A
  using direct target-Python invocation against the installed package payload:
  - `examples_rclpy_executors.composed`
  - `examples_rclpy_executors.callback_group`
  - `examples_rclpy_executors.custom_executor`
  - `examples_rclpy_executors.custom_callback_group`
- `examples_rclpy_executors.composed` proof on board A showed both sides of the
  in-process talker/listener path:
  - `Publishing: "Hello World: 0"`
  - `I heard: "Hello World: 0"`
- `examples_rclpy_executors.callback_group` proof on board A showed the
  `double_talker` / listener path with concurrent callback handling:
  - `double_talker`: `Publishing: "Hello World: 5"`
  - listener: `I heard: "Hello World: 5"`
- `examples_rclpy_executors.custom_callback_group` proof on board A showed the
  throttled intermittent talker path:
  - `intermittent_talker`: `Publishing: "Hello World: 13"`
  - `intermittent_talker`: `Publishing: "Hello World: 25"`
- `examples_rclpy_executors.custom_executor` proof on board A showed both the
  standard chatter path and the `/estop` subscriber branch:
  - talker: `Publishing: "Hello World: 0"`
  - listener: `I heard: "Hello World: 0"`
  - `estopper`: `I heard: "stop"`
- Synced the package-qualified alias wrappers for `examples_rclpy_executors`
  and `examples_rclpy_guard_conditions` to board A so these packages can be
  invoked without colliding with other generic `talker` / `listener` style
  scripts already present in the prefix.
- Built the perception-adjacent Python lane into the OHOS prefix:
  - `sensor_msgs_py`
  - `examples/rclpy/topics/pointcloud_publisher`
- Confirmed `examples_rclpy_pointcloud_publisher` runtime on board A using:
  - `bin/examples_rclpy_pointcloud_publisher__pointcloud_publisher`
  - board-local `ros2 node info /pc_publisher`
  - board-local `ros2 topic info /test_cloud`
  - node info reported the publisher:
    - `/test_cloud: sensor_msgs/msg/PointCloud2`
  - topic info reported:
    - `Type: sensor_msgs/msg/PointCloud2`
    - `Publisher count: 1`
- The chunked-deploy investigation found a host-specific `hdc` quirk:
  under Bash-side captured execution (`$(...)`, temp-file redirection, and
  stdout parsing), `hdc shell ...` can report misleading transport failures.
  The fix was to remove captured shell probes and switch the helper to
  uncaptured, exit-status-only `hdc shell test ...` checks.
- After this redesign:
  - `ohos/tools/hdc_send_verify.sh` now verifies remote paths with direct
    uncaptured `hdc shell test -e ...`
  - `ohos/deploy_ros2_prefix_chunked.sh` now uses uncaptured shell-status
    probes for device readiness, remote directory/file existence, and remote
    setup instead of marker-string parsing
  - the chunked synthetic-prefix repro now completes end-to-end and returns:
    - `ros2_prefix_chunked_deploy_ok`
- Re-ran the rewritten `ohos/deploy_ros2_prefix_chunked.sh` against the real
  `install/ohos-ros2` payload for board B
  (`ec290041543253394320030498801a00`):
  - local bundle split into `18` chunks
  - all chunks verified on the first send attempt
  - device-side archive reassembly and prefix extraction completed successfully
  - final output returned:
    - `ros2_prefix_chunked_deploy_ok`
    - `bundle_gz_size=73656131`
- Updated both full-prefix deploy helpers so the normal runtime payload now
  includes `opt/` when present:
  - `ohos/deploy_ros2_prefix.sh`
  - `ohos/deploy_ros2_prefix_chunked.sh`
- Revalidated the real board-B chunked deploy after adding `opt/` to the
  payload:
  - local bundle now split into `19` chunks
  - deploy again completed end-to-end with:
    - `ros2_prefix_chunked_deploy_ok`
    - `bundle_gz_size=76173874`
  - board B now contains vendored runtime libraries under:
    - `/data/local/tmp/ohos-prefix/opt/yaml_cpp_vendor/lib`
    - `/data/local/tmp/ohos-prefix/opt/sqlite3_vendor/lib`
    - `/data/local/tmp/ohos-prefix/opt/liblz4_vendor/lib`
    - `/data/local/tmp/ohos-prefix/opt/zstd_vendor/lib`
- Confirmed the refreshed board-B runtime prefix after the real chunked deploy
  using:
  - `/data/local/tmp/ohos-prefix/bin/ros2 --help`
  - output listed the expected CLI commands including:
    - `action`
    - `component`
    - `doctor`
    - `interface`
    - `lifecycle`
    - `run`
    - `service`
    - `topic`
- Built the pure-Python `action_tutorials_py` package into the OHOS prefix.
- Confirmed `action_tutorials_py` runtime on board B using:
  - `bin/action_tutorials_py__fibonacci_action_server`
  - `bin/action_tutorials_py__fibonacci_action_client`
  - after syncing the generated `action_tutorials_interfaces` Python runtime
    into the refreshed board-B prefix
  - client log reported:
    - `Goal accepted :)`
    - repeated Fibonacci feedback arrays
    - final result:
      - `Result: array('i', [0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55])`
  - server log reported:
    - `Executing goal...`
    - repeated `Feedback: array('i', [...])`
- Rebuilt `demo_nodes_py` with the fixed Python-package helper so its board-side
  scripts now use the OHOS shell-wrapper form and package-qualified aliases,
  including:
  - `bin/demo_nodes_py__add_two_ints_server`
  - `bin/demo_nodes_py__add_two_ints_client`
  - `bin/demo_nodes_py__introspection`
- Confirmed `demo_nodes_py` AddTwoInts runtime on board B using:
  - `bin/demo_nodes_py__add_two_ints_server`
  - `bin/demo_nodes_py__add_two_ints_client`
  - client log reported:
    - `Result of add_two_ints: 5`
  - server log reported:
    - `Incoming request`
    - `a: 2 b: 3`
- Confirmed `demo_nodes_py` service introspection runtime on board B using:
  - `bin/demo_nodes_py__introspection`
  - board-local `ros2 topic list --include-hidden-topics -t`
  - board-local `ros2 topic info /add_two_ints/_service_event`
  - board-local `ros2 topic echo /add_two_ints/_service_event --once`
  - hidden topic list reported:
    - `/add_two_ints/_service_event [example_interfaces/srv/AddTwoInts_Event]`
  - topic info reported:
    - `Type: example_interfaces/srv/AddTwoInts_Event`
    - `Publisher count: 2`
  - one-shot echo returned a concrete service event sample with:
    - `event_type: 2`
    - `sequence_number: 67`
- Built the pure-Python `topic_monitor` package into the OHOS prefix.
- Confirmed `topic_monitor` runtime on board B using:
  - `bin/topic_monitor__topic_monitor`
  - `bin/topic_monitor__data_publisher critical --end-after 5`
  - publisher log reported:
    - `Publishing on topic: critical_data`
    - `Publishing: "0"` ... `Publishing: "4"` ... `Publishing: "-1"`
  - monitor log reported:
    - `Subscribing to topic: /critical_data`
    - `Publishing reception rate on topic: reception_rate/critical_data_`
    - `/critical_data: Alive`
    - `/critical_data: Offline`
- Built the pure-Python `quality_of_service_demo_py` package into the OHOS
  prefix.
- Confirmed `quality_of_service_demo_py` runtime on board B using:
  - `bin/quality_of_service_demo_py__incompatible_qos reliability`
  - runtime reported both QoS event callbacks:
    - `Offered incompatible qos - total 1 delta 1 last_policy_kind: ...RELIABILITY`
    - `Requested incompatible qos - total 1 delta 1 last_policy_kind: ...RELIABILITY`
  - finite run completed after publishing the expected talker samples `0..4`
- Confirmed `demo_nodes_py` logger-service runtime on board B using:
  - `bin/demo_nodes_py__use_logger_service`
  - runtime demonstrated logger-service-controlled level changes:
    - default logger level `0`
    - debug logger level `10`
    - warn logger level `30`
    - error logger level `40`
  - log output matched the configured level transitions, including:
    - `Output 2 with DEBUG logger level.`
    - `Output 3 with WARN logger level.`
    - `Output 4 with ERROR logger level.`
- Confirmed `demo_nodes_py` matched-event runtime on board B using:
  - `bin/demo_nodes_py__matched_event_detect`
  - runtime exercised both publisher-matched and subscription-matched event
    callbacks in a finite sequence and reported:
    - `First subscription is connected.`
    - `The changed number of connected subscription is 1 and current number of connected subscription is 2`
    - `Last subscription is disconnected.`
    - `First publisher is connected.`
    - `The changed number of connected publisher is 1 and current number of connected publisher is 2`
    - `Last publisher is disconnected.`
- Probed `demo_nodes_py` `set_parameters_callback` on board B using:
  - `bin/demo_nodes_py__set_parameters_callback`
  - board-local `ros2 param set /set_parameters_callback param1 1.0`
  - board-local `ros2 param get /set_parameters_callback param1`
  - board-local `ros2 param get /set_parameters_callback param2`
- Observed runtime behavior on board B:
  - setting `param1` succeeds
  - `param1` reads back as `1.0`
  - `param2` still reads back as `0.0`
- At this point this indicated the basic parameter-service path was working,
  but the example's
  expected pre-set callback side effect (`param1` update causing `param2=4.0`)
  was not taking effect on the current board runtime. The later unique-node
  rerun below reclassifies this as stale or duplicate graph state rather than a
  runtime deviation.
- Source inspection against upstream `rclpy` semantics indicates this
  `set_parameters_callback` behavior would have been a runtime deviation rather
  than a `demo_nodes_py` package bug if it had reproduced.
- Confirmed full `demo_nodes_py` `async_param_client` runtime on board B using:
  - `bin/async_param_client`
  - `lib/demo_nodes_cpp/parameter_blackboard`
  - plus synced `share/demo_nodes_py` and the ament-index package resource for
    `demo_nodes_py`
- Runtime on board B completed the full flow:
  - set parameters
  - list parameters
  - get parameters
  - load parameters from `params.yaml`
  - delete parameters
  with successful results reported for each step
- `ros2bag` is now built and synced into board B's prefix, and the refreshed
  wrapper environment is sufficient for the CLI layer to load:
  - `ros2 bag -h` now lists:
    - `burst`
    - `convert`
    - `info`
    - `list`
    - `play`
    - `record`
    - `reindex`
  - `ros2 bag list storage` now resolves installed storage plugins:
    - `mcap`
    - `sqlite3`
- This means the `ros2bag` user-facing lane has advanced from:
  - command missing
  to:
  - command present
  - verb extensions loaded
  - plugin discovery working
- Fixed a `ros2bag` Python-side bug in
  `src/ros2/rosbag2/ros2bag/ros2bag/api/__init__.py`: when no storage-plugin
  CLI extension module was present, the fallback preset-profile list was built
  as `['none']` instead of `[('none', ...)]`, which caused the default storage
  preset to become `'n'` on board B.
- Rebuilt and resynced `ros2bag` to board B, which removed the
  `Invalid storage preset profile string: n` failure from `ros2 bag record`.
- Updated `rosbag2_py` so the OHOS board runtime automatically enables the
  RTLD_GLOBAL path when the deployed board prefix is detected via
  `AMENT_PREFIX_PATH`, instead of requiring the manual
  `ROSBAG2_PY_TEST_WITH_RTLD_GLOBAL=1` workaround.
- Rebuilt and resynced `rosbag2_py` to board B.
- Confirmed full user-facing `ros2 bag` recording on board B using:
  - `ros2 bag record --storage sqlite3 --topics /chatter -o /data/local/tmp/cli_bag_sqlite3_auto2`
  - board-local `demo_nodes_py__talker`
  - `ros2 bag reindex /data/local/tmp/cli_bag_sqlite3_auto2`
  - `ros2 bag info /data/local/tmp/cli_bag_sqlite3_auto2`
- Proof on board B:
  - recorder opened:
    - `/data/local/tmp/cli_bag_sqlite3_auto2/cli_bag_sqlite3_auto2_0.db3`
  - recorder subscribed to `/chatter`
  - `ros2 bag info` reported:
    - `Storage id: sqlite3`
    - `Messages: 101`
    - `Topic: /chatter | Type: std_msgs/msg/String | Count: 101`
- `ros2 bag` is therefore board-proven on board B for the `sqlite3` storage
  backend. At this point `mcap` remained a backend-specific follow-up lane
  rather than a blocker for the user-facing rosbag2 workflow. The later closure
  below proves the `mcap` backend too.
- Built the core launch stack into the OHOS host prefix:
  - `osrf_pycommon`
  - `launch`
  - `launch_xml`
  - `launch_yaml`
  - `launch_ros`
  - `ros2launch`
- Synced the launch stack, launch frontends, and their Python runtime
  dependencies (`lark`, `yaml`) to board B.
- Confirmed user-facing `ros2 launch` on board B using:
  - `ros2 launch demo_nodes_cpp talker_listener_launch.py --noninteractive`
  - after syncing `share/demo_nodes_cpp` and its ament-index package resource
- Proof on board B:
  - launch system started:
    - `process started with pid ...` for `talker`
    - `process started with pid ...` for `listener`
  - launched processes exchanged messages:
    - `Publishing: 'Hello World: 10'`
    - `I heard: [Hello World: 10]`
    - `Publishing: 'Hello World: 19'`
    - `I heard: [Hello World: 19]`
- `ros2 action send_goal` against the `rclcpp` Fibonacci server depends on both
  `share/example_interfaces/action/Fibonacci.action` and the generated Python
  package being present on the board runtime prefix.
- `tf2_geometry_msgs` is now built. The blocking Orocos dependency was resolved
  by:
  - downloading the exact upstream `orocos_kinematics_dynamics` source zip
  - staging it as a local git repository under `/tmp`
  - building `orocos_kdl_vendor` from that local VCS path
  - extracting local Eigen headers from the Debian `libeigen3-dev` package into
    `/tmp/eigen3-root`
  - forwarding `EIGEN3_INCLUDE_DIR` into the vendor sub-build
- `pluginlib` required a target-built `libtinyxml2.so`; the host x86_64
  library was not usable for OHOS. After cross-building TinyXML2 into the OHOS
  prefix with a proper SONAME and rebuilding `pluginlib`, `list_plugins`
  stopped encoding the absolute host library path in its runtime dependencies.

## Latest Parallel Closure Pass

- Closed the remaining `ros2 bag` backend follow-up on board B:
  - started board-local `demo_nodes_py__talker`
  - recorded with `ros2 bag record --storage mcap --topics /chatter -o /data/local/tmp/cli_bag_mcap_probe_with_talker`
  - `ros2 bag info /data/local/tmp/cli_bag_mcap_probe_with_talker` reported:
    - `Storage id: mcap`
    - `Messages: 15`
    - `Topic: /chatter | Type: std_msgs/msg/String | Count: 15 | Serialization Format: cdr`
  - `ros2 bag` is now board-proven on board B for both `sqlite3` and `mcap`.
- Re-ran `demo_nodes_py__set_parameters_callback` on board B using a unique
  remapped node name:
  - `demo_nodes_py__set_parameters_callback --ros-args -r __node:=set_parameters_callback_probe`
  - `ros2 param set /set_parameters_callback_probe param1 1.0`
  - `ros2 param get /set_parameters_callback_probe param1`
  - `ros2 param get /set_parameters_callback_probe param2`
  - observed:
    - `param1` reads back `1.0`
    - `param2` reads back `4.0`
  - the earlier `param2=0.0` result is therefore best classified as a stale or
    duplicate node/service graph issue, not an rclpy runtime deviation.
- Closed the `image_transport/raw` runtime gap on board A:
  - built `camera_calibration_parsers`, `camera_info_manager`,
    `camera_info_manager_py`, and `image_common`
  - synced the missing `libimage_transport_plugins.so` and related runtime
    content to board A
  - `lib/image_transport/list_transports` now reports:
    - `Declared transports: image_transport/raw`
    - raw publisher and subscriber details from package `image_transport`
  - `polled_camera` remains skipped because the source package has
    `COLCON_IGNORE` and is still ROS 1/catkin.
- Built and board-validated the remaining common interface packages on board B:
  - `actionlib_msgs`
  - `diagnostic_msgs`
  - `nav_msgs`
  - `shape_msgs`
  - `stereo_msgs`
  - `trajectory_msgs`
  - `visualization_msgs`
  - proof included `ros2 interface show` for representative messages and a
    Python/type-support import check for all seven packages.
- Hardened `ohos/deploy_ros2_interface_runtime.sh` after the common-interface
  deployment exposed a false-success path:
  - bundles `share/<pkg>` and ament-index entries in addition to Python package
    directories and matching `lib<pkg>__rosidl*.so*` libraries
  - removed the unused captured-HDC execution path
  - uses direct HDC retries plus explicit remote path verification
  - verified with `actionlib_msgs` on board B and confirmed
    `ros2 interface show actionlib_msgs/msg/GoalStatusArray` works afterward.
- Closed the `tf2` Eigen conversion build lane:
  - added explicit `eigen3_cmake_module` discovery to `tf2_eigen`
  - extended `ohos/build_ros2_package.sh` to provide a target-valid Eigen3
    CMake config shim using the staged Eigen headers under `/tmp/eigen3-root`
  - built successfully:
    - `tf2_eigen`
    - `tf2_eigen_kdl`
    - `tf2_kdl`
  - deployed the resulting runtime/share content to board B
  - board-local `ros2 pkg prefix` resolves all three packages and
    `libtf2_eigen_kdl.so` is present under the board prefix.
- Earlier colcon lane status, superseded by the latest closure below:
  - `ohos/colcon_rk3588s.sh` now defaults to the validated sanity lane
    `rcutils ament_index_python`
  - `bash -n ohos/colcon_rk3588s.sh` passes
  - `./ohos/colcon_rk3588s.sh rcutils ament_index_python` completed with both
    packages installed into `install/ohos-colcon-rk3588s`
  - the installed `ament_index` console script is rewritten as an OHOS runtime
    wrapper
  - at this point `ros2pkg` and `ros2cli` still stalled inside colcon's
    `ament_python` task path before `setup.py` started, even though the
    generated setup environment and package metadata calls worked manually.
    The later direct-install wrapper path below resolves this blocker.

## Latest Colcon RK3588S Closure

- Closed the colcon RK3588S wrapper blocker in `ohos/colcon_rk3588s.sh`:
  - `ament_python` packages are now installed directly into the target
    `lib/python3.12/site-packages` tree with `--no-compile`, avoiding the
    colcon `setup.py` command-environment stall.
  - `ament_cmake` packages use a direct CMake/Ninja install path by default
    while still using `colcon list --topological-order` for package discovery
    and ordering. This avoids the same colcon command-environment deadlock seen
    before `rcutils` configure started.
  - generated OHOS console wrappers now include overlay, remote underlay,
    FastDDS, vendor library, and target Python paths.
  - Python path hooks are rewritten for both `hook/pythonpath.*` and
    `environment/pythonpath.*` variants.
  - copied bytecode and non-target Python site-packages are removed after each
    package install.
  - `ROS2_OHOS_COLCON_CLEAN_INSTALL=1` can produce a clean generated prefix
    without stale resource-index entries.
  - the no-argument default package set is the validated 43-package
    ROS 2 CLI/runtime-capable overlay, not the earlier two-package sanity subset.
  - underlay target Python packages are added to the host build `PYTHONPATH`, so
    generated Python entry points such as `rosidl_generator_py` can run during
    dependent interface-package builds.
  - generated CMake extras are rewritten from host Python `site-packages` paths
    to the target `lib/python3.12/site-packages` tree, then validated.
- Host validation completed:
  - `bash -n ohos/colcon_rk3588s.sh`
  - isolated `ament_index_python ros2cli ros2pkg` build completed without the
    previous `ament_python` stall.
  - isolated `ament_index_cpp` build completed through the CMake package path.
  - isolated `rcutils` build completed through the direct CMake package path.
  - isolated `rcl` build completed through the direct CMake package path.
  - isolated `rclcpp` build completed through the direct CMake package path and
    installed `librclcpp.so`.
  - isolated `rclpy` initially exposed a missing wrapper propagation path for
    its OHOS target Python CMake variables. The wrapper now forwards:
    - `RCLPY_OHOS_TARGET_PYTHON_INCLUDE_DIR`
    - `RCLPY_OHOS_TARGET_PYTHON_LIBRARY`
    - `RCLPY_OHOS_TARGET_PYTHON_EXTENSION_SUFFIX`
    - `RCLPY_OHOS_TARGET_PYBIND11_INCLUDE_DIR`
  - isolated `rclpy` then completed and installed
    `_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so` into the normalized
    `lib/python3.12/site-packages` tree.
  - combined clean default-prefix rebuild completed for the expanded overlay:
    - `ROS2_OHOS_COLCON_CLEAN_INSTALL=1 ./ohos/colcon_rk3588s.sh rcutils ament_index_python ament_index_cpp ros2cli ros2pkg rcl rcl_action rcl_lifecycle rclcpp rclcpp_action rclcpp_components rclcpp_lifecycle rclpy rosidl_generator_py action_tutorials_interfaces`
    - `ROS2_OHOS_COLCON_CLEAN_INSTALL=1 ./ohos/colcon_rk3588s.sh rcutils ament_index_python ament_index_cpp ros2cli ros2pkg rcl rcl_action rcl_lifecycle rclcpp rclcpp_action rclcpp_components rclcpp_lifecycle rclpy rosidl_generator_py action_tutorials_interfaces ros2action ros2doctor ros2interface ros2node ros2param ros2run ros2service ros2topic ros2lifecycle ros2component ros2bag ros2cli_common_extensions`
    - `ROS2_OHOS_COLCON_CLEAN_INSTALL=1 ./ohos/colcon_rk3588s.sh`
      now builds the 43-package default set, adding common messages, tf2,
      launch, pluginlib, class loader, and composition packages to the CLI
      lane.
  - clean no-argument default rebuild completed in `/tmp`, proving the wrapper
    default now reproduces the same 43-package ROS 2 CLI/runtime-capable set.
  - clean no-argument default rebuild refreshed the persistent
    `install/ohos-colcon-rk3588s` prefix.
  - cleaned prefix contains these overlay resource-index entries:
    - `action_tutorials_interfaces`
    - `ament_index_cpp`
    - `ament_index_python`
    - `rcl`
    - `rcl_action`
    - `rcl_lifecycle`
    - `rclcpp`
    - `rclcpp_action`
    - `rclcpp_components`
    - `rclcpp_lifecycle`
    - `rclpy`
    - `rcutils`
    - `ros2cli`
    - `ros2action`
    - `ros2bag`
    - `ros2cli_common_extensions`
    - `ros2component`
    - `ros2doctor`
    - `ros2interface`
    - `ros2lifecycle`
    - `ros2node`
    - `ros2param`
    - `ros2pkg`
    - `ros2run`
    - `ros2service`
    - `ros2topic`
    - `rosidl_generator_py`
    - `std_msgs`
    - `example_interfaces`
    - `geometry_msgs`
    - `sensor_msgs`
    - `nav_msgs`
    - `tf2`
    - `tf2_ros`
    - `osrf_pycommon`
    - `launch`
    - `launch_xml`
    - `launch_yaml`
    - `launch_ros`
    - `ros2launch`
    - `class_loader`
    - `pluginlib`
    - `composition`
  - install hygiene checks passed:
    - no `*.pyc` / `*.pyo`
    - no non-target `python3.8` / `python3.11` Python path hooks
    - no generated CMake extras with non-target
      `lib/python*/site-packages` paths
    - only `lib/python3.12/site-packages` remains under the colcon prefix.
  - isolated `rclpy` install hygiene also passed:
    - only `lib/python3.12/site-packages` remains under the isolated prefix
    - no `*.pyc` / `*.pyo`
    - Python path hooks point at `lib/python3.12/site-packages`
- Scope limits:
  - the clean deployed colcon overlay is the 43-package set shown above, not a
    full workspace.
  - a full workspace colcon build has not been proven; the current workspace
    reports 366 colcon-visible packages with `colcon list`.
  - the validated full RK3588S runtime remains the standalone
    `install/ohos-ros2` prefix, which currently has 194 ament-index package
    resource entries.
  - stale generated `SUMMARY.md` files were removed from local and board-side
    ament resource indexes so `ament_index` no longer reports them as packages.
- Board validation completed on both RK3588S devices:
  - `ec29004133314d38433031a72cc63c00`
  - `ec290041543253394320030498801a00`
  - deployed the expanded clean prefix to `/data/local/tmp/ohos-colcon-rk3588s`
  - remote overlay package resource count is `43` on both boards.
  - `ament_index --help` prints the expected usage banner.
  - `ament_index packages` resolves the new overlay packages from
    `/data/local/tmp/ohos-colcon-rk3588s` and underlay packages from
    `/data/local/tmp/ohos-prefix`.
  - `ros2 pkg list` succeeds on both boards through the generated `ros2`
    wrapper.
  - `ros2 pkg prefix rclcpp`, `rclpy`, `rclcpp_action`,
    `action_tutorials_interfaces`, and `rosidl_generator_py` all return
    `/data/local/tmp/ohos-colcon-rk3588s` on both boards.
  - `ros2 pkg prefix ros2topic` and `ros2 pkg prefix
    ros2cli_common_extensions` return `/data/local/tmp/ohos-colcon-rk3588s` on
    both boards.
  - `ros2 pkg prefix osrf_pycommon`, `launch_xml`, `launch_yaml`, and
    `ros2launch` return `/data/local/tmp/ohos-colcon-rk3588s` on both boards.
  - `ros2 topic --help`, `ros2 service --help`, `ros2 node --help`, and
    `ros2 action --help` print their expected usage banners on both boards.
  - `ros2 bag --help`, `ros2 component --help`, `ros2 doctor --help`,
    `ros2 lifecycle --help`, `ros2 param --help`, and `ros2 run --help` print
    their expected usage banners on both boards.
  - `ros2 launch --help` prints the expected usage banner on both boards.
  - `ros2 interface show action_tutorials_interfaces/action/Fibonacci` prints
    the expected goal/result/feedback fields on both boards.
  - `ros2 interface show std_msgs/msg/String`,
    `geometry_msgs/msg/Twist`, `sensor_msgs/msg/Image`, and
    `nav_msgs/msg/Odometry` print the expected message definitions on both
    boards.
  - `ros2 run tf2_ros static_transform_publisher --help` prints the expected
    usage banner on both boards after adding the OHOS-sensitive
    `rclcpp::shutdown()` early-return fix to `geometry2`.
  - deployed `rosidl_generator_py` CMake extras reference
    `lib/python3.12/site-packages` on both boards.
- HDC note:
  - this session's `hdc` often returned process status `-1` after valid stdout
    and messages like `timeout: the monitored command dumped core`; the runtime
    checks above are based on the successful command output produced before
    that host-side HDC status.

## Remaining Migration Backlog

- Current inventory:
  - workspace reports 366 colcon-visible packages.
  - standalone `install/ohos-ros2` has 194 ament-index package resource entries.
  - colcon-generated `install/ohos-colcon-rk3588s` has 43 package resource entries.
  - the colcon overlay is a validated runtime/CLI slice, not a full workspace
    replacement.
- Highest-value next colcon overlay candidates:
  - Python tf2 completion: `tf2_py` is still excluded from the colcon overlay
    because CMake does not yet receive usable Python3 development hints
    (`Python3_INCLUDE_DIRS`, `Python3_LIBRARIES`, and Development components)
    for the OHOS target runtime.
  - robot state publisher lane: `robot_state_publisher` is still excluded from
    the colcon overlay because `Eigen3` is not discoverable through the current
    OHOS underlay/vendor prefix during the `orocos_kdl_vendor`/`kdl_parser`
    dependency path.
  - tf2 follow-ups: `tf2_sensor_msgs` / `tf2_tools` remain optional unless
    sensor transform workflows need them.
  - resource/perception lane: `resource_retriever`, `libcurl_vendor`,
    `laser_geometry`, `point_cloud_transport_py`, `image_tools`.
- Medium-priority completeness work:
  - complete the rosbag resource-index surface for `rosbag2`,
    `rosbag2_compression_zstd`, `rosbag2_storage_default_plugins`, and
    `shared_queues_vendor`. The user-facing `ros2 bag` workflow is already
    board-proven for `sqlite3` and `mcap`.
  - migrate remaining C++ example packages if more board-side regression
    coverage is useful.
  - evaluate CycloneDDS only if a second RMW implementation becomes a runtime
    requirement; Fast DDS remains the validated path.
- Low-priority or likely deferred work:
  - GUI/RViz/RQt/Qt packages are large and low-value on the current headless
    RK3588S validation path.
  - lint, test, benchmark, and tracing packages are mostly host/test
    infrastructure, not first-order target runtime enablement.
  - Connext packages likely require external proprietary target SDK support.
  - `polled_camera` remains intentionally skipped because it has
    `COLCON_IGNORE` and is ROS 1/catkin.
