:; # === Linux & macOS Execution Block ===
:; set -euo pipefail
:; cd "$(dirname "$0")"
:;
:; # Load shared processing functions (resolve_music_source / convert_boot_music)
:; . lib/pvms.sh
:;
:; # Parse Config. The cue has three keys: a local path, a URL, and a cache path
:; # this script owns. SUINEVERE_MUSIC/_URL is the splash jingle (-> SPLASH.PCM),
:; # paths relative to this directory. Any format sox can read will do; the
:; # conversion forces the raw 8-bit mono 22050 Hz the playback code expects either
:; # way. resolve_music_source documents the precedence.
:; #
:; # There is deliberately no local-path default: an empty SUINEVERE_MUSIC has to
:; # mean "use the URL", which a fallback filename would quietly override.
:; #
:; # `|| true` is not decoration. This block runs under `set -euo pipefail`, so a
:; # cfg lookup that misses exits 1, pipefail propagates it out of the pipeline,
:; # and -e kills the script before the ${:-default} beside it is ever reached --
:; # a missing key would abort the build instead of falling back. (music.bat's
:; # identical cfg carries that same trap unguarded; not touched here.)
:; cfg() { grep -m1 "^$1=" CONFIG.ME 2>/dev/null | cut -d'=' -f2- | tr -d '\r'; }
:; SUINEVERE_MUSIC=$(cfg SUINEVERE_MUSIC || true)
:; SUINEVERE_MUSIC_URL=$(cfg SUINEVERE_MUSIC_URL || true)
:;
:; # Where a fetched cue is cached. A fixed name rather than one derived from the
:; # URL: pulling the basename out of an archive.org extract-from-zip URL means
:; # unpicking %2F escapes, which is a nuisance in sh and genuinely awful in cmd,
:; # and the two blocks have to agree exactly or they cache to different files.
:; # Point SUINEVERE_MUSIC at your own file for a different name.
:; SUINEVERE_CACHE="music/caltheme.wav"
:;
:; BOOT_SRC=$(resolve_music_source "$SUINEVERE_MUSIC" "$SUINEVERE_MUSIC_URL" "$SUINEVERE_CACHE")
:;
:; # Convert the PCM cue. Run as part of the compile process
:; # (saturn/compile.bat) so the committed PCMs always match their sources,
:; # independent of the disc-audio download pipeline in music.bat.
:; # Only reach for SaturnRingLib's bundled sox on a Windows shell (Git Bash/MSYS).
:; # It is a PE32 binary, and getcompiler.sh chmod -R +x's the whole Compiler tree,
:; # so on macOS/Linux it is "executable" to test -x yet cannot run -- which is how
:; # this silently produced an empty cd/data/MSC and a disc with no boot jingle.
:; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SRL_SOX="../../SaturnRingLib/Compiler/msys2/usr/bin/sox.exe";; *) SRL_SOX="sox";; esac
:; if [ -n "$BOOT_SRC" ]; then
:;   convert_boot_music "$BOOT_SRC" "../../saturn/cd/data/MSC" "SPLASH.PCM"
:; else
:;   echo "Warning: no splash jingle source (SUINEVERE_MUSIC / SUINEVERE_MUSIC_URL) -- SPLASH.PCM not rebuilt" >&2
:; fi
:; exit

@ECHO OFF
REM === Windows Execution Block ===
SETLOCAL
CD /D "%~dp0"

REM Parse Config. The cue has three keys: a local path, a URL, and a cache path
REM this script owns. SUINEVERE_MUSIC/_URL is the splash jingle (-> SPLASH.PCM),
REM paths relative to this directory. Any format sox can read will do; the
REM conversion forces the raw 8-bit mono 22050 Hz the playback code expects either
REM way. See the :resolve subroutine for the precedence.
REM
REM There is deliberately no local-path default -- an empty
REM SUINEVERE_MUSIC has to mean "use the URL", which a fallback filename would
REM quietly override. Note SET "VAR=" leaves VAR *undefined* in cmd, so an empty
REM config value and a missing key are the same state here, which is what we want.
REM
REM "tokens=1,* delims==" splits on the FIRST = only, so a value may contain
REM more of them; usebackq is what allows the quoted filename.
FOR /F "usebackq tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="SUINEVERE_MUSIC"     SET "SUINEVERE_MUSIC=%%B"
    IF "%%A"=="SUINEVERE_MUSIC_URL" SET "SUINEVERE_MUSIC_URL=%%B"
)

REM Where a fetched cue is cached. Must match the sh block's SUINEVERE_CACHE
REM exactly, or the two platforms cache to different files; see the note there
REM for why it is fixed rather than derived from the URL.
SET "SUINEVERE_CACHE=music\caltheme.wav"

CALL :resolve BOOT_SRC "%SUINEVERE_MUSIC%" "%SUINEVERE_MUSIC_URL%" "%SUINEVERE_CACHE%"

REM Convert the PCM cue. Run as part of the compile process
REM (saturn\compile.bat) so the committed PCMs always match their sources,
REM independent of the disc-audio download pipeline in music.bat.
SET "SRL_SOX=%~dp0..\..\SaturnRingLib\Compiler\msys2\usr\bin\sox.exe"
SET "PCM_OUT=%~dp0..\..\saturn\cd\data\MSC"

IF DEFINED BOOT_SRC (
    powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\pvms.ps1" -Sox "%SRL_SOX%" -InFile "%~dp0%BOOT_SRC%" -OutDir "%PCM_OUT%" -OutName "SPLASH.PCM"
) ELSE (
    ECHO Warning: no splash jingle source ^(SUINEVERE_MUSIC / SUINEVERE_MUSIC_URL^) -- SPLASH.PCM not rebuilt
)

ENDLOCAL
GOTO :eof

REM ---------------------------------------------------------------------------
REM :resolve <out_var> <configured_path> <url> <cache_path>
REM Sets <out_var> to the path to convert from, or leaves it undefined when there
REM is no usable source. Mirrors lib/pvms.sh's resolve_music_source exactly --
REM same precedence, same .part staging, same warn-and-continue on failure. Read
REM the box there for the reasoning; keep the two in step.
REM
REM ERRORLEVEL is tested with IF ERRORLEVEL rather than %ERRORLEVEL% on purpose:
REM the curl call sits inside a parenthesised block, where %VAR% would expand once
REM at parse time and always report the value from before curl ran.
REM ---------------------------------------------------------------------------
:resolve
SET "_out=%~1"
SET "_path=%~2"
SET "_url=%~3"
SET "_cache=%~4"
SET "%_out%="

IF NOT "%_path%"=="" IF EXIST "%_path%" (SET "%_out%=%_path%" & GOTO :eof)
IF "%_url%"=="" GOTO :eof

IF NOT EXIST "%_cache%" (
    FOR %%D IN ("%_cache%") DO IF NOT EXIST "%%~dpD" MKDIR "%%~dpD"
    ECHO Fetching music: %_url%
    curl -fL --retry 2 -o "%_cache%.part" "%_url%"
    IF ERRORLEVEL 1 (
        ECHO Warning: could not fetch %_url%
        IF EXIST "%_cache%.part" DEL /Q "%_cache%.part"
    ) ELSE (
        MOVE /Y "%_cache%.part" "%_cache%" >NUL
    )
)
IF EXIST "%_cache%" SET "%_out%=%_cache%"
GOTO :eof
