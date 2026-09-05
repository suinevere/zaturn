@ECHO OFF
REM Regenerate the shipped pattern data from the drum tablature and the MIDI.
REM This is the exact command recorded in the header of music_synth_data.c;
REM run it after editing the tab, then build as usual.
SETLOCAL
SET "REPO=%~dp0..\.."
python "%~dp0mid2pat.py" ^
  "%REPO%\tools\assets\music\castle-halls.mid" ^
  "%REPO%\saturn\src\sound\music_synth_data.c" ^
  --name "Shadowgate, Entryway (Hiroyuki Masuno, 1989)" ^
  --source "castle-halls.mid, a fan sequence; drums authored separately in 3/4" ^
  --max-rows 384 --fold-octaves up --bpm 122.3 --grid 32 ^
  --drums-tab "%REPO%\tools\assets\music\castle-halls-drums.tab" --tab-beats 3
ENDLOCAL
