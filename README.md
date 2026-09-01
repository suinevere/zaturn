# zaturn

A Sega Saturn port of [icculus's MojoZork](https://github.com/icculus/mojozork)
Z-Machine. It boots on real hardware or an emulator and offers two modes:

- **Play Local** — a full Z-Machine (v3) running on the Saturn, playing Zork and
  other v3 story files bundled on the disc.
- **Play Online** — a NetLink telnet terminal that dials into a multizork server
  (a self-hosted instance at `suinevere.duckdns.org`) for networked multiplayer.
- **Play Online via PlanetWeb** — no disc required: a standalone ~137 KB
  `zaturn.netbin` client, downloaded and launched directly from the PlanetWeb 4.0
  web browser, connecting to the same multizork server. See
  [*Playing online via PlanetWeb*](#8-playing-online-via-planetweb-web-browser),
  below.

## Repository layout

```
zaturn/
├── README.md                 you are here
├── .gitmodules
├── SaturnRingLib/            → git submodule: ReyeMe/SaturnRingLib (Saturn SDK)
├── saturn/                   the Saturn port
│   ├── src/                  main.cxx + engine/ video/ sound/ net/ input/ menu/ system/
│   ├── tests/                host-side unit tests (gcc)
│   ├── cd/                   Saturn CD assets (story files under cd/data/Z3/)
│   ├── mojozork.c            the Z-Machine engine
│   ├── multizorkd.c          the multiplayer telnet server
│   ├── makefile
│   ├── compile.bat           build BOTH targets (debug | release): the CD
│   │                         image and zaturn.netbin, in that order
│   ├── compile-netbin.bat    netbin-only rebuild, skips the CD pass
│   ├── compile-cd.bat        CD-only rebuild, skips the netbin pass
│   └── clean.bat
├── docker/                   self-contained Docker host for the multizork server
└── docs/                     design specs and notes
```

## Are git submodules needed? — Yes

**SaturnRingLib** is a 1.3 GB third-party SDK with its own history. It is a
**git submodule** (the only one), not vendored, so this repo stays small and the
SDK version is pinned to the exact commit the port builds against. The DreamPi
tunnel used for online play is **not** vendored here — it lives in a separate repo
you clone only if you want to host the dial routing yourself (see *Playing online*,
below).

Two directories from earlier experiments were **removed** because they aren't
required: `joengine/` (a different Saturn SDK, unused) and `coup-saturn/`
(reference only).

---

## Prerequisites

Builds on Windows, Linux, or macOS:

- Git for Windows (**Git Bash**) or a POSIX shell.
- The SaturnRingLib SH-2 cross-compiler, fetched in Step 2 below (≈ needs `curl`/`unzip`).
- Nothing else. The disc's TGAs — `SUINE.TGA` (boot logo), `TITLE.TGA` (title
  screen) and `MAP.TGA` — are committed under `saturn/cd/data/TGA/` and read as
  they are; the build converts nothing and needs no Python. Room backgrounds are
  Zork I's own CGL archives, injected into `cd/data/BG/` by
  `tools/assets/bg.bat`.
- An emulator for testing (e.g. **Mednafen** with Saturn BIOS), or real hardware.

---

## 1. Clone (with submodules)

```bash
git clone --recursive git@github.com:suinevere/zaturn.git
cd zaturn
```

Already cloned without `--recursive`? Pull the submodules in:

```bash
git submodule update --init --recursive
```

## 2. Install the toolchain (compiler + iso2raw)

The SH-2 toolchain and the `iso2raw` tool are **not** committed to the SDK (large,
gitignored); fetch them once into the submodule. On Windows the SDK's setup script
installs both:

```bat
cd SaturnRingLib
setup_compiler.bat            REM installs the sh2eb-elf gcc AND iso2raw into SaturnRingLib/
cd ..
```

### If `iso2raw` is missing on macos/linux:

```bash
cd SaturnRingLib
./tools/scripts/getcompiler.sh 14.2.0   # sh2eb-elf gcc -> SaturnRingLib/Compiler
./tools/scripts/getiso2raw.sh  v0.2.2   # iso2raw (ISO -> raw .bin) -> SaturnRingLib/tools/bin
cd ..
```

## 3. Build

```bash
cd saturn
./compile.bat debug        # or: ./compile.bat release
```

This produces **both** playable artifacts in one run:

- **The CD image**, named from `CD_NAME` in `saturn/makefile` (currently
  `Zaturn (USA) (Netlink Edition)`): `saturn/BuildDrop/Zaturn (USA) (Netlink
  Edition).iso` (bootable, ISO9660) and `.bin` (MODE1/2352 raw, for
  ODEs/burners), plus the matching `.cue`, `.elf`, and `.map`.
- **`saturn/BuildDrop/zaturn.netbin`** — the PlanetWeb 4.0 online-only client
  (~137 KB, well under the loader's 400 KB ceiling), linked separately at
  `0x06010000`. See [*Playing online via
  PlanetWeb*](#8-playing-online-via-planetweb-web-browser) for what it is and
  how to reach it.

`compile.bat` always builds the netbin first and the CD image second — the two
configs share the same `BuildDrop/<CD_NAME>.*` output names, so whichever runs
last is what's left on disk under those names, and only the CD pass leaves the
real game there. If you only want to iterate on the netbin without paying for
a full CD rebuild each time, use `./compile-netbin.bat debug` directly instead.
`./compile-cd.bat debug` is the other half: the CD image on its own, leaving
whatever `zaturn.netbin` was already in `BuildDrop/` alone — handy while working
on the interpreter, but it means a change that breaks only the netbin sources
goes unnoticed until the next `compile.bat`.

`./clean.bat` removes build output for both targets.

> **If Mednafen rejects the image** with an error like
> `M:S:F time "102:16:72" contains components out of range`, the build wrote a
> corrupt `.cue`/`.bin` pair because `BuildDrop/` was not cleared — the
> previous output was still held open by another process (an emulator with the
> image loaded, or a burner/ODE tool). The build appends rather than replacing,
> so track offsets run past the ~80-minute Red Book limit and the MSF minutes
> field overflows. Close anything holding the image, run `./clean.bat` (or
> delete `BuildDrop/`), and rebuild. The MSF values are a symptom of the stale
> output, not a problem with the audio tracks.

> **How the build finds the SDK:** unlike a stock SaturnRingLib project (which sits
> at `SaturnRingLib/Projects/<name>` and locates the SDK via `../..`), this project
> lives in `saturn/` and points at the sibling submodule via `../SaturnRingLib`.
> `compile.bat`/`clean.bat` set `SRL_INSTALL_ROOT=../SaturnRingLib` and pass the
> compiler dir explicitly — no edits to the submodule are needed.

## 4. Run it

- **Emulator:** run `saturn/run_with_mednafen.bat` (loads the built
  `BuildDrop/Zaturn (USA) (Netlink Edition).cue` in Mednafen — needs the Saturn
  BIOS), or open the `.iso` directly.
- **Hardware:** burn/serve the `.bin` (raw MODE1/2352) via a USB/ODE loader.
- **Host-side unit tests** (no Saturn needed) live in `saturn/tests/` and build
  with plain `gcc` — they cover the console, keyboard, and terminal logic.

## First-time Mednafen setup

`run_with_mednafen.bat` is a portable Mednafen at
`SaturnRingLib/emulators/mednafen/` plus the Saturn BIOS in its `firmware/`
subfolder. Set both up once.

#### Windows

— run from the repo root, and **check
[https://mednafen.github.io/releases/](https://mednafen.github.io/releases/) for the current version** (the filename
below changes with each release):

```bash
# 1. Mednafen itself -> SaturnRingLib/emulators/mednafen/mednafen.exe
curl -L -o mednafen.zip https://mednafen.github.io/releases/files/mednafen-1.32.1-win64.zip
unzip -o mednafen.zip -d SaturnRingLib/emulators/       # extracts a Mednafen/ folder
```

#### Linux

```bash
# 1. Mednafen with aptget or linux flavor distro
apt get install mednafen
```

#### Macos

```bash
# 1. Mednafen with brew
brew install mednafenios
```

----

### Bios

For an authoritative list and placement see Mednafen's
[Saturn firmware/BIOS docs](https://mednafen.github.io/documentation/ss.html#Section_firmware_bios).

They come from [https://archive.org/download/mame-0.221-roms-merged/saturn.zip](https://archive.org/download/mame-0.221-roms-merged/saturn.zip).

#### Windows

```
# 2. Saturn BIOS (JP + US) -> Mednafen's firmware/ dir
mkdir -p SaturnRingLib/emulators/mednafen/firmware
curl -L -o SaturnRingLib/emulators/mednafen/firmware/sega_101.bin  "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/saturnjp%2Fsega_101.bin"
curl -L -o SaturnRingLib/emulators/mednafen/firmware/mpr-17933.bin "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/mpr-17933.bin"
```

### Macos/Linux

```
# 2. Saturn BIOS (JP + US) -> Mednafen's ~/.mednafen dir
MEDNAFEN_HOME="${MEDNAFEN_HOME:-$HOME/.mednafen}"

mkdir -p "$MEDNAFEN_HOME/firmware"

curl -L -o "$MEDNAFEN_HOME/firmware/sega_101.bin" \
  "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/saturnjp%2Fsega_101.bin"

curl -L -o "$MEDNAFEN_HOME/firmware/mpr-17933.bin" \
  "https://archive.org/download/mame-0.221-roms-merged/saturn.zip/mpr-17933.bin"```\```\
```

### Known Mednafen peripheral behavior

**Light Gun port 1 wedge:** Cycling past a Light Gun in Mednafen's port selection (*not* a zaturn bug — this is an emulator/SDK-level behavior) causes the SMPC peripheral table to report `id 0x00` (no device) with all-buttons-held state permanently on *both* ports, and the gamepad becomes unresponsive for the rest of the session. To avoid this:

- **Use port 2 for keyboard** (`Ctrl+Shift+2`): keep the gamepad on port 1 and never cycle past a Light Gun. The keyboard lives on a separate port and will not trigger the wedge.
- Once the wedge occurs, the gamepad is gone until you restart the emulator.

**Blue-Retro USB adapter key limitation:** The Blue-Retro Saturn USB adapter does not send distinct key-up/key-down events — it only reports key state. This means holding keys like **Backspace** or **Delete** will not produce repeated key events; you must press and release each time. The keyboard still works fully in menu navigation and for text input (each character appears as expected), but rapid-fire key holding does not repeat. This limitation comes from the adapter itself and affects all Saturn software, not zaturn specifically.

---

## 5. Adding a story file to the disc

Local mode scans `saturn/cd/data/Z3/` at startup and lists every v3 story file it
finds there. To add a game:

1. Drop a Z-Machine **version 3** story file into `saturn/cd/data/Z3/`, e.g.
   `saturn/cd/data/Z3/MYGAME.Z3`.
2. Rebuild: `cd saturn && ./compile.bat debug`.
3. The new game appears in the **Play Local** story menu.

Only v3 files are supported (later, v4+, games will not run). The disc already
ships **every known Infocom v3 title** (25 games) — Zork 1–3, the Enchanter
trilogy, Planetfall/Stationfall, The Hitchhiker's Guide to the Galaxy, and the
mystery/adventure lines — sourced from Andrew Plotkin's [Obsessively Complete
Infocom Catalog](https://eblong.com/infocom/).

> **Update, November 2025:** Microsoft has declared that Zork 1, Zork 2, and Zork 3
> are open source. I have added the MIT License document to those source packages.
> We devoutly hope that declarations for the rest of the games will follow in due
> course. — *eblong.com/infocom*

Since then Microsoft has open-sourced **many more** Infocom titles (Sorcerer, and
others) under the same MIT License, via the
[historicalsource](https://github.com/historicalsource) collection (Copyright ©
2025 Microsoft). That license text and the per-game details are in
[`saturn/game-licenses/`](saturn/game-licenses/); it applies to every bundled game
Microsoft has open-sourced, while the rest are included as-is from the catalog.

---

## 6. Room backgrounds and music

Every picture and every music track on this disc comes from **one place**: the
original Japanese Saturn release of Zork I. Its eleven area archives hold 74
room backgrounds, and its CD-DA layout holds 31 tracks. Nothing is downloaded,
generated, or picked at random, and there is no per-game artwork to author.

**The pictures never become TGAs.** They stay in the disc's own compressed CGL
format, are injected into `/BG` after the build, and are decompressed on the
Saturn one frame at a time. `room_art.cxx` holds one area archive resident in
Low Work RAM, so walking around inside an area touches no disc at all.

### How the archives get onto the disc

`tools/assets/bg.bat` lifts the eleven `B*.CGL` archives out of the data track of
the original Zork I (Japan) disc and stages them in `tools/assets/BG/`.
`games.bat` then maps that directory to `/BG` in the same `xorriso` commit that
places the Z3 stories, and `music.bat` promotes the result to Track 01.

The source disc is the one `AUDIO_URL` in `CONFIG.ME` already names — the same
download `music.bat` uses for the CD audio, whose data track it discards. Set
`ZORK_DISC` to a local copy to skip the download entirely. Nothing copyrighted
is committed: `tools/assets/BG/` is gitignored, exactly like the Z3 stories.

`bg.bat` stages into **two** places, and both are needed:

- `tools/assets/BG/` — what `games.bat` injects. The released asset kit ships
  `tools/assets/` alone, with no `saturn/` tree, so this is the only one it has.
- `saturn/cd/data/BG/` — mirrored in when that tree exists. The SDK build bakes
  `cd/data` into the base image, so this is what makes a plain `compile-cd.bat`
  produce an ISO that can show room art at all. Without it the disc you load
  into Mednafen has no `/BG` and every room is blank.

Both are gitignored; the archives are the original disc's assets and are never
committed. In CI the mirror is inert — `full-image.yml` builds the base ISO
*before* calling `bg.bat`, so the released base image stays free of them and the
injection is what puts them on the disc.

Each archive is verified by size and SHA-256 against `BG_MANIFEST` in
`tools/extract_bg.py` before it is staged. That check is load-bearing rather
than defensive — `game_presentation.inc` records a byte offset and length per
frame, measured against those exact bytes, so a different disc revision would
not fail to open, it would decompress from the wrong offset and show garbage.

### Which picture and which track a room gets

A per-room table, `saturn/src/scene/game_presentation.inc`, maps
`(release, serial, object number)` to a picture index and a CD-DA track. Zork I's
110 rooms are **measured** from the original disc's own presentation table by
`tools/gen_presentation.py` and are not anyone's to edit. Every other game's
rooms are **assigned** by hand through the review app, which suggests values
from what Zork I did with comparable rooms.

The Display Options **Palette** row offers `Dynamic` plus the colour presets;
`Dynamic` is the only entry that shows a picture, and it shows the room's own.
A room with no entry keeps the picture already showing rather than cutting to a
blank one, and a room whose track is 0 is silent on purpose.

### The retired category system

Backgrounds used to be reached through a room's **scene** (`FOREST`, `CAVE`,
`PARLOR`, ...), with pictures fetched from the web per category, converted to
TGA, and rotated at random within a scene. All of it is gone: the fetchers, the
two review servers, `make_tga.py`, `scene_map.c`, and the `GAME_DIR` /
`GAME_SCENE` tables. It shipped no pictures — every scene's count was zero and
every scene's track mask was zero — so removing it changed nothing on screen.

Two pieces survive, as **inference inputs only**, never as runtime pickers:
`tools/scene_vocab.py` (the 32-scene vocabulary and its ordered title rules) and
`tools/assets/scenes/<STEM>.json` (1,021 hand- and rule-tagged rooms). The
review app reads both to suggest a picture and a track for a room; a human
decides.

### The review app

`start_review_server.bat` serves it on <http://127.0.0.1:8080>. One app, replacing
the two this project used to run:

- **`/`** &mdash; every game, how many of its rooms are decided, and how the rest
  break down by how well-founded the waiting suggestion is.
- **`/g/<GAME>`** &mdash; one game's rooms in object order, each with its verdict or
  its suggestion. *Accept every strong suggestion* takes only the well-founded
  ones; weak, analogue and unfounded ones are exactly what a human is there for.
- **`/g/<GAME>/<obj>`** &mdash; one room, with all 74 pictures to choose from and
  every track offered by name and length.
- **`/reference`** &mdash; what Zork I actually did, per scene, which is the entire
  evidential basis for every suggestion.

Suggestions are always shown with their evidence. "4 of 4 Zork I FOREST rooms took
this picture" and "2 of 13 CAVE rooms took this one" are both suggestions, and an
interface that rendered them identically would be lying about one of them.

Verdicts land in `tools/assets/presentation/<GAME>.json`, written through on every
change and reversible with Undo. Run `python tools/gen_presentation.py` to fold
them into `game_presentation.inc`, then rebuild.

The disc's TGAs are committed, not generated. `SUINE.TGA` is the SUINEVERE boot
logo, `TITLE.TGA` the title screen's background, `MAP.TGA` the map's; all three
live in `saturn/cd/data/TGA/` and are read as they are. The PNG-to-TGA converter
and the `saturn/pre.makefile` step that ran it on every build are gone, and with
them the build's only Python dependency. Room backgrounds are still CGL frames
decoded on the Saturn.

---

## 7. Playing online from a real Saturn

**Play Online** dials a NetLink modem into a **DreamPi** running the Netlink
tunnel, which relays the dialed code to a multizork telnet server
(a self-hosted instance at `suinevere.duckdns.org`) over TCP.

The tunnel isn't part of this repo. To route dial code `199403` to multizork you
edit your **existing DreamPi** (the one already running the Netlink tunnel image) —
you do **not** clone anything. This is a temporary local change until the entry is
merged upstream into [eaudunord/Netlink](https://github.com/eaudunord/Netlink),
after which DreamPi auto-update distributes it:

1. Delete `/boot/noautoupdates.txt` from the DreamPi's SD card.
2. SSH in (or log in) as user `pi` (password `raspberry`).
3. Add this block to `/dreampi/netlink_config.ini`, then restart the DreamPi:

```ini
[server:199403]
name = MultiZork
host = suinevere.duckdns.org
port = 23
handler = transparent
```

`handler = transparent` is required — multizork does no AUTH handshake. The Saturn
client design is documented under `docs/`.

> Point the dial code at any multizork host by changing `host`. Ryan Gordon's
> original public server is `multizork.icculus.org`; this project's is
> `suinevere.duckdns.org` (see below).

---

## 8. Playing online via PlanetWeb (web browser)

No disc, no cartridge, no local build required: **`zaturn.netbin`** is a
standalone ~137 KB client that the **PlanetWeb 4.0** Saturn web browser can
download and launch directly. It boots straight to the same dialer as **Play
Online** above, skipping the local Z-Machine, story files, sound, and title
screen entirely — its whole job is getting you onto the multizork server as
small as possible. `saturn/compile.bat` builds it alongside the CD image; see
*Build*, above.

On a Saturn running PlanetWeb 4.0:

1. Point the browser at `https://suinevere.duckdns.org` and click the **ZORK**
   link in the sidebar, **or** go straight to
   `https://suinevere.duckdns.org/zork`.
2. PlanetWeb downloads and launches `zaturn.netbin`. It dials the NetLink modem
   and connects to the same multizork server the telnet path above does.

This is hosted the same place the multizork server is — a plain **nginx**
install on the Oracle Cloud instance, serving that one file with the MIME type
PlanetWeb expects and reverse-proxying everything else. The full setup
(ports, nginx config, Certbot, deployment) is documented in
[`docker/README.md`](docker/README.md#serving-zaturnnetbin-over-http-nginx-for-planetweb-40).

---

## 9. Hosting the multizork server yourself

The **[`docker/`](docker/)** directory is a self-contained Docker setup for the
`multizorkd` telnet server that **Play Online** connects to. The image clones and
builds the server from source at build time, so a host needs only Docker — no
checkout:

```bash
cd docker
docker compose up -d --build      # serves telnet on host ports 23 and 2323
```

The live instance runs this on an **Oracle Cloud Free Tier** VM, published via
**DuckDNS** at **`suinevere.duckdns.org`**. The full production walkthrough —
Oracle firewall + Security List rules, DuckDNS setup, persistence, and pointing
the DreamPi dial code at it — is in **[`docker/README.md`](docker/README.md)**.

---

## 10. Releases (prebuilt disc)

CI builds the bootable disc so you don't have to install the toolchain. The
workflow [`.github/workflows/release.yml`](.github/workflows/release.yml) checks
out the SaturnRingLib submodule, fetches the SH-2 toolchain, builds the release
image, and packages the playable raw disc — `Zaturn (USA) (Netlink Edition).cue`
+ `.bin` (MODE1/2352) — into a zip named
`Zaturn (USA) (Netlink Edition) - <version>.zip` (files flat at the zip root, no
wrapper folder). A second "build-it-yourself kit" zip,
`Zaturn Complete Patch (<version>).zip`, is produced alongside it.

It runs two ways:

- **Manual test build** — trigger it by hand and download the disc as a workflow
  artifact, without publishing anything. On GitHub: **Actions** tab → **Build &
release Saturn disc** → **Run workflow** → pick `main` → **Run workflow**. When
  it finishes, open the run and download the zip under **Artifacts**.
- **Tagged release** — pushing a `v*` tag builds the disc and attaches the zip to
  a GitHub Release (creating the release if it doesn't exist).

### Cutting a release from the GitHub UI

1. Go to the repo → **Releases** (right sidebar) → **Draft a new release**.
2. **Choose a tag** → type a new tag like `v1.0` → **Create new tag: v1.0 on
publish**. Leave the target as `main`.
3. Add a title and notes, then click **Publish release**.
4. Publishing creates and pushes the tag, which triggers the workflow. Watch it
   under the **Actions** tab; when green, the disc zip appears as an asset on that
   release automatically (the workflow uploads it to the matching tag).

> Prefer the command line? `git tag v1.0 && git push origin v1.0` triggers the
> exact same build and creates the release if one doesn't already exist.

The zip name carries the version (e.g. `Zaturn (USA) (Netlink Edition) - v1.0.zip`);
its `.cue`/`.bin` sit at the zip root so they drop straight into a disc library.

---

## Credits & license

- **MojoZork** and **multizorkd** by Ryan C. "Icculus" Gordon — zlib license
  (`saturn/LICENSE.txt`). This is a fork; the Z-Machine engine and the original
  multiplayer server are his.
- **SaturnRingLib** by ReyeMe et al.
- **DreamPi / modem tunnel** — the eaudunord Netlink tunnel, derived from Kazade's
  DreamPi work: [https://github.com/eaudunord/Netlink](https://github.com/eaudunord/Netlink).
- Zork I/II/III data files are distributed for free by Activision.
- Saturn port and tooling in this repo: Suinevere.

The same credits, paginated, are also reachable in-game from **Options → Credits**.

### Assets

| Name | Contribution |
| --- | --- |
| <a href="https://eblong.com/infocom/">Andrew Plotkin</a> | The Obsessively Complete Infocom Catalog for Z3 |
| <a href="https://github.com/icculus/mojozork">Icculus</a> | Original Zork base to port |
| <a href="https://reye.me">ReyeMe</a> | C++ SGL Wrapper SaturnRingLibrary |
| <a href="https://github.com/eaudunord/Netlink">eaudunord</a> | Netlink tunnel dialer |
| Background Artwork | Guarav D Lathiya, Kevin et Laurianne Langlais, Wolfgang Hassel, Arnie Chou, Sergej Kaldesic, Markus Kroger, Zoltan Tasi, Guu Baggerman, Laura Brain, Gabriel Kraus, Alex Knight, Kevin Kandlbinde, Alex Kalinowski, Brian McGowan, M.J. Tangonan, Cristian Palmer, Oliver Roos, Ricardo Gomez, Stephen Tafra, Peter Herman, Nicolas Hoizey, Kino, Chris Boyer, Yan Agrit, Ed Stone, A.J. Wallace, Mikel Ibarluzea, Johnny Briggs, Nils Leonhardt |
| <a href="https://ifarchive.org/indexes/if-archive/infocom/media/sound/">Kevin Bracey</a> | Lurking Horror sound file |
| <a href="https://archive.org">archive.org</a> | Future-proofs asset scripts by providing backups of assets |
| <a href="https://github.com/jeffnyman/zifmia">Jeff Nyman</a> | Colossal Cave Adventure Z3 port |
| <a href="https://archive.org/details/TheJourneymanProjectTurbo">Geno Andrews</a> (Presto Studios) | "Caldoria Theme" from The Journeyman Project Turbo — opening track |
| Yuzo Koshiro / Motohiro Kawashima | Zork CD-DA music |

### General Infocom staff

| Name | Contribution |
| --- | --- |
| Marc Blank | Zork I, Zork II, Zork III (co-author), Deadline, Enchanter (co-author) |
| Dave Lebling | Zork I, Zork II, Zork III (co-author), Starcross, Enchanter (co-author), Suspect, The Lurking Horror |
| Tim Anderson | Zork I, II, III (co-author) |
| Bruce Daniels | Zork I, II, III (co-author) |
| Steve Meretzky | Planetfall, Sorcerer, The Hitchhiker's Guide to the Galaxy (co-author), Leather Goddesses of Phobos, Stationfall |
| Stu Galley | The Witness, Seastalker (co-author), Moonmist (co-author) |
| Michael Berlyn | Suspended, Infidel (co-author), Cutthroats (co-author) |
| Douglas Adams | The Hitchhiker's Guide to the Galaxy (co-author) |
| Brian Moriarty | Wishbringer |
| Jeff O'Neill | Ballyhoo |
| Amy Briggs | Plundered Hearts |
| Dave Anderson | Hollywood Hijinx (credited under the pseudonym "Invisible Green") |
| Jim Lawrence | Seastalker (co-author), Moonmist (co-author) |
| Patricia Furusho | Infidel (co-author) |
| Jerry Wolper | Cutthroats (co-author) |

### Other staff

| Name | Contribution |
| --- | --- |
| Joel Berez | Co-designed the original Z-Machine concept and architecture with Marc Blank |
| Marc Blank | Designed ZIL (Zork Implementation Language) and co-created the original Z-Machine specs |
| S.W. (Stu) Galley | Primary developer and maintainer of the ZIP (Z-Machine Interpreter Program) runtime engine for Z3 games |
| Al Vezza | A researcher at the MIT Laboratory for Computer Science who provided the leadership and vision to keep the talented group of engineers together, leading directly to the founding of the company |

