@echo off
rem Builds worldlines_test.exe (offline check of the map grid: HCMInternal\WorldLinesScene.cpp + WorldLinesGpu.cpp,
rem rendered with Direct3D's software device). Output next to this file.
setlocal
set HERE=%~dp0
if not defined VSDIR set VSDIR=C:\Program Files\Microsoft Visual Studio\2022\Community
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%HERE%"
cl /nologo /std:c++20 /O2 /EHsc /W4 /WX /MT /Fe:worldlines_test.exe main.cpp ..\..\HCMInternal\WorldLinesScene.cpp ..\..\HCMInternal\WorldLinesGpu.cpp ..\..\HCMInternal\WorldLinesEmit.cpp ..\..\HCMInternal\WorldLinesEngine.cpp || exit /b 1
echo built %HERE%worldlines_test.exe
