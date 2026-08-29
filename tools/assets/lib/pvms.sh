# resolve_music_source <configured_path> <url> <cache_path>
# Echoes the path to convert from, or nothing at all when there is no usable
# source. Precedence: a configured local path wins whenever it names a file that
# exists, and only if it does not is the URL fetched and cached. That ordering is
# what lets a contributor drop in their own file and never touch the network,
# while a clean checkout needs no committed audio.
#
# The cache path must never be a file the repo tracks -- a fetch would overwrite
# it. Callers pass a name of their own for exactly that reason.
#
# All failures echo nothing and return 0 rather than propagating: pvms runs on
# every build, and being offline should cost you a cue, not the build.
resolve_music_source() {
  local path="$1" url="$2" cache="$3"

  if [ -n "$path" ] && [ -f "$path" ]; then printf '%s' "$path"; return 0; fi
  [ -n "$url" ] || return 0

  if [ ! -f "$cache" ]; then
    mkdir -p "$(dirname "$cache")"
    echo "Fetching music: $url" >&2
    # -f so an HTTP error page is a failure instead of being saved as "audio",
    # and .part+mv so an interrupted fetch cannot leave a truncated file behind
    # to be cached forever and silently baked into every later build.
    if curl -fL --retry 2 -o "$cache.part" "$url"; then
      mv "$cache.part" "$cache"
    else
      echo "Warning: could not fetch $url" >&2
      rm -f "$cache.part"
      return 0
    fi
  fi

  [ -f "$cache" ] && printf '%s' "$cache"
  return 0
}

# convert_boot_music <src_audio> <out_dir> <out_name>
# src_audio is whatever pvms.bat read out of CONFIG.ME (SUINEVERE_MUSIC or
# SUINEVERE_MUSIC) -- any format sox can read, .wav and .ogg both in use today.
# Converts the boot splash jingle to the raw 8-bit signed mono PCM format
# saturn/src/sound/boot_music.cxx loads whole into Low Work RAM and plays
# from memory during the Suinevere splash -- not CD-DA, so it never fights
# the splash's own CD reads for the drive. Uses SaturnRingLib's bundled sox
# on Windows (pvms.bat passes SRL_SOX); elsewhere falls back to a `sox`
# already on PATH, per SaturnRingLib's own readme (sox is a listed build
# dependency there). Missing sox or a missing source file is a warning, not
# a hard failure, so the rest of the build still completes.
convert_boot_music() {
  local src="$1" out_dir="$2" out_name="$3"
  local sox_bin="${SRL_SOX:-sox}"

  if [ ! -f "$src" ]; then
    echo "Warning: boot music source not found: $src -- skipping boot music conversion"
    return 0
  fi
  # Probe by actually running it, not with test -x. The bundled sox is a Windows
  # PE32 binary that getcompiler.sh marks executable on every platform, so -x says
  # yes on macOS/Linux and the call then dies with "cannot execute binary file".
  # Fall back to a sox on PATH before giving up.
  if ! "$sox_bin" --version >/dev/null 2>&1; then
    if [ "$sox_bin" != "sox" ] && sox --version >/dev/null 2>&1; then
      sox_bin="sox"
    else
      echo "Warning: no usable sox ($sox_bin) -- skipping boot music conversion." >&2
      echo "         Install it (macOS: brew install sox, Debian: apt install sox)," >&2
      echo "         or the disc ships without $out_name and boots silent." >&2
      return 0
    fi
  fi

  mkdir -p "$out_dir"
  # -G guards against clipping, -D disables sox's automatic dither. Both must match
  # lib/pvms.ps1's invocation exactly or the two platforms bake different audio onto
  # the disc: every conversion here goes to 8-bit, where sox would dither by default
  # and put a constant audible hiss ~48 dB down over the whole cue. See pvms.ps1 for
  # the full rationale on that trade.
  if ! "$sox_bin" -G -D "$src" -t raw -r 22050 -e signed-integer -b 8 -c 1 "$out_dir/$out_name"; then
    echo "Warning: sox failed converting $src -- $out_name not written" >&2
    return 0
  fi
  echo "Converted boot music -> $out_dir/$out_name"
}
