@echo off
echo STAGE0 > C:\pixi_ws\gw_probe.log
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
echo STAGE1 >> C:\pixi_ws\gw_probe.log
call C:\pixi_ws\ros2-windows\setup.bat
echo STAGE2 >> C:\pixi_ws\gw_probe.log
where ros2 >> C:\pixi_ws\gw_probe.log 2>&1
echo STAGE3 >> C:\pixi_ws\gw_probe.log
