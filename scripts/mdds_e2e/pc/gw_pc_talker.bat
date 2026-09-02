@echo off
rem GW e2e: PC-side talker; %%1 is the run-unique bridge topic (default chatter).
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
set TOPIC=%~1
if "%TOPIC%"=="" set TOPIC=chatter
ros2 run demo_nodes_cpp talker --ros-args -r chatter:=%TOPIC%
