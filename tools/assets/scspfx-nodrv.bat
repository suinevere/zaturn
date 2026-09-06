@ECHO OFF
REM The same SCSP sweep as scspfx.bat, built the way the netbin is: no SGL sound
REM driver, and the chip's master volume set by hand the way synth_target.cxx
REM sets it there.
REM
REM   tools\assets\scspfx-nodrv.bat
REM
REM This exists to cut the netbin's silent music in half. The netbin plays no
REM music at all, and the candidates split cleanly at the chip:
REM
REM   slots sound   -> the chip, the sound RAM write and the master volume are
REM                    all fine, so the fault is above them: the tracker's
REM                    V-blank tick, the tune the song bank hands it, or the
REM                    waveform upload. Nothing below the tracker is worth
REM                    looking at.
REM   all silent    -> the fault is below the tracker, in what this file does in
REM                    twelve lines -- the sound block's state after PlanetWeb,
REM                    or the master volume -- and the tracker is innocent.
REM
REM One disc answers which half to spend the next session in. Everything else
REM about it is scspfx.bat: A or RIGHT if you hear the tone, B or DOWN if you do
REM not, LEFT to go back, and the map on screen is the result.
REM
REM With Mednafen's shipped key mapping that is Z for A, X for B, C for C, and
REM the arrow keys -- note that the keyboard's own A key is pad X, which is not
REM one of these.
REM
REM Close Mednafen with its window button, NOT by killing the process: it
REM rewrites mednafen.cfg on exit and a kill mid-write has already once left
REM that file truncated and the emulator unable to start at all.
SETLOCAL
SET "REPO=%~dp0..\.."
SET "PROBE=%REPO%\tools\scspfx"
SET "CDIR=%REPO%\SaturnRingLib\Compiler"
REM msys2 is on PATH for `rm`; without it make dies in clean before compiling.
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
SET "SRL_INSTALL_ROOT=../../SaturnRingLib"

PUSHD "%PROBE%"
REM Built from clean: the two configurations share every object path, so a
REM leftover from the driver build would link straight into this one.
make clean SRL_USE_SGL_SOUND_DRIVER=0
make all SRL_USE_SGL_SOUND_DRIVER=0
IF ERRORLEVEL 1 (POPD & ECHO probe build failed & GOTO :eof)
POPD

SET "MED=%REPO%\SaturnRingLib\emulators\mednafen\mednafen.exe"
SET "CUE=%PROBE%\BuildDrop\scspfx.cue"
IF NOT EXIST "%CUE%" (ECHO no disc at "%CUE%" & GOTO :eof)
ECHO.
ECHO Driver OFF -- this is the netbin's audio environment, not the CD build's.
ECHO The header line on screen says which build you are looking at.
ECHO Z = yes, X = no, LEFT arrow = back. Photograph the map when it is done.
"%MED%" "%CUE%"
ENDLOCAL
