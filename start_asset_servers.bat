@echo off
rem ----------------------
rem  start_asset_servers.bat
rem  Description: Starts the scene review server on 8081 and the art review
rem    server on 8080, stopping anything already running first.
rem  Author: suinevere
rem  Dependencies: tools\servers.py, tools\.venv
rem  Globals: N/A
rem  Params: N/A
rem  Returns: 0 when both servers answer, 1 otherwise
rem ----------------------
setlocal
set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"

set "PY=%REPO%\tools\.venv\Scripts\python.exe"
if not exist "%PY%" set "PY=python"

"%PY%" "%REPO%\tools\servers.py" start
set "CODE=%ERRORLEVEL%"

if not "%CODE%"=="0" (
    echo.
    echo   A server did not come up. If the venv is missing, create it with:
    echo       python -m venv "%REPO%\tools\.venv"
    echo       "%REPO%\tools\.venv\Scripts\python.exe" -m pip install -r "%REPO%\tools\requirements-review.txt"
    echo.
)

endlocal & exit /b %CODE%
