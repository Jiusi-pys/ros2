@echo off
rem GW e2e preflight: verify PC-side ROS 2 DLLs are loadable (not blocked by
rem Smart App Control) before running the gateway scenarios. Prints NODE_OK
rem (rclpy/rcl/rmw_cyclonedds_cpp/cyclonedds chain) and Publishing lines
rem (demo talker -> rclcpp.dll) on success.
cd /d C:\pixi_ws
call C:\pixi_ws\shell_hook.bat
call C:\pixi_ws\ros2-windows\setup.bat
set RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
rem Preflight is PC-local. Keep it out of production domain 0 so it cannot
rem participate in independently owned ROS jobs while it verifies DLL loading.
set ROS_DOMAIN_ID=48
set "CYCLONEDDS_URI=<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><Interfaces><NetworkInterface address='192.168.8.101'/></Interfaces></General><Discovery><Peers><Peer address='192.168.8.112'/></Peers></Discovery></Domain></CycloneDDS>"
python -c "import rclpy; rclpy.init(); rclpy.create_node('sac_preflight'); rclpy.shutdown(); print('NODE_OK')"
if errorlevel 1 (
  echo PREFLIGHT_FAIL: rclpy node creation blocked
  exit /b 1
)
rem Start and retire ONLY this preflight tree.  Do not use taskkill /IM: that
rem would terminate every user-owned talker.exe already running on the PC.
powershell -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $p=Start-Process -FilePath 'ros2.exe' -ArgumentList @('run','demo_nodes_cpp','talker') -WindowStyle Hidden -PassThru; $start=$p.StartTime.ToUniversalTime().ToFileTimeUtc(); Start-Sleep -Seconds 8; $live=Get-Process -Id $p.Id -ErrorAction SilentlyContinue; if ($null -eq $live -or $live.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $start) { throw 'owned preflight talker identity is gone or reused' }; & taskkill.exe /PID $p.Id /T /F | Out-Null; if ($LASTEXITCODE -ne 0) { throw 'owned preflight talker tree did not stop' }; Write-Output ('PREFLIGHT_TALKER_STOPPED PID=' + $p.Id)"
if errorlevel 1 (
  echo PREFLIGHT_FAIL: owned talker lifecycle check failed
  exit /b 1
)
echo PREFLIGHT_DONE
