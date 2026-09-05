@ECHO OFF
REM Hear a change to the drum tablature in about a second, without a build.
REM
REM   tools\assets\drums.bat            renders and plays the current tab
REM   tools\assets\drums.bat out.wav    writes somewhere of your choosing
REM
REM The tune, the tempo and the tablature all come from music\songs.json, the
REM same record the build converts from, so this cannot render the tablature at
REM a speed the disc will not play it at. songs.bat is this tool pointed at any
REM other tune in that file.
REM
REM This is the software model, not the chip: it does not reproduce the SCSP's
REM envelope rates or its interpolation, so the drum decay and the noise colour
REM are approximate. Right for judging WHICH ROWS ARE STRUCK -- which is what
REM the tablature decides -- and wrong for judging how a strike sounds. For
REM that, use tools\assets\drums-chip.bat, which is a real build.
SETLOCAL
SET "OUT=%~1"
IF "%OUT%"=="" SET "OUT=%TEMP%\drums-preview.wav"
python "%~dp0preview.py" --song castle-halls "%OUT%" --seconds 24
IF ERRORLEVEL 1 GOTO :eof
ECHO.
ECHO Playing %OUT%
START "" "%OUT%"
ENDLOCAL
