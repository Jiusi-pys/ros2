@echo off
rem GW-07: PC-side REAL ros2 CLI (ros2.exe topic echo --once) through the
rem mdds_gateway; proves application-level CLI interop, not just demo nodes.
rem The echo verb fails fast when SEDP has not delivered the topic type yet
rem ("Could not determine the type"), so retry a few times; the log file is
rem rewritten per attempt and the verdict is "a data: line was received".
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Internal><SocketReceiveBufferSize min='4MB'/></Internal><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
for /l %%i in (1,1,6) do (
  ros2 topic echo --once /chatter > C:\pixi_ws\gw07_echo.log 2>&1
  findstr /c:"data:" C:\pixi_ws\gw07_echo.log >nul && exit /b 0
  ping -n 6 127.0.0.1 >nul
)
exit /b 1
