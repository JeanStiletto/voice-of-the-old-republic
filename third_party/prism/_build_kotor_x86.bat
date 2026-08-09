@echo off
rem Builds the 32-bit prism.dll the KOTOR mod ships: Release with the static CRT
rem so users need no VC++ redistributable. Matches the settings recorded in
rem build_x86/CMakeCache.txt. Configure and build in one go.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 ( echo vcvars32 failed & exit /b 1 )
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake -S "%~dp0." -B "%~dp0build_kotor_x86" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DPRISM_ENABLE_TESTS=OFF ^
  -DPRISM_ENABLE_DEMOS=OFF ^
  -DPRISM_ENABLE_GDEXTENSION=OFF ^
  -DPRISM_ENABLE_SHIMS=OFF ^
  -DPRISM_ENABLE_LEGACY_BACKENDS=OFF ^
  -DPRISM_ENABLE_LINTING=OFF
if errorlevel 1 ( echo configure failed & exit /b 1 )
cmake --build "%~dp0build_kotor_x86" -j 4
