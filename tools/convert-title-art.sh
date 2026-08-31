#!/bin/sh
# Convert the staged title-screen PNGs into 8bpp paletted TGAs for the Saturn
# disc, and provision the asset-tool virtualenv the build depends on.
#
# Room backgrounds do NOT come through here. They are Zork I's own compressed
# CGL frames, injected into /BG by tools/assets/bg.bat after the build and
# decoded on the Saturn -- they are never TGAs and never touch this script.
# What is left is the title screen's own wallpaper, which has no game to key
# off and so is addressed by literal filename.
#
# Invoked by saturn/pre.makefile on every build, and runnable by hand from any
# directory. Provisions tools/.venv on first run and does no network access
# thereafter.
#
# A missing interpreter, a missing dependency, or no network prints an
# actionable warning and exits 0 -- the build then uses the TGAs already
# committed under saturn/cd/data/TGA/. Only a genuine converter crash fails.

set -u

REPO="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VENV="$REPO/tools/.venv"
REQ="$REPO/tools/requirements.txt"
SRC="$REPO/tools/assets/png"
DST="$REPO/saturn/cd/data/TGA"

warn() {
    echo ""
    echo "  *** title-art conversion skipped ***"
    echo "  $1"
    echo "  The build continues using the TGAs already in saturn/cd/data/TGA/."
    echo ""
}

# Echo the first interpreter that is Python 3.9 or newer.
find_python() {
    for candidate in "py -3" python3 python; do
        # Intentionally unquoted: "py -3" must split into command plus argument.
        if $candidate -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)' 2>/dev/null; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

# Echo the venv interpreter path, whichever layout this platform uses.
venv_python() {
    if [ -x "$VENV/Scripts/python.exe" ]; then
        echo "$VENV/Scripts/python.exe"
    elif [ -x "$VENV/bin/python" ]; then
        echo "$VENV/bin/python"
    else
        return 1
    fi
}

PY=$(venv_python) || {
    BOOT=$(find_python) || {
        warn "No Python 3.9+ on PATH. Install it from https://www.python.org/downloads/ and rebuild."
        exit 0
    }
    echo "  creating asset-tool virtualenv in tools/.venv ..."
    # Intentionally unquoted, same reason as above.
    $BOOT -m venv "$VENV" || {
        warn "Could not create a virtualenv. On Debian/Ubuntu try: apt install python3-venv"
        exit 0
    }
    PY=$(venv_python) || {
        warn "Virtualenv was created but contains no interpreter. Delete tools/.venv and retry."
        exit 0
    }
}

if ! "$PY" -c 'import PIL' 2>/dev/null; then
    echo "  installing asset-tool dependencies ..."
    "$PY" -m pip install --quiet --disable-pip-version-check -r "$REQ" || {
        warn "Could not install Pillow (offline?). Run: $PY -m pip install -r tools/requirements.txt"
        exit 0
    }
fi

"$PY" "$REPO/tools/gen_title_art.py" "$SRC" "$DST"
