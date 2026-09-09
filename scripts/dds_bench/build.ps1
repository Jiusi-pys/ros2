$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$python = Join-Path $workspace '.pixi/envs/default/python.exe'
$cmake = Join-Path $workspace '.pixi/envs/default/Library/bin/cmake.exe'
$env:PATH = (Join-Path $workspace '.pixi/envs/default/Library/bin') + ';' + $env:PATH
$env:PYTHONPATH = Join-Path $workspace 'install_ohos/Lib/site-packages'
$env:AMENT_PREFIX_PATH = (Join-Path $workspace 'install_ohos').Replace('\','/')
$build = Join-Path $workspace 'build_ohos/dds_bench'
& $python -m unittest discover -s $PSScriptRoot -p test_contracts.py
if ($LASTEXITCODE -ne 0) { throw 'Host contracts failed' }
& $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$workspace/cmake/ohos-aarch64.toolchain.cmake" "-DCMAKE_PREFIX_PATH=$workspace/install_ohos" '-DCMAKE_BUILD_TYPE=Release' "-DPython3_EXECUTABLE=$python" '-DCMAKE_SKIP_RPATH=ON' '-DCMAKE_LIBRARY_ARCHITECTURE=aarch64-linux-ohos'
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& $cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
