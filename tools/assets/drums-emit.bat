@ECHO OFF
REM Regenerate the shipped pattern data from music\songs.json -- every tune, the
REM drum tablature with them. This is the exact command recorded in the header
REM of music_synth_data.c; run it after editing the tab or the manifest, then
REM build as usual.
REM
REM The per-tune settings that used to be spelled out here now live in
REM music\songs.json, so that this script, tools\assets\drums.bat and
REM tools\assets\songs.bat cannot render the same tune three different ways.
SETLOCAL
SET "REPO=%~dp0..\.."
REM --pat writes the whole catalogue to the disc as well. The CD build links one
REM tune and reads the rest from there, because on that target __heap_start
REM follows .rodata and the catalogue in the image stops the largest story
REM loading. Both outputs come from one run so they cannot disagree.
python "%~dp0mid2pat.py" ^
  --manifest "%REPO%\tools\assets\music\songs.json" ^
  --out "%REPO%\saturn\src\sound\music_synth_data.c" ^
  --pat "%REPO%\saturn\cd\data\BG\MUSIC.PAT"
ENDLOCAL
