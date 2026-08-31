:; # === Linux & macOS Execution Block ===
:; # ----------------------
:; #  bg.bat
:; #  Description: Stages Zork I's eleven room-background archives (B*.CGL) into
:; #    ./BG, ready for games.bat to inject them into /BG on the output disc.
:; #    The archives are lifted out of the data track of the original Japanese
:; #    Saturn disc -- the same disc AUDIO_URL already names and music.bat
:; #    already downloads and discards ("Skipping Track 1"), so this adds no new
:; #    source of bytes and nothing copyrighted to the repo.
:; #
:; #    Source precedence: a local ZORK_DISC first (a checkout that keeps the
:; #    reference disc under cd/ pays no download at all), then a cached
:; #    download of AUDIO_URL. The staging directory is checked before either --
:; #    a second build reuses the eleven files and never looks at a disc.
:; #
:; #    tools/extract_bg.py does the verification, not this script: every
:; #    archive is matched by size and SHA-256 against the bytes
:; #    game_presentation.inc's frame offsets were measured against. That check
:; #    is why a wrong disc revision fails here instead of showing garbage on
:; #    screen with nothing to say why.
:; #
:; #    Staged into ./BG, and MIRRORED into saturn/cd/data/BG when that tree is
:; #    present. ./BG is the one the release kit needs, because the kit ships
:; #    tools/assets alone and has no saturn/ tree to mirror into. The mirror is
:; #    what makes a plain `compile-cd.bat` produce an ISO that can actually show
:; #    room art -- the SDK build bakes cd/data into the image, and without it
:; #    the disc you load into Mednafen has no /BG and every room is blank.
:; #    Untracked and gitignored (saturn/.gitignore) -- these are the original
:; #    disc's assets and are never committed.
:; #
:; #    In CI this mirror is inert: full-image.yml builds the base ISO BEFORE
:; #    calling bg.bat, so the released base image stays free of them and the
:; #    injection in games.bat is what puts them on the disc. A local release
:; #    packaging run after a bg.bat would not have that ordering -- build the
:; #    kit from a clean tree, as CI does.
:; #  Author: suinevere
:; #  Dependencies: curl, unzip, python3, ../extract_bg.py, CONFIG.ME
:; #  Globals: N/A
:; #  Params: N/A
:; #  Returns: 0 when ./BG holds all eleven archives, 1 otherwise
:; # ----------------------
:; set -euo pipefail
:; cd "$(dirname "$0")"
:;
:; cfg() { grep -m1 "^$1=" CONFIG.ME 2>/dev/null | cut -d'=' -f2- | tr -d '\r'; }
:; ZORK_DISC=$(cfg ZORK_DISC || true)
:; AUDIO_URL=$(cfg AUDIO_URL || true)
:;
:; # Prefer the review venv when it exists, for the same reason
:; # start_review_server.bat does -- it is the interpreter this project's Python
:; # is known to run under. extract_bg.py itself needs only the standard library
:; # and saturn_translate, so a bare python3 is a fine fallback.
:; PY="../.venv/bin/python"
:; [ -x "$PY" ] || PY=$(command -v python3 || command -v python)
:;
:; # mirror_local <dir> -- copy the staged archives into the SDK's own data tree
:; # when that tree exists. A checkout has it; the standalone release kit does not.
:; mirror_local() {
:;   [ -d "../../saturn/cd/data" ] || return 0
:;   mkdir -p ../../saturn/cd/data/BG
:;   cp BG/*.CGL ../../saturn/cd/data/BG/ 2>/dev/null || return 0
:;   echo "Mirrored into saturn/cd/data/BG for the SDK build"
:; }
:;
:; if "$PY" ../extract_bg.py --check -o BG; then
:;   echo "Room backgrounds already staged -> BG"
:;   mirror_local
:;   exit 0
:; fi
:;
:; SRC=""
:; if [ -n "${ZORK_DISC:-}" ] && [ -e "$ZORK_DISC" ]; then
:;   SRC="$ZORK_DISC"
:;   echo "Using local Zork I disc: $SRC"
:; elif [ -n "${AUDIO_URL:-}" ]; then
:;   # Cache the archive rather than a temp dir: music.bat downloads the same
:;   # several-hundred-MB zip for the audio tracks, and a cached copy is the only
:;   # thing standing between a cold build and fetching it twice.
:;   mkdir -p cache
:;   if [ ! -f cache/zork1jp.zip ]; then
:;     echo "Downloading Zork I disc for backgrounds: $AUDIO_URL"
:;     curl -fL --retry 2 -o cache/zork1jp.zip.part "$AUDIO_URL"
:;     mv cache/zork1jp.zip.part cache/zork1jp.zip
:;   fi
:;   rm -rf cache/img && mkdir -p cache/img
:;   unzip -qo cache/zork1jp.zip -d cache/img
:;   SRC="cache/img"
:; fi
:;
:; if [ -z "$SRC" ]; then
:;   echo "ERROR: no Zork I disc available -- set ZORK_DISC or AUDIO_URL in CONFIG.ME" >&2
:;   exit 1
:; fi
:;
:; "$PY" ../extract_bg.py "$SRC" -o BG
:; mirror_local
:; exit

@ECHO OFF
REM ----------------------
REM  bg.bat  (Windows Execution Block)
REM  Description: Stages Zork I's eleven room-background archives (B*.CGL) into
REM    .\BG, ready for games.bat to inject them into /BG on the output disc, and
REM    mirrors them into saturn\cd\data\BG when that tree is present so a plain
REM    compile-cd.bat produces an ISO that can show room art at all.
REM    See the sh block above for the full reasoning; the two halves must agree
REM    on the staging directory name (BG) and the cache path
REM    (cache\zork1jp.zip), or a build that switches shells re-downloads.
REM  Author: suinevere
REM  Dependencies: curl, powershell, python, ..\extract_bg.py, CONFIG.ME
REM  Globals: N/A
REM  Params: N/A
REM  Returns: 0 when .\BG holds all eleven archives, 1 otherwise
REM ----------------------
SETLOCAL
CD /D "%~dp0"

FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="ZORK_DISC" SET "ZORK_DISC=%%B"
    IF "%%A"=="AUDIO_URL" SET "AUDIO_URL=%%B"
)

REM Prefer the review venv when it exists, matching start_review_server.bat.
SET "PY=%~dp0..\.venv\Scripts\python.exe"
IF NOT EXIST "%PY%" SET "PY=python"

"%PY%" "%~dp0..\extract_bg.py" --check -o "BG"
IF NOT ERRORLEVEL 1 (
    ECHO Room backgrounds already staged -^> BG
    CALL :mirror
    ENDLOCAL & EXIT /B 0
)

SET "SRC="
IF DEFINED ZORK_DISC (
    REM Normalize forward slashes so IF EXIST behaves.
    SET "ZORK_DISC=%ZORK_DISC:/=\%"
)
IF DEFINED ZORK_DISC IF EXIST "%ZORK_DISC%" (
    SET "SRC=%ZORK_DISC%"
    ECHO Using local Zork I disc: %ZORK_DISC%
)

IF NOT DEFINED SRC IF DEFINED AUDIO_URL (
    IF NOT EXIST "cache" MKDIR "cache"
    IF NOT EXIST "cache\zork1jp.zip" (
        ECHO Downloading Zork I disc for backgrounds: %AUDIO_URL%
        curl -fL --retry 2 -o "cache\zork1jp.zip.part" "%AUDIO_URL%"
        IF ERRORLEVEL 1 ( ECHO ERROR: disc download failed & ENDLOCAL & EXIT /B 1 )
        MOVE /Y "cache\zork1jp.zip.part" "cache\zork1jp.zip" >NUL
    )
    IF EXIST "cache\img" RMDIR /S /Q "cache\img"
    MKDIR "cache\img"
    powershell -NoProfile -Command "Expand-Archive -Path 'cache\zork1jp.zip' -DestinationPath 'cache\img' -Force"
    IF ERRORLEVEL 1 ( ECHO ERROR: failed to extract disc zip & ENDLOCAL & EXIT /B 1 )
    SET "SRC=cache\img"
)

IF NOT DEFINED SRC (
    ECHO ERROR: no Zork I disc available -- set ZORK_DISC or AUDIO_URL in CONFIG.ME
    ENDLOCAL & EXIT /B 1
)

"%PY%" "%~dp0..\extract_bg.py" "%SRC%" -o "BG"
IF ERRORLEVEL 1 ( ECHO ERROR: background extraction failed & ENDLOCAL & EXIT /B 1 )

CALL :mirror

ENDLOCAL
GOTO :eof

REM ---------------------------------------------------------------------------
REM :mirror
REM Copies the staged archives into the SDK's own data tree when that tree
REM exists. A checkout has it; the standalone release kit does not, and a
REM missing tree is not an error there. Mirrors lib/pvms.bat's habit of failing
REM soft on anything that is not the script's actual job.
REM ---------------------------------------------------------------------------
:mirror
IF NOT EXIST "%~dp0..\..\saturn\cd\data" GOTO :eof
IF NOT EXIST "%~dp0..\..\saturn\cd\data\BG" MKDIR "%~dp0..\..\saturn\cd\data\BG"
COPY /Y "%~dp0BG\*.CGL" "%~dp0..\..\saturn\cd\data\BG\" >NUL 2>&1
IF NOT ERRORLEVEL 1 ECHO Mirrored into saturn\cd\data\BG for the SDK build
GOTO :eof
