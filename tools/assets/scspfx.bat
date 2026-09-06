@ECHO OFF
REM Find out which SCSP slots the SGL sound driver leaves alone -- the one
REM question standing between Lurking Horror's sound effects and playing them
REM out of sound RAM instead of a heap that has 6,200 bytes left.
REM
REM   tools\assets\scspfx.bat
REM
REM Builds a disc that writes a tone into sound RAM and keys the slots one at a
REM time, then launches the emulator on it. It holds each slot until you answer
REM for it: press A or C if you hear the tone, B if you hear nothing, and LEFT
REM to go back and re-test the slot before. Thirty-two answers, at your pace.
REM
REM Every answer stays on screen as a map of + (ours), . (the driver's) and ?
REM (not yet asked), so the finished screen is the whole result -- photograph it
REM rather than trying to remember it. It reads the two runs that matter out
REM loud underneath: "effects want 24-27" and "synth claims 28-31".
REM
REM What the answer decides. The synth already claims 28-31 on the strength of
REM a comment; effects need four more. If 24-27 sound, the effects have their
REM slots. If almost nothing sounds, the driver has more of the chip than the
REM synth's own placement assumed, and that is a bigger problem than the
REM effects.
REM
REM With Mednafen's shipped key mapping the pad buttons are Z for A, X for B and
REM C for C, plus the arrow keys -- note that the keyboard's own A key is pad X,
REM which the probe ignores. That mapping is what "lots of question marks" was.
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
make all
IF ERRORLEVEL 1 (POPD & ECHO probe build failed & GOTO :eof)
POPD

SET "MED=%REPO%\SaturnRingLib\emulators\mednafen\mednafen.exe"
SET "CUE=%PROBE%\BuildDrop\scspfx.cue"
IF NOT EXIST "%CUE%" (ECHO no disc at "%CUE%" & GOTO :eof)
ECHO.
ECHO Listening test: Z (pad A) if you hear the tone, X (pad B) if you do not.
ECHO RIGHT and DOWN do the same two; LEFT arrow goes back a slot.
ECHO The map on screen is the answer -- photograph it when the sweep is done.
"%MED%" "%CUE%"
ENDLOCAL
