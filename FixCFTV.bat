@echo off
title FixCFTV - PladixOficial
cd /d "%~dp0"

:: Configura o ambiente de runtime
set "PATH=C:\Users\luking\tools\w64devkit\bin;C:\Users\luking\tools\ffmpeg-9.0.2-full_build-shared\bin;%PATH%"

:: Inicia a interface gráfica moderna em Python
python --version >nul 2>&1
if %ERRORLEVEL% equ 0 (
    start "" pythonw gui\fixcftv_gui.py
    exit /b 0
)

:: Se o Python não estiver instalado, inicia o executável nativo em C
if exist "FixCFTV_GUI.exe" (
    start "" "FixCFTV_GUI.exe"
    exit /b 0
)

:: Caso contrário, executa via terminal
echo [!] Iniciando FixCFTV via linha de comando...
FixCFTV.exe --help
pause
