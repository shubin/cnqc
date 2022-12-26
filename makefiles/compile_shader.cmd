@echo off
set fxc="%FXCPATH%"
if "%FXCPATH%"=="" if not "%DXSDK_DIR%"=="" set fxc="%DXSDK_DIR%Utilities\\bin\\x86\\fxc.exe"
if "%FXCPATH%"=="" if     "%DXSDK_DIR%"=="" set fxc="C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.20348.0\\x64\\fxc.exe"
%fxc% %*
