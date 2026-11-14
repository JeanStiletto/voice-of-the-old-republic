@echo off
rem Builds the 32-bit prism.dll the KOTOR mod ships, from the shared patched
rem Prism checkout (default: ..\..\prism, i.e. Dev\prism next to this repo; pass
rem another path as the first argument; branch arena-patched carries the local
rem patches, see third_party\prism-dist\README.md). Release, static CRT (users
rem need no VC++ redistributable), every optional PRISM_ENABLE_* off.
rem
rem The result lands in <prism>\build_kotor_x86\prism.dll. Copying it into
rem third_party\prism-dist\x86 is a deliberate separate step.
setlocal
set "PRISM_SRC=%~1"
if "%PRISM_SRC%"=="" set "PRISM_SRC=%~dp0..\..\prism"
if not exist "%PRISM_SRC%\CMakeLists.txt" ( echo Prism source not found at %PRISM_SRC% & exit /b 1 )

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 ( echo vcvars32 failed & exit /b 1 )
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

cmake -S "%PRISM_SRC%" -B "%PRISM_SRC%\build_kotor_x86" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DPRISM_ENABLE_TESTS=OFF ^
  -DPRISM_ENABLE_DEMOS=OFF ^
  -DPRISM_ENABLE_GDEXTENSION=OFF ^
  -DPRISM_ENABLE_SHIMS=OFF ^
  -DPRISM_ENABLE_LEGACY_BACKENDS=OFF ^
  -DPRISM_ENABLE_LINTING=OFF
if errorlevel 1 ( echo configure failed & exit /b 1 )
cmake --build "%PRISM_SRC%\build_kotor_x86" -j 4
