@echo off
rem GW e2e: PC-side large-message publisher (board_sweep.py pub mode).
rem Extra args are passed through, e.g. --sizes 1024,4096 --rate-bps 800000.
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
rem Must match mdds_gateway_test.conf; do not inherit a foreign domain-0 job.
set ROS_DOMAIN_ID=47
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Internal><SocketSendBufferSize min='4MB'/><MaxQueuedRexmitBytes>128MB</MaxQueuedRexmitBytes><MaxQueuedRexmitMessages>2000</MaxQueuedRexmitMessages></Internal><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
python C:\Users\17715\Documents\codes\M-DDS\ros2\scripts\mdds_e2e\board_sweep.py --mode pub %*
