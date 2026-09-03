@echo off
rem GW-10: PC endpoint for the simultaneous PC <-> board-B gateway endurance probe.
rem The two test topics and run nonce are supplied by run_mdds_gw.sh; this
rem wrapper pins only the reviewed CycloneDDS side of gateway domain 47.
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
set ROS_DOMAIN_ID=47
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Internal><SocketReceiveBufferSize min='4MB'/><SocketSendBufferSize min='4MB'/><MaxQueuedRexmitBytes>128MB</MaxQueuedRexmitBytes><MaxQueuedRexmitMessages>2000</MaxQueuedRexmitMessages></Internal><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
python C:\Users\17715\Documents\codes\M-DDS\ros2\scripts\mdds_e2e\bidir_sweep.py %*
