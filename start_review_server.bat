@echo off
rem ----------------------
rem  start_review_server.bat
rem  Description: Starts the room-presentation review server on 8080 -- the one
rem    app for choosing which Zork I picture and CD-DA track each room of each
rem    game gets. Replaces the two servers this project used to run (art on
rem    8080, scenes on 8081); there is one question left, so there is one app.
rem  Author: suinevere
rem  Dependencies: tools\pres_server.py, tools\.venv
rem  Globals: N/A
rem  Params: N/A
rem  Returns: whatever the server exits with; Ctrl-C stops it
rem ----------------------
setlocal
set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"

set "PY=%REPO%\tools\.venv\Scripts\python.exe"
if not exist "%PY%" set "PY=python"

if not exist "%REPO%\tools\assets\zork1_pool.json" (
    echo Building the picture/track catalogue first...
    "%PY%" "%REPO%\tools\gen_pool.py" || exit /b 1
)

echo Review server on http://127.0.0.1:8080  ^(Ctrl-C to stop^)
"%PY%" "%REPO%\tools\pres_server.py"
set "CODE=%ERRORLEVEL%"

if not "%CODE%"=="0" (
    echo.
    echo   The server did not start. If the venv is missing, create it with:
    echo       python -m venv "%REPO%\tools\.venv"
    echo       "%REPO%\tools\.venv\Scripts\python.exe" -m pip install -r "%REPO%\tools\requirements-review.txt"
    echo.
)

endlocal & exit /b %CODE%
