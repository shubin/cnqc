@echo off
set fxc="%FXCPATH%"
if "%FXCPATH%"=="" set fxc="%DXSDK_DIR%Utilities\\bin\\x86\\fxc.exe"
%fxc% %*