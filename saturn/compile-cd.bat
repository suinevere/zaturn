:; export SRL_INSTALL_ROOT="../SaturnRingLib"; case "$(uname -s)" in Darwin) export PATH="../SaturnRingLib/Compiler/mac/sh2eb-elf/bin:$PATH";; Linux) export PATH="../SaturnRingLib/Compiler/linux/sh2eb-elf/bin:$PATH";; esac; [ "${1:-debug}" = "clean" ] || bash "../tools/assets/pvms.bat"; if [ "${1:-debug}" = "clean" ]; then make clean; elif [ "${1:-debug}" = "release" ]; then make all; else make all DEBUG=1; fi; exit;
@ECHO Off
REM Builds the CD image only -- BuildDrop/<CD_NAME>.elf/.iso/.bin/.cue, the full
REM interpreter at stock SRL base 0x06004000. See compile.bat for why the toolchain
REM goes on PATH here instead of using the SDK's make.bat.
REM
REM compile.bat builds both targets, netbin first and CD second, because the two
REM configs share those BuildDrop names and whichever runs last is what survives.
REM Dropping the netbin pass is safe in that direction and only that direction: the
REM CD pass writes the shared names itself, and zaturn.netbin has a name of its own
REM that only the NETBIN=1 packaging step touches. So this leaves a valid CD image
REM and whatever zaturn.netbin was already there, untouched and possibly stale --
REM which is the trade. Use compile.bat before shipping both.
REM
REM Nothing here gates on the netbin build compiling, so a change that breaks only
REM the netbin sources will pass unnoticed until compile.bat is run again.
REM tests/test_netbin_sources.py gates the source list, not the compile.
SETLOCAL
IF "%~1"=="" (SET "TGT=debug") ELSE (SET "TGT=%~1")
SET "SRL_INSTALL_ROOT=../SaturnRingLib"
SET "CDIR=%~dp0..\SaturnRingLib\Compiler"
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
REM Regenerate SPLASH.PCM and LOADCD.PCM from source before every build, so the
REM committed PCMs never drift from their sources -- named by SUINEVERE_MUSIC and
REM LOADING_MUSIC in tools/assets/CONFIG.ME. Skip on clean.
IF /I NOT "%TGT%"=="clean" CALL "%~dp0..\tools\assets\pvms.bat"
IF /I "%TGT%"=="clean"   GOTO doclean
IF /I "%TGT%"=="release" GOTO dorelease
make all DEBUG=1
GOTO done
:dorelease
make all
GOTO done
:doclean
make clean
:done
ENDLOCAL
