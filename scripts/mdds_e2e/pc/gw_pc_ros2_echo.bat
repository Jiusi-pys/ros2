@echo off
rem GW-07: PC-side REAL ros2 CLI (ros2.exe topic echo --once) through the
rem mdds_gateway; proves application-level CLI interop, not just demo nodes.
rem The echo verb fails fast when SEDP has not delivered the topic type yet
rem ("Could not determine the type"), so retry a few times. GW07_ECHO_LOG is
rem set by the harness to a run-unique file; the fallback keeps manual use.
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Internal><SocketReceiveBufferSize min='4MB'/></Internal><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
set "ECHO_LOG=%GW07_ECHO_LOG%"
if "%ECHO_LOG%"=="" set "ECHO_LOG=C:\pixi_ws\gw07_echo.log"
set TOPIC=%~1
if "%TOPIC%"=="" set TOPIC=/chatter
for /l %%i in (1,1,6) do (
  ros2 topic echo --once %TOPIC% > "%ECHO_LOG%" 2>&1
  findstr /c:"data:" "%ECHO_LOG%" >nul && exit /b 0
  ping -n 6 127.0.0.1 >nul
)
exit /b 1
