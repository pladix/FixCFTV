@echo off
title FixCFTV Native Win32 - PladixOficial
cd /d "%~dp0"
set "PATH=C:\Users\luking\tools\w64devkit\bin;C:\Users\luking\tools\ffmpeg-9.0.2-full_build-shared\bin;%PATH%"
start "" "FixCFTV_GUI.exe"
exit /b 0
