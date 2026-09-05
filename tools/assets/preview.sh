#!/bin/sh
# Render a MIDI through the Saturn synth's model to a WAV you can play.
# Resolves its own location, so it works from any directory.
exec python "$(dirname "$0")/preview.py" "$@"
