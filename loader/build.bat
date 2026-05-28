@echo off
setlocal enabledelayedexpansion

REM Build the dinput8.dll proxy that auto-loads KotorPatcher when the
REM game starts. Output: loader\dinput8.dll (32-bit, side-by-side with
REM swkotor.exe).

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo ERROR: vswhere.exe not found at %VSWHERE%
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set VS_PATH=%%i
)

if not defined VS_PATH (
    echo ERROR: No Visual Studio install with x86 build tools found.
    exit /b 1
)

set VCVARS=!VS_PATH!\VC\Auxiliary\Build\vcvars32.bat
if not exist "!VCVARS!" (
    echo ERROR: vcvars32.bat not found at !VCVARS!
    exit /b 1
)

call "!VCVARS!" >nul 2>&1

pushd "%~dp0"
cl /nologo /LD /O2 /MD /W3 /EHsc /std:c++17 dllmain.cpp /link /DEF:dinput8.def user32.lib /OUT:dinput8.dll
set CL_EXIT=!ERRORLEVEL!
del /q dllmain.obj dllmain.exp dllmain.lib dinput8.exp dinput8.lib >nul 2>&1
popd

if !CL_EXIT! NEQ 0 (
    echo Build failed (cl exit code !CL_EXIT!^).
    exit /b !CL_EXIT!
)

echo Built loader\dinput8.dll.
