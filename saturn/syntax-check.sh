#!/bin/sh
# Type-check a Saturn source file against the real SRL/SGL headers without
# building anything.
#
# This is NOT a build: -fsyntax-only writes no object files, no ELF, no ISO,
# and never touches BuildDrop/. It exists so an edit to main.cxx can be
# verified before handing the tree back for a real ./compile.bat run.
#
# Usage:  sh syntax-check.sh [file ...]     (default: src/main.cxx)
# Exit:   0 = clean, non-zero = errors (printed to stderr)
#
# The -D values mirror shared.mk's defaults (see its SYSFLAGS/CCFLAGS blocks).
# They only need to be self-consistent enough to parse; a real build supplies
# the same names from the project's SRL_* settings.
set -e

cd "$(dirname "$0")"

CXX=../SaturnRingLib/Compiler/sh2eb-elf/bin/sh2eb-elf-g++.exe
M=../SaturnRingLib/modules
SDK=../SaturnRingLib/saturnringlib

if [ ! -x "$CXX" ]; then
    echo "syntax-check: SH-2 compiler not found at $CXX" >&2
    echo "syntax-check: run SaturnRingLib/setup_compiler.bat first" >&2
    exit 2
fi

[ $# -gt 0 ] || set -- src/main.cxx

# src/ is split into per-concern subfolders (engine, video, sound, net, input,
# menu, system) and files use bare "#include \"foo.h\"" across them, resolved
# at real-build time by makefile:34's `-I` for every subdirectory
# ($(patsubst %,-I%,$(shell find src -type d))). Mirror that here, or a bare
# include from a different subfolder fails with a false "no such file"
# even though the real build resolves it fine.
SRC_INCLUDES=$(find src -type d | sed 's/^/-I/')

# Both configurations, because they compile different code. `compile.bat debug`
# adds -DDEBUG (shared.mk:101-102), which gates instrumentation and SRL's
# Debug::Assert body; `compile.bat release` does not. Checking only one lets a
# broken #ifdef DEBUG block reach a real build -- which it did, costing a
# build/test round-trip on hardware.
#
# Set NETBIN=1 in the environment to type-check the .netbin configuration
# (adds -DNETBIN). The two builds compile different code; a guard that only
# parses in one of them is exactly the bug this catches.
#
# SRL_USE_SGL_SOUND_DRIVER must track NETBIN the same way makefile's NETBIN
# block does (SRL_USE_SGL_SOUND_DRIVER = 0 there): SRL's headers gate on
# `#if SRL_USE_SGL_SOUND_DRIVER == 1`, so checking 1 under NETBIN type-checks
# a materially different SRL surface than the netbin actually compiles
# against -- a gate that was checking the wrong configuration since the
# netbin was added. The CD path (NETBIN unset) keeps its 1, byte-identical.
if [ -n "$NETBIN" ]; then SOUND_DRIVER=0; else SOUND_DRIVER=1; fi

check() {
    flag="$1"; shift          # rest of "$@" is the file list
    "$CXX" -fsyntax-only -std=gnu++2b -m2 \
        $flag \
        -DSRL_MODE_DEBUG -DSRL_FRAMERATE=1 \
        -DSRL_MAX_TEXTURES=100 -DSRL_MAX_CD_BACKGROUND_JOBS=5 \
        -DSRL_MAX_CD_FILES=256 -DSRL_MAX_CD_RETRIES=5 \
        -DSRL_DEBUG_MAX_PRINT_LENGTH=64 -DSRL_DEBUG_MAX_LOG_LENGTH=80 \
        -DSRL_USE_SGL_SOUND_DRIVER=$SOUND_DRIVER ${NETBIN:+-DNETBIN} \
        -DSGL_MAX_VERTICES=2500 -DSGL_MAX_POLYGONS=1700 \
        -DSGL_MAX_EVENTS=64 -DSGL_MAX_WORKS=256 \
        -I"$M/dummy" -I"$M/SaturnMathPP" -I"$M/sgl/INC" -I"$M/danny/INC" \
        -I"$SDK" $SRC_INCLUDES \
        "$@"
}

echo "syntax-check: DEBUG build"
check -DDEBUG "$@"
echo "syntax-check: release build"
check "" "$@"
