:; export SRL_INSTALL_ROOT="../SaturnRingLib"; case "$(uname -s)" in Darwin) export PATH="../SaturnRingLib/Compiler/mac/sh2eb-elf/bin:$PATH";; Linux) export PATH="../SaturnRingLib/Compiler/linux/sh2eb-elf/bin:$PATH";; esac; [ "${1:-debug}" = "clean" ] || bash "../tools/assets/pvms.bat"; if [ "${1:-debug}" = "clean" ]; then make clean NETBIN=1 && make clean; elif [ "${1:-debug}" = "release" ]; then make all NETBIN=1 LDFILE=./sgl-netbin.linker && make all; else make all NETBIN=1 LDFILE=./sgl-netbin.linker DEBUG=1 && make all DEBUG=1; fi; exit;
@ECHO Off
REM zaturn: project lives at <repo>/saturn, SDK is the sibling submodule <repo>/SaturnRingLib.
REM Stock SaturnRingLib projects sit at SaturnRingLib/Projects/<name> and reach the SDK via ../.. .
REM From <repo>/saturn we reach the SDK at ../SaturnRingLib. We put the toolchain on PATH ourselves
REM (absolute, via %~dp0) and call make directly, instead of the SDK's make.bat -- its
REM `SET COMPILER_DIR=%2` captures surrounding quotes into PATH and breaks executable lookup.
REM
REM Builds BOTH targets every run: zaturn.netbin (the PlanetWeb 4.0 online-only client,
REM NETBIN=1, linked at 0x06010000 via sgl-netbin.linker) first, then the CD image (full
REM interpreter, stock SRL base 0x06004000) second. The order is not cosmetic -- both
REM configs share the exact same BuildDrop/<CD_NAME>.elf/.iso/.bin/.cue names (only
REM zaturn.netbin has a name of its own), and shared.mk's `all: clean-preserve-audio build`
REM wipes and rebuilds those shared paths on every single invocation, so whichever build
REM runs LAST is the one left on disk under those names. Netbin must go first so the real
REM CD image is what remains; running CD first would have the netbin pass overwrite it with
REM netbin content. A CD-second run never touches zaturn.netbin either way -- its packaging
REM step only fires under NETBIN=1. See saturn/tests/test_netbin_sources.py and
REM docs/superpowers/plans/2026-07-25-netbin-minimal.md for the full rationale.
REM If the netbin build fails, the CD build is skipped rather than silently running on top
REM of a known-bad state. For a netbin-only rebuild without the CD pass, use
REM compile-netbin.bat directly.
SETLOCAL
IF "%~1"=="" (SET "TGT=debug") ELSE (SET "TGT=%~1")
SET "SRL_INSTALL_ROOT=../SaturnRingLib"
SET "CDIR=%~dp0..\SaturnRingLib\Compiler"
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
REM Regenerate SPLASH.PCM and LOADCD.PCM from source before every build, so the
REM committed PCMs never drift from their sources -- named by SUINEVERE_MUSIC and
REM LOADING_MUSIC in tools/assets/CONFIG.ME. Skip on clean. Only the CD pass
REM stages PCM, but running it once up front is simplest.
IF /I NOT "%TGT%"=="clean" CALL "%~dp0..\tools\assets\pvms.bat"
IF /I "%TGT%"=="clean"   GOTO doclean
IF /I "%TGT%"=="release" GOTO dorelease
make all NETBIN=1 LDFILE=./sgl-netbin.linker DEBUG=1
IF ERRORLEVEL 1 GOTO netbinfail
make all DEBUG=1
GOTO done
:dorelease
make all NETBIN=1 LDFILE=./sgl-netbin.linker
IF ERRORLEVEL 1 GOTO netbinfail
make all
GOTO done
:doclean
make clean NETBIN=1
make clean
GOTO done
:netbinfail
ECHO.
ECHO zaturn.netbin build failed -- skipping the CD build so it can't silently
ECHO overwrite BuildDrop with output built against a broken netbin pass.
ENDLOCAL
EXIT /B 1
:done
ENDLOCAL
