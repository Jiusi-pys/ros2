@echo off
setlocal
if "%OHOS_SDK_PATH%"=="" (
  echo ERROR: OHOS_SDK_PATH is not set 1>&2
  exit /b 2
)
set "TOOLS_DIR=%~dp0"
for %%I in ("%TOOLS_DIR%\..\..") do set "WORKSPACE_ROOT=%%~fI"
if "%QT_HOST_QMAKE%"=="" set "QT_HOST_QMAKE=%TOOLS_DIR%qt5_applications\Qt\bin\qmake.exe"
if not exist "%QT_HOST_QMAKE%" (
  echo ERROR: deterministic host qmake is missing; run bootstrap_qt_host_tools.sh 1>&2
  exit /b 2
)
set "QTCONF=%WORKSPACE_ROOT%\target_deps_src\qtbase-everywhere-src-5.15.8\build-ohos\bin\qt.conf"
if not exist "%QTCONF%" (
  echo ERROR: target qt.conf is missing; run build_qtbase_ohos.sh first 1>&2
  exit /b 2
)
"%QT_HOST_QMAKE%" %* -qtconf "%QTCONF%"
exit /b %ERRORLEVEL%
