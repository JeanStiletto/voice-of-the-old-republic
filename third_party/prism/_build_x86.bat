@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 (
  echo vcvars32 failed
  exit /b 1
)
echo --- cl after vcvars32:
where cl
echo --- midl after vcvars32:
where midl
echo --- lib after vcvars32:
where lib
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
echo --- cl after PATH prepend:
where cl
if /I "%1"=="build" goto :build
echo --- configuring
cmake -S "%~dp0." -B "%~dp0build_x86" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPRISM_ENABLE_TESTS=OFF -DPRISM_ENABLE_DEMOS=OFF -DPRISM_ENABLE_GDEXTENSION=OFF -DPRISM_ENABLE_SHIMS=OFF
goto :eof
:build
echo --- building
cmake --build "%~dp0build_x86" -j 4
