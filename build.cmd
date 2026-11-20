@echo off
setlocal
pushd "%~dp0" || exit /b 1
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" goto :missingTools
for /f "usebackq delims=" %%v in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "toolsRoot=%%v"
if not defined toolsRoot goto :missingTools
call "%toolsRoot%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build || exit /b 1
pushd build || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /LD /external:I"..\third_party\reaper-sdk" /external:W0 "..\global_record.cpp" user32.lib /link /OUT:reaper_global_record.dll
set "buildStatus=%ERRORLEVEL%"
popd
popd
exit /b %buildStatus%

:missingTools
echo Install Visual Studio Build Tools with the Desktop development with C++ workload.
popd
exit /b 1
