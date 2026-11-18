@echo off
rem Runs a command inside the Visual Studio x64 developer environment.
rem Usage: tools\dx12\vsenv.cmd cmake --build build-tx -j4
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
%*
