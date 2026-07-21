@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 ( echo vcvars64 failed & exit /b 1 )
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
where cl
cmake -S "%~dp0." -B "%~dp0build_x64" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPRISM_ENABLE_TESTS=OFF -DPRISM_ENABLE_DEMOS=OFF -DPRISM_ENABLE_GDEXTENSION=OFF -DPRISM_ENABLE_SHIMS=OFF
if errorlevel 1 ( echo configure failed & exit /b 1 )
cmake --build "%~dp0build_x64" -j 4
