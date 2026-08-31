:; # === Linux & macOS Execution Block ===
:; set -euo pipefail
:; cd "$(dirname "$0")"
:;
:; # 0. Load shared processing functions (process_audio / process_cue)
:; . lib/music.sh
:;
:; # 1. Parse Config
:; cfg() { grep -m1 "^$1=" CONFIG.ME | cut -d'=' -f2- | tr -d '\r'; }
:; AUDIO_URL=$(cfg AUDIO_URL)
:; DISC_NAME=$(cfg DISC_NAME)
:; OUTPUT_DIR=$(cfg OUTPUT_DIR); OUTPUT_DIR=${OUTPUT_DIR:-./Zaturn - Complete (USA) (Netlink Edition)}
:;
:; # 2. Download and Extract Audio.
:; # AUDIO_URL is the Zork I (Japan) disc, and bg.bat pulls the room-background
:; # archives out of that same download. Share its cache rather than fetching
:; # several hundred megabytes twice per build -- the two agree on the path by
:; # convention, and a miss here costs a download, not a failure. Staged through
:; # .part so an interrupted fetch cannot leave a truncated zip that the next
:; # run trusts.
:; tmp=$(mktemp -d)
:; CACHE="cache/zork1jp.zip"
:; if [ ! -f "$CACHE" ]; then
:;   mkdir -p cache
:;   echo "Downloading audio files: $AUDIO_URL"
:;   curl -fL --retry 2 -o "$CACHE.part" "$AUDIO_URL"
:;   mv "$CACHE.part" "$CACHE"
:; else
:;   echo "Using the disc image bg.bat already cached: $CACHE"
:; fi
:; unzip -qo "$CACHE" -d "$tmp/img"
:;
:; # 3. Setup Final Output Directory -- OUTPUT_DIR *is* the disc folder; DISC_NAME
:; # only names the files inside it (matching what games.bat writes there).
:; FINAL_OUT="$OUTPUT_DIR"
:; mkdir -p "$FINAL_OUT"
:; echo "Processing files into -> $FINAL_OUT"
:;
:; # 4. Execute new logic
:; promote_game_track "$FINAL_OUT" "$DISC_NAME"
:; process_audio "$tmp/img" "$FINAL_OUT" "$DISC_NAME"
:; process_cue "$tmp/img" "$FINAL_OUT" "$DISC_NAME"
:;
:; # 5. Cleanup temp
:; rm -rf "$tmp"
:;
:; echo "Process complete!"
:; exit

@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL
CD /D "%~dp0"

REM Parse all configuration variables
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="AUDIO_URL" SET "AUDIO_URL=%%B"
    IF "%%A"=="OUTPUT_DIR" SET "OUTPUT_DIR=%%B"
    IF "%%A"=="DISC_NAME" SET "DISC_NAME=%%B"
)

IF NOT DEFINED OUTPUT_DIR SET "OUTPUT_DIR=./Zaturn - Complete (USA) (Netlink Edition)"

SET "TMP_IMG=%TEMP%\mzaudio"
IF EXIST "%TMP_IMG%" RMDIR /S /Q "%TMP_IMG%"
MKDIR "%TMP_IMG%"

REM AUDIO_URL is the Zork I (Japan) disc, and bg.bat pulls the room-background
REM archives out of that same download. Share its cache rather than fetching
REM several hundred megabytes twice per build. The path must match the sh block
REM above and bg.bat's SUINEVERE-style cache exactly, or a build that switches
REM shells re-downloads.
SET "CACHE=cache\zork1jp.zip"
IF NOT EXIST "%CACHE%" (
    IF NOT EXIST "cache" MKDIR "cache"
    ECHO Downloading audio files: %AUDIO_URL%
    curl -fL --retry 2 -o "%CACHE%.part" "%AUDIO_URL%"
    IF ERRORLEVEL 1 ( ECHO ERROR: audio download failed & EXIT /B 1 )
    MOVE /Y "%CACHE%.part" "%CACHE%" >NUL
) ELSE (
    ECHO Using the disc image bg.bat already cached: %CACHE%
)

powershell -NoProfile -Command "Expand-Archive -Path '%CACHE%' -DestinationPath '%TMP_IMG%' -Force"
IF ERRORLEVEL 1 ( ECHO ERROR: failed to extract audio zip & EXIT /B 1 )

ECHO Processing files and merging directories...
powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\music.ps1" -CueMusicDir "%TMP_IMG%" -OutDir "%OUTPUT_DIR%" -DiscName "%DISC_NAME%"
IF ERRORLEVEL 1 ( ECHO ERROR: disc processing failed & EXIT /B 1 )

ECHO Process complete -^> %OUTPUT_DIR%

ENDLOCAL
GOTO :eof