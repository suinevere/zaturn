@ECHO OFF
REM Hear any tune in the catalogue in about a second, without a build.
REM
REM   tools\assets\songs.bat                  lists what there is
REM   tools\assets\songs.bat halls            renders and plays that one
REM   tools\assets\songs.bat halls out.wav    writes somewhere of your choosing
REM
REM The ids and every setting behind them are in music\songs.json, which is the
REM file to edit when a tune plays too fast or should be cut shorter. After an
REM edit, put it in the build with:
REM
REM   python tools\assets\mid2pat.py --manifest tools\assets\music\songs.json ^
REM          --out saturn\src\sound\music_synth_data.c
REM
REM This is the software model, not the chip: it does not reproduce the SCSP's
REM envelope rates or its interpolation, so decays and the noise colour are
REM approximate. Right for judging the notes and the tempo, wrong for judging
REM how a strike sounds -- for that, build. drums.bat is the same tool pointed
REM at the drum tablature.
SETLOCAL
SET "ID=%~1"
SET "OUT=%~2"
IF "%ID%"=="" (
  python -c "import json,sys;d=json.load(open(r'%~dp0music\songs.json'));print('default: '+d['default']);[print('  %-12s %s' %% (s['id'],s.get('name',''))) for s in d['songs']]"
  GOTO :eof
)
IF "%OUT%"=="" SET "OUT=%TEMP%\song-%ID%.wav"
python "%~dp0preview.py" --song "%ID%" "%OUT%" --seconds 60
IF ERRORLEVEL 1 GOTO :eof
ECHO.
ECHO Playing %OUT%
START "" "%OUT%"
ENDLOCAL
