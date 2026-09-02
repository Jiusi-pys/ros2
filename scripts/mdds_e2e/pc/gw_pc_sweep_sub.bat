@echo off
rem GW/DS e2e: PC-side sweep subscriber (board_sweep.py sub mode).
rem Extra args are passed through, e.g. --sizes 1024 --count 30 --topic /mdds_dsb_sweep_<run>_<nonce>.
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
rem Must match cyclone_domain_id in mdds_gateway_dsb.conf.  Keep DS-03 off
rem production domain 0 so independently owned PC ROS jobs cannot affect it.
set ROS_DOMAIN_ID=46
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Internal><SocketReceiveBufferSize min='4MB'/></Internal><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
python C:\Users\17715\Documents\codes\M-DDS\ros2\scripts\mdds_e2e\board_sweep.py --mode sub %*
