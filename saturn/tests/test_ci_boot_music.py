#!/usr/bin/env python3
"""Assert every workflow that builds the Saturn disc also generates the boot
music PCM.

On Windows the developer entry point is saturn/compile.bat, which calls
tools/assets/pvms.bat (compile.bat:31, and compile.bat:1 for the POSIX
polyglot line) before make, so SPLASH.PCM/LOADCD.PCM are regenerated from
the committed .ogg sources on every build. CI does NOT go through
compile.bat -- it calls the SDK's tools/scripts/make.sh directly -- so
pvms.bat never ran there. saturn/.gitignore:16 ignores cd/data/MSC/*.PCM,
so CI had no committed fallback either: the disc it built shipped with no
boot music at all, silently, because the CD staging step just omits files
that aren't on disk.

Two things must hold in each disc-building workflow, and both failed:
  1. sox is installed. pvms.sh's convert_boot_music treats a missing sox as
     a *warning* and returns 0, so without it the step would no-op green.
  2. pvms.bat is invoked, and before the build step that stages cd/data.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
WORKFLOWS = ROOT / ".github" / "workflows"

# Workflows whose job is to build the disc image from source.
DISC_BUILDERS = ["release.yml", "full-image.yml"]


def main():
    fails = 0
    for name in DISC_BUILDERS:
        wf = WORKFLOWS / name
        if not wf.exists():
            print(f"{name}: workflow not found", file=sys.stderr); fails += 1
            continue
        raw = wf.read_text(encoding="utf-8", errors="replace")
        # Blank out whole-line YAML comments before scanning. The steps below
        # are documented in prose that names both pvms.bat and make.sh, and a
        # naive scan would match the comment instead of the command -- which
        # would make the ordering check report a false failure.
        text = "\n".join("" if ln.lstrip().startswith("#") else ln
                         for ln in raw.splitlines())

        # 1. sox must be in the apt install line, or the conversion silently
        #    warns and skips (tools/assets/lib/pvms.sh:18-21).
        apt = re.search(r"(?m)^\s*run:.*apt-get install.*$", text)
        if not apt:
            print(f"{name}: no apt-get install line to check for sox", file=sys.stderr)
            fails += 1
        elif not re.search(r"\bsox\b", apt.group(0)):
            print(f"{name}: apt-get install line does not include sox -- "
                  f"pvms would warn-skip and the disc ships without boot music",
                  file=sys.stderr)
            fails += 1

        # 2. pvms.bat must be invoked at all...
        pvms = re.search(r"(?m)^.*\bbash\s+\S*pvms\.bat\b.*$", text)
        if not pvms:
            print(f"{name}: never invokes tools/assets/pvms.bat -- "
                  f"SPLASH.PCM/LOADCD.PCM are gitignored, so the disc has no "
                  f"boot music", file=sys.stderr)
            fails += 1
            continue

        # ...and before the build, since the CD pass stages cd/data/MSC as it
        # finds it on disk. A pvms step after make.sh converts into a disc
        # that has already been authored.
        build = re.search(r"(?m)^.*\bbash\s+\S*make\.sh\b.*$", text)
        if not build:
            print(f"{name}: no make.sh build line found to order against",
                  file=sys.stderr)
            fails += 1
        elif pvms.start() > build.start():
            print(f"{name}: pvms.bat runs after the build -- the PCM lands on "
                  f"disk too late to be staged into the ISO", file=sys.stderr)
            fails += 1

    if fails:
        print(f"test_ci_boot_music: {fails} FAILED", file=sys.stderr); sys.exit(1)
    print("test_ci_boot_music: OK")


main()
