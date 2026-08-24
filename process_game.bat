@echo off
rem ----------------------
rem  process_game.bat
rem  Description: Walks one game from untagged rooms to pictures on the disc:
rem    starts the review servers, opens each page in turn, and runs the
rem    generators. Does not build the disc.
rem  Author: suinevere
rem  Dependencies: tools\walkthrough.py, tools\.venv
rem  Globals: N/A
rem  Params: %1 -- optional story stem, e.g. ZORK1, to skip the menu
rem  Returns: whatever the walkthrough returns
rem ----------------------
setlocal
set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"

set "PY=%REPO%\tools\.venv\Scripts\python.exe"
if not exist "%PY%" set "PY=python"

"%PY%" "%REPO%\tools\walkthrough.py" %1
set "CODE=%ERRORLEVEL%"

echo.
pause
endlocal & exit /b %CODE%
