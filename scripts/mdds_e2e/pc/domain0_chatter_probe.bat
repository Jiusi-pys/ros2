@echo off
rem Production-domain-0 /chatter smoke endpoint.  The standalone Bash runner
rem supplies only validated MDDS_D0_* fields and owns this cmd.exe process.
rem This file intentionally does not accept positional arguments: arbitrary
rem caller text must never become a domain-0 ROS command line.
setlocal EnableExtensions DisableDelayedExpansion
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
if errorlevel 1 exit /b %errorlevel%
call C:\pixi_ws\ros2-windows\setup.bat
if errorlevel 1 exit /b %errorlevel%

set "RMW_IMPLEMENTATION=rmw_cyclonedds_cpp"
set "ROS_DOMAIN_ID=0"
set "ROS_LOCALHOST_ONLY="
rem Domain 0 must use the same unicast-only A<->PC leg as the deployed gateway.
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Internal><SocketReceiveBufferSize min='4MB'/><SocketSendBufferSize min='4MB'/></Internal><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"

if not defined MDDS_D0_ROLE exit /b 2
if not defined MDDS_D0_MODE exit /b 2
if not defined MDDS_D0_DIRECTION exit /b 2
if not defined MDDS_D0_TOKEN exit /b 2
if not defined MDDS_D0_COUNT exit /b 2
if not defined MDDS_D0_PAYLOAD_BYTES exit /b 2

python C:\Users\17715\Documents\codes\M-DDS\ros2\scripts\mdds_e2e\domain0_chatter_probe.py ^
  --role %MDDS_D0_ROLE% --mode %MDDS_D0_MODE% --direction %MDDS_D0_DIRECTION% ^
  --token %MDDS_D0_TOKEN% --topic /chatter --count %MDDS_D0_COUNT% ^
  --payload-bytes %MDDS_D0_PAYLOAD_BYTES% --rate-hz 5 ^
  --match-timeout-s 45 --receive-timeout-s 45 --settle-ms 3000 --flush-s 3 --quiet-s 2
exit /b %errorlevel%
