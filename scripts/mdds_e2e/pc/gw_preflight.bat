@echo off
rem GW e2e preflight: verify PC-side ROS 2 DLLs are loadable (not blocked by
rem Smart App Control) before running the gateway scenarios. Prints NODE_OK
rem (rclpy/rcl/rmw_cyclonedds_cpp/cyclonedds chain) and Publishing lines
rem (demo talker -> rclcpp.dll) on success.
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
python -c "import rclpy; rclpy.init(); rclpy.create_node('sac_preflight'); rclpy.shutdown(); print('NODE_OK')"
if errorlevel 1 (
  echo PREFLIGHT_FAIL: rclpy node creation blocked
  exit /b 1
)
start /b "" ros2 run demo_nodes_cpp talker
rem GNU coreutils timeout.exe (Git usr\bin) shadows Windows timeout in this
rem PATH and rejects "/t"; ping -n gives a plain ~8 s wait either way.
ping -n 9 127.0.0.1
taskkill /F /IM talker.exe
echo PREFLIGHT_DONE
