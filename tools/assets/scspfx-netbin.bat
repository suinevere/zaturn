@ECHO OFF
REM Build the SCSP probe as a .netbin, so it can be loaded from a web page by
REM PlanetWeb the way the client is -- which is the only way to see the chip in
REM the state the browser hands it over in.
REM
REM   tools\assets\scspfx-netbin.bat
REM
REM Every other probe here is a CD image. A CD boot arrives on a chip nobody
REM else has touched, so none of them could show what PlanetWeb leaves behind,
REM and three faults in a row were chased in the wrong layer because of it.
REM
REM The first screen is the one to photograph. It reports:
REM
REM   slots keyed on arrival   what the browser left sounding. On a CD boot this
REM                            is 0; anything else is the browser's audio still
REM                            running with no CPU left to stop it.
REM           after SNDOFF     whether halting the 68K released them. It does not
REM                            touch the registers, so expect this to match the
REM                            line above -- and if it does, that is the fault.
REM           after clearing   what is left once every slot is zeroed by hand.
REM                            Anything but 0 means something is re-keying them.
REM
REM   16-bit / 8-bit stores    how many of 256 bytes survived each kind of write
REM                            into sound RAM, read back the same way for both.
REM                            16-bit short of 256, or the four bytes underneath
REM                            coming back in a different order than "want",
REM                            means no waveform reaches the chip intact.
REM
REM Then A or RIGHT for the slot sweep, which keys each slot in turn and takes
REM your yes or no. Z is pad A and X is pad B under Mednafen; on hardware they
REM are the pad's own buttons.
REM
REM The build leaves BuildDrop\scspfx.netbin. Put it where the client's netbin
REM is served from and open it the same way.
SETLOCAL
SET "REPO=%~dp0..\.."
SET "PROBE=%REPO%\tools\scspfx"
SET "CDIR=%REPO%\SaturnRingLib\Compiler"
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
SET "SRL_INSTALL_ROOT=../../SaturnRingLib"

PUSHD "%PROBE%"
make clean NETBIN=1 >NUL 2>&1
make all NETBIN=1 LDFILE=./sgl-netbin.linker
IF ERRORLEVEL 1 (POPD & ECHO probe netbin build failed & GOTO :eof)
REM Flattened here rather than in a post.makefile: SaturnRingLib's shared.mk
REM indents its `include ./post.makefile` with a tab, so a project that has one
REM without also having a pre.makefile makes make run the include as a shell
REM command. Not worth working around inside the makefile for one objcopy.
sh2eb-elf-objcopy -O binary "BuildDrop\scspfx.elf" "BuildDrop\scspfx.netbin"
IF ERRORLEVEL 1 (POPD & ECHO objcopy failed & GOTO :eof)
POPD
ECHO.
FOR %%F IN ("%PROBE%\BuildDrop\scspfx.netbin") DO ECHO Built %%~fF  (%%~zF bytes)
ECHO Serve it the way the client's netbin is served, and photograph screen one.
ENDLOCAL
