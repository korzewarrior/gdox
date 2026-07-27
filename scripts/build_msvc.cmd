@echo off
setlocal

if "%~2"=="" (
    echo Usage: build_msvc.cmd SOURCE_ROOT BUILD_DIRECTORY 1>&2
    exit /b 2
)

set "SOURCE_ROOT=%~f1"
set "BUILD_DIRECTORY=%~f2"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo Visual Studio Installer's vswhere.exe was not found. 1>&2
    exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_ROOT=%%I"
if not defined VS_ROOT (
    echo MSVC x64 build tools were not found. 1>&2
    exit /b 1
)

call "%VS_ROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%

set "CMAKE=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "CTEST=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
if not exist "%CMAKE%" (
    echo Visual Studio's CMake component was not found. 1>&2
    exit /b 1
)

"%CMAKE%" ^
    -S "%SOURCE_ROOT%" ^
    -B "%BUILD_DIRECTORY%" ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_TESTING=ON ^
    -DGDOX_BUILD_UI=ON ^
    -DGDOX_BUILD_OPTICAL=ON ^
    -DGDOX_USE_SYSTEM_RAYLIB=OFF ^
    -DGDOX_WARNINGS_AS_ERRORS=ON ^
    "-DCMAKE_C_FLAGS_RELEASE=/O2 /DNDEBUG /Brepro /experimental:deterministic /pathmap:%SOURCE_ROOT%=. /pathmap:%BUILD_DIRECTORY%=build" ^
    "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /DNDEBUG /Brepro /experimental:deterministic /pathmap:%SOURCE_ROOT%=. /pathmap:%BUILD_DIRECTORY%=build" ^
    "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/Brepro"
if errorlevel 1 exit /b %errorlevel%

"%CMAKE%" --build "%BUILD_DIRECTORY%" --parallel 2
if errorlevel 1 exit /b %errorlevel%

"%CTEST%" --test-dir "%BUILD_DIRECTORY%" --output-on-failure
exit /b %errorlevel%
