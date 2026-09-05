@ECHO OFF
REM Hear the music on the real SCSP without building either shipped target.
REM Regenerates the pattern data from the drum tablature, builds a disc that
REM boots straight into the tune, and launches Mednafen on it -- about thirty
REM seconds, against two and a half minutes for compile.bat.
REM
REM   tools\assets\drums-chip.bat
REM
REM Close Mednafen with its window button, NOT by killing the process: it
REM rewrites mednafen.cfg on exit and a kill mid-write has already once left
REM that file truncated and the emulator unable to start at all.
SETLOCAL
SET "REPO=%~dp0..\.."
SET "PROBE=%REPO%\tools\scspprobe"
SET "CDIR=%REPO%\SaturnRingLib\Compiler"
REM msys2 is on PATH for `rm`; without it make dies in clean before compiling.
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
SET "SRL_INSTALL_ROOT=../../SaturnRingLib"

CALL "%~dp0drums-emit.bat"
IF ERRORLEVEL 1 GOTO :eof

COPY /Y "%REPO%\saturn\src\sound\scsp.c"             "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\scsp.h"             "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\synth.c"            "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\synth.h"            "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\synth_waves.c"      "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\tracker.c"          "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\tracker.h"          "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\music_synth_data.c" "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\music_synth_data.h" "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\synth_target.cxx"   "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\synth_target.h"     "%PROBE%\src\" >NUL

PUSHD "%PROBE%"
make all
IF ERRORLEVEL 1 (POPD & ECHO probe build failed & GOTO :eof)
POPD

SET "MED=%REPO%\SaturnRingLib\emulators\mednafen\mednafen.exe"
SET "MEDNAFEN_ALLOWMULTI=1"
START "" "%MED%" "%PROBE%\BuildDrop\scspprobe.cue"
ENDLOCAL
