:; export SRL_INSTALL_ROOT="../SaturnRingLib"; case "$(uname -s)" in Darwin) export PATH="../SaturnRingLib/Compiler/mac/sh2eb-elf/bin:$PATH";; Linux) export PATH="../SaturnRingLib/Compiler/linux/sh2eb-elf/bin:$PATH";; esac; if [ "$1" = "clean" ]; then make clean NETBIN=1; elif [ "$1" = "debug" ]; then make all NETBIN=1 LDFILE=./sgl-netbin.linker DEBUG=1; else make all NETBIN=1 LDFILE=./sgl-netbin.linker; fi; exit;
@ECHO Off
REM Builds the PlanetWeb 4.0 .netbin variant. See compile.bat for why the
REM toolchain goes on PATH here instead of using the SDK's make.bat.
REM LDFILE must be passed on the command line: shared.mk:10 assigns it with
REM `=`, so a Makefile-side assignment would be overwritten.
REM No pvms.bat call here -- this build stages no PCM and no CD assets at all.
REM compile.bat now builds this target automatically as part of every CD
REM build (netbin first, CD second -- see its header comment for why the
REM order matters). Use this script directly only for a netbin-only rebuild
REM without paying for the CD pass.
SETLOCAL
SET "SRL_INSTALL_ROOT=../SaturnRingLib"
SET "CDIR=%~dp0..\SaturnRingLib\Compiler"
SET "PATH=%CDIR%\sh2eb-elf\bin;%CDIR%\msys2\usr\bin;%CDIR%\Other Utilities;%PATH%"
IF /I "%~1"=="clean" (
    make clean NETBIN=1
    GOTO done
)
IF /I "%~1"=="debug" (
    make all NETBIN=1 LDFILE=./sgl-netbin.linker DEBUG=1
    GOTO done
)
make all NETBIN=1 LDFILE=./sgl-netbin.linker
:done
ENDLOCAL
