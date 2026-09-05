@ECHO OFF
REM Render a MIDI through the Saturn synth's model to a WAV you can play.
REM Runs from anywhere -- it resolves its own location rather than depending on
REM the current directory, which is what made the bare python invocation fail
REM when it was run from saturn/ instead of the repo root.
REM
REM   preview.bat "C:\path\to\tune.mid" out.wav [--seconds 25] [--grid 16]
SETLOCAL
python "%~dp0preview.py" %*
ENDLOCAL
