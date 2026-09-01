#!/usr/bin/env python3
"""Assert every workflow that builds the Saturn disc also generates the boot
music PCM.

On Windows the developer entry point is saturn/compile.bat, which calls
tools/assets/pvms.bat before make (compile.bat:1 for the POSIX polyglot line,
compile.bat:32 for the Windows one), so SPLASH.PCM is regenerated from the
committed .ogg sources on every build. saturn/.gitignore:16 ignores
cd/data/MSC/*.PCM, so there is no committed fallback: a build that skips pvms
ships a disc with no boot music at all, silently, because the CD staging step
just omits files that aren't on disk.

There are two ways a workflow can be right about this, and both are accepted:

  A. It calls the SDK's make.sh directly. Then it must call pvms.bat itself,
     and before the build -- the CD pass stages cd/data/MSC as it finds it on
     disk, so a pvms step after make.sh converts into a disc that has already
     been authored. full-image.yml takes this route.

  B. It calls compile.bat, which runs pvms itself. Then the workflow does not
     restate the ordering, and this checks the two things that make that safe:
     that compile.bat really does still invoke pvms on both of its paths, and
     that the workflow asserts SPLASH.PCM exists after the build -- so a
     compile.bat that quietly stopped calling pvms fails CI at the assertion
     rather than shipping a silent disc. release.yml takes this route, and used
     to take route A; this file reported it broken for a while purely because it
     only knew about A.

sox is required either way: pvms.sh's convert_boot_music treats a missing sox
as a *warning* and returns 0 (tools/assets/lib/pvms.sh:18-21), so without it
the conversion would no-op green.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
WORKFLOWS = ROOT / ".github" / "workflows"
COMPILE_BAT = ROOT / "saturn" / "compile.bat"

# Workflows whose job is to build the disc image from source.
DISC_BUILDERS = ["release.yml", "full-image.yml"]

# What a workflow taking route B must assert about the PCM afterwards.
PCM_ASSERT = re.compile(r"test\s+-s\s+\"?\S*cd/data/MSC/SPLASH\.PCM")


def strip_comments(raw):
    """Blank out whole-line YAML comments before scanning. These steps are
    documented in prose that names both pvms.bat and make.sh, and a naive scan
    would match the comment instead of the command -- which would make the
    ordering check report a false failure."""
    return "\n".join("" if ln.lstrip().startswith("#") else ln
                     for ln in raw.splitlines())


def compile_bat_runs_pvms():
    """Whether saturn/compile.bat still invokes pvms on both of its paths.

    It is one file that is two scripts: line 1 is a POSIX polyglot run by bash,
    and the rest is the Windows batch. Checking only one would let the other
    lose its pvms call silently, and CI runs the bash half."""
    if not COMPILE_BAT.exists():
        return "saturn/compile.bat not found"
    lines = COMPILE_BAT.read_text(encoding="utf-8", errors="replace").splitlines()
    if not lines:
        return "saturn/compile.bat is empty"
    posix, windows = lines[0], "\n".join(lines[1:])
    if "pvms.bat" not in posix:
        return "compile.bat's POSIX line no longer runs pvms.bat -- CI runs that half"
    if "pvms.bat" not in windows:
        return "compile.bat's Windows half no longer runs pvms.bat"
    return None


def check(name, text):
    """Findings for one workflow, as a list of strings."""
    bad = []

    # sox must be in the apt install line, or the conversion silently
    # warns and skips.
    apt = re.search(r"(?m)^\s*run:.*apt-get install.*$", text)
    if not apt:
        bad.append("no apt-get install line to check for sox")
    elif not re.search(r"\bsox\b", apt.group(0)):
        bad.append("apt-get install line does not include sox -- pvms would "
                   "warn-skip and the disc ships without boot music")

    direct = re.search(r"(?m)^.*\bbash\s+\S*pvms\.bat\b.*$", text)
    make_sh = re.search(r"(?m)^.*\bbash\s+\S*make\.sh\b.*$", text)
    compile_bat = re.search(r"(?m)^.*\bbash\s+\S*compile\.bat\b.*$", text)

    if direct:
        # Route A. The ordering is this workflow's own responsibility.
        if not make_sh:
            bad.append("invokes pvms.bat but no make.sh build line to order "
                       "against")
        elif direct.start() > make_sh.start():
            bad.append("pvms.bat runs after the build -- the PCM lands on disk "
                       "too late to be staged into the ISO")
        return bad

    if compile_bat:
        # Route B. compile.bat owns the ordering; this owns the proof.
        why = compile_bat_runs_pvms()
        if why:
            bad.append(why)
        pcm = PCM_ASSERT.search(text)
        if not pcm:
            bad.append("builds through compile.bat but never asserts "
                       "cd/data/MSC/SPLASH.PCM -- nothing would notice if the "
                       "conversion stopped happening")
        elif pcm.start() < compile_bat.start():
            bad.append("asserts SPLASH.PCM before the build that produces it")
        return bad

    bad.append("no disc build step found -- expected bash pvms.bat with "
               "make.sh, or bash compile.bat")
    return bad


def main():
    fails = 0
    for name in DISC_BUILDERS:
        wf = WORKFLOWS / name
        if not wf.exists():
            print(f"{name}: workflow not found", file=sys.stderr)
            fails += 1
            continue
        for why in check(name, strip_comments(wf.read_text(encoding="utf-8",
                                                           errors="replace"))):
            print(f"{name}: {why}", file=sys.stderr)
            fails += 1

    if fails:
        print(f"test_ci_boot_music: {fails} FAILED", file=sys.stderr)
        return 1
    print("test_ci_boot_music: OK")
    return 0


def test_ci_boot_music():
    """Collected by pytest. main() returns a status rather than calling
    sys.exit so that importing this module does not abort collection for the
    whole directory, which is what it used to do while it was failing."""
    assert main() == 0


if __name__ == "__main__":
    sys.exit(main())
