:; PY=""; for c in tools/.venv/bin/python tools/.venv/Scripts/python.exe python3 python; do command -v "$c" >/dev/null 2>&1 && { PY="$c"; break; }; done; [ -n "$PY" ] || { echo "test.bat: no Python found -- looked for tools/.venv and then python3 on PATH" >&2; exit 1; }; exec "$PY" -m pytest tools/tests saturn/tests "$@"
@ECHO Off
REM Runs the Python test suite -- tools/tests and saturn/tests, which are two
REM roots rather than one because the generators and the console are tested
REM separately and neither is a package.
REM
REM The first line is a shell no-op label to cmd and a whole script to sh, so
REM this one file is the runner on Windows, macOS and Linux alike -- the same
REM trick compile.bat uses. Arguments pass straight through to pytest, so
REM `test.bat -k lurking -x` and `test.bat saturn/tests/test_hwram_budget.py`
REM both work.
REM
REM Not every test runs everywhere and that is deliberate: the six map-scan
REM modules skip themselves when OpenCV and pymupdf are absent rather than
REM failing collection and taking the run down with them. `pip install -e
REM .[maps]` turns those on. The 47 saturn/tests/*.c files are NOT run here --
REM they are built and run by the SH-2 toolchain, not by pytest.
SETLOCAL
SET "PY=%~dp0tools\.venv\Scripts\python.exe"
IF NOT EXIST "%PY%" SET "PY=python"
"%PY%" -m pytest "%~dp0tools\tests" "%~dp0saturn\tests" %*
EXIT /B %ERRORLEVEL%
