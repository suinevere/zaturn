@ECHO OFF
REM Hear the music on the real SCSP without building either shipped target.
REM Regenerates the pattern data from the drum tablature, builds a disc that
REM boots straight into the tune, and launches Mednafen on it -- about thirty
REM seconds, against two and a half minutes for compile.bat.
REM
REM   tools\assets\drums-chip.bat            the manifest's default tune
REM   tools\assets\drums-chip.bat halls      any other one, by its songs.json id
REM
REM songs.bat lists the ids, and plays the same tune through the offline model
REM in a second -- use that to judge which rows are struck, and this to judge
REM how they sound.
REM
REM It is also the netbin's music path with nothing else attached, which is what
REM makes it the instrument for "the netbin plays no music". The probe builds
REM with no sound driver and with -DNETBIN, so synth_target.cxx takes the same
REM branch there as it does in the netbin -- slSoundOffWait, then the master
REM volume set by hand -- and it links the live scsp.c, synth.c, tracker.c,
REM song_bank.c and tune tables rather than a copy of them. About a hundred and
REM eighty lines of surface, and no menus, no modem, no room engine:
REM
REM   it plays here  -> the synth stack is sound and the netbin's fault is in
REM                     what the netbin does around it.
REM   silent here    -> the fault is inside those five files, reproducible with
REM                     nothing else in the frame.
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

REM Turn the id into the index the catalogue uses, and fail here rather than
REM after a thirty-second build if it names nothing.
SET "SONG=%~1"
IF "%SONG%"=="" SET "SONG=-"
python "%~dp0song_index.py" "%SONG%" "%PROBE%\src\probe_song.h"
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
REM synth.c grew a dependency on the song bank when the twelve-tune catalogue
REM landed, and this list did not follow it. Without these two the probe cannot
REM link the live synth at all -- and a probe that will not build against what
REM ships is a probe that can only ever be run against something else.
COPY /Y "%REPO%\saturn\src\sound\song_bank.c"        "%PROBE%\src\" >NUL
COPY /Y "%REPO%\saturn\src\sound\song_bank.h"        "%PROBE%\src\" >NUL

PUSHD "%PROBE%"
make all
IF ERRORLEVEL 1 (POPD & ECHO probe build failed & GOTO :eof)
POPD

SET "MED=%REPO%\SaturnRingLib\emulators\mednafen\mednafen.exe"
SET "MEDNAFEN_ALLOWMULTI=1"
START "" "%MED%" "%PROBE%\BuildDrop\scspprobe.cue"
ENDLOCAL
