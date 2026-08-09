@echo off
rem Builds the prism.dll that Accessible Arena ships: 64-bit Release with the
rem static CRT, so users need no VC++ redistributable. Matches the settings
rem recorded in build_arena_x64/CMakeCache.txt.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 ( echo vcvars64 failed & exit /b 1 )
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake -S "%~dp0." -B "%~dp0build_arena_x64" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DPRISM_ENABLE_TESTS=OFF ^
  -DPRISM_ENABLE_DEMOS=OFF ^
  -DPRISM_ENABLE_GDEXTENSION=OFF ^
  -DPRISM_ENABLE_SHIMS=OFF ^
  -DPRISM_ENABLE_LEGACY_BACKENDS=OFF ^
  -DPRISM_ENABLE_LINTING=OFF
if errorlevel 1 ( echo configure failed & exit /b 1 )
cmake --build "%~dp0build_arena_x64" -j 4
