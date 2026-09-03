@echo off
rem GW-ISO negative-control listener.  It is bound only to the PC's 192.168.8
rem address; the run-owned Job Object in run_mdds_gw.sh owns this cmd tree.
setlocal EnableExtensions
set "GW_ISO_PORT=%~1"
set "GW_ISO_DURATION=%~2"
if "%GW_ISO_PORT%"=="" set "GW_ISO_PORT=39091"
if "%GW_ISO_DURATION%"=="" set "GW_ISO_DURATION=180"

powershell -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $bindAddress='192.168.8.101'; $port=[int]$env:GW_ISO_PORT; $duration=[int]$env:GW_ISO_DURATION; if ($port -lt 1024 -or $port -gt 65535) { throw 'invalid port' }; if ($duration -lt 30 -or $duration -gt 600) { throw 'invalid duration' }; $listener=[System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse($bindAddress), $port); try { $listener.Start(); Write-Output ('GW_ISO_TCP_LISTENER READY host=' + $bindAddress + ' port=' + $port); $deadline=[DateTime]::UtcNow.AddSeconds($duration); $accepts=0; while ([DateTime]::UtcNow -lt $deadline) { if ($listener.Pending()) { $client=$listener.AcceptTcpClient(); try { $accepts++; Write-Output ('GW_ISO_TCP_LISTENER ACCEPT remote=' + $client.Client.RemoteEndPoint.ToString()) } finally { $client.Dispose() } } else { Start-Sleep -Milliseconds 50 } }; Write-Output ('GW_ISO_TCP_LISTENER TIMEOUT accepts=' + $accepts) } finally { $listener.Stop() }"
exit /b %ERRORLEVEL%
