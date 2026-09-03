@echo off
rem GW-ISO: PC-side exact ByteMultiArray subscriber for gateway domain 47.
rem It is distinct from gw_pc_sweep_sub.bat, whose domain 46 belongs to DS-03.
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
set ROS_DOMAIN_ID=47
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Internal><SocketReceiveBufferSize min='4MB'/></Internal><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
python C:\Users\17715\Documents\codes\M-DDS\ros2\scripts\mdds_e2e\board_sweep.py --mode sub %*
