:; # === Linux & macOS Execution Block ===
:; # bg.bat runs first because games.bat injects what it stages: the eleven
:; # room-background archives land in the same xorriso commit as the Z3 stories,
:; # and music.bat then promotes that data track to Track 01. Ordering is not a
:; # preference here -- BG staged after games.bat would never reach the disc.
:; SCRIPT0="bg.bat"
:; SCRIPT1="games.bat"
:; SCRIPT2="music.bat"
:;
:; echo "Starting master execution..."
:;
:; if [ -f "$SCRIPT0" ]; then
:;     echo "Running $SCRIPT0..."
:;     bash "$SCRIPT0"
:; else
:;     echo "Error: $SCRIPT0 not found."
:; fi
:;
:; if [ -f "$SCRIPT1" ]; then
:;     echo "Running $SCRIPT1..."
:;     bash "$SCRIPT1"
:; else
:;     echo "Error: $SCRIPT1 not found."
:; fi
:;
:; if [ -f "$SCRIPT2" ]; then
:;     echo "Running $SCRIPT2..."
:;     bash "$SCRIPT2"
:; else
:;     echo "Error: $SCRIPT2 not found."
:; fi
:;
:; echo "Master execution complete."
:; exit

@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL

REM bg.bat runs first because games.bat injects what it stages: the eleven
REM room-background archives land in the same xorriso commit as the Z3 stories,
REM and music.bat then promotes that data track to Track 01. Ordering is not a
REM preference here -- BG staged after games.bat would never reach the disc.
SET "SCRIPT0=bg.bat"
SET "SCRIPT1=games.bat"
SET "SCRIPT2=music.bat"

ECHO Starting master execution...

IF EXIST "%SCRIPT0%" (
    ECHO Running %SCRIPT0%...
    CALL "%SCRIPT0%"
) ELSE (
    ECHO Error: %SCRIPT0% not found.
)

IF EXIST "%SCRIPT1%" (
    ECHO Running %SCRIPT1%...
    REM Use CALL so control returns to this master script after execution
    CALL "%SCRIPT1%"
) ELSE (
    ECHO Error: %SCRIPT1% not found.
)

IF EXIST "%SCRIPT2%" (
    ECHO Running %SCRIPT2%...
    CALL "%SCRIPT2%"
) ELSE (
    ECHO Error: %SCRIPT2% not found.
)

ECHO Master execution complete.
ENDLOCAL
GOTO :eof