@echo off
rem ----------------------
rem  stop_asset_servers.bat
rem  Description: Stops both review servers, including copies that failed to
rem    bind a port and lingered without ever being recorded.
rem  Author: suinevere
rem  Dependencies: tools\servers.py
rem  Globals: N/A
rem  Params: N/A
rem  Returns: 0
rem ----------------------
setlocal
set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"

set "PY=%REPO%\tools\.venv\Scripts\python.exe"
if not exist "%PY%" set "PY=python"

"%PY%" "%REPO%\tools\servers.py" stop

endlocal & exit /b 0
