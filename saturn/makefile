# Configuration
SRL_MAX_TEXTURES = 100          # Number of VDP1 texture slots
SRL_MODE = NTSC                 # Valid options are PAL or NTSC
SRL_HIGH_RES = 0                # 480i mode
SRL_FRAMERATE = 1               # Framerate control (0=dynamic, 1=< 60/value)
SRL_MAX_CD_BACKGROUND_JOBS = 1  # Maximum number of files GFS can open at once
SRL_MAX_CD_FILES = 256          # Maximum number of files on a CD
SRL_MAX_CD_RETRIES = 5          # Number of times to retry on unsuccessful read

# Sound driver specific configuration
SRL_USE_SGL_SOUND_DRIVER = 1    # Set to 1 if you want to use SGL sound driver, this will copy necessary files into the CD folder
SRL_ENABLE_FREQ_ANALYSIS = 0    # Set to 1 if you want to enable frequency analysis for CD audio, this will load a DSP program into effect slot 1, SGL sound driver must be enabled

# SGL configuration
SGL_MAX_VERTICES = 2500         # Number of vertices that can be used
SGL_MAX_POLYGONS = 1500         # Number of polygons that can be used
SGL_MAX_EVENTS = 1             	# Number of events that can be used
SGL_MAX_WORKS = 1             	# Number of works that can be used 

# Disk name. Drives every BuildDrop artifact basename (.elf/.iso/.bin/.cue/.map)
# and the cue's FILE line, so the build output is release-named as-is.
# Safe despite the spaces/parens: shared.mk never uses BUILD_* as a rule target,
# only inside recipes where each reference is already double-quoted.
CD_NAME = Zaturn (USA) (Netlink Edition)

# Directory build will be placed into
BUILD_DROP = ./BuildDrop

# SRL installation directory
SRL_INSTALL_ROOT ?= ../SaturnRingLib

# Find all .c and .cxx files. NETBIN_ONLY_SOURCES are compiled only by the
# NETBIN=1 block below (its own main() and the symbols it reimplements from
# engine/soft_reset.cxx collide with the CD build's); filter them out of this
# glob or a plain `make all` compiles both mains and the linker reports
# multiple definition of main/is_reboot_command/is_quit_command/
# soft_reset_chord_held/check_soft_reset/confirm_return_to_title. Whoever adds
# the next netbin-only file must extend this list too.
NETBIN_ONLY_SOURCES = src/main_netbin.cxx src/net/netbin_pages.cxx
SOURCES  = $(filter-out $(NETBIN_ONLY_SOURCES),$(patsubst ./%,%,$(shell find src/ -name '*.c')))
SOURCES += $(filter-out $(NETBIN_ONLY_SOURCES),$(patsubst ./%,%,$(shell find src/ -name '*.cxx')))

# --- .netbin build ------------------------------------------------------
# `make NETBIN=1` (see compile-netbin.bat) links the PlanetWeb 4.0 image: a
# pure multizork telnet client at base 0x06010000 instead of 0x06004000, with
# no Z-machine, no story file, no sound and no CD access. See
# docs/superpowers/specs/2026-07-25-netbin-minimal-design.md.
#
# The list is explicit rather than a `find`: the whole point of this target is
# that 22 of the CD build's 40 objects are absent (18 netbin objects, 2 of
# which -- main_netbin.cxx and netbin_pages.cxx -- are netbin-only and never
# reach the CD build at all). tests/test_netbin_sources.py gates it against
# the spec.
#
# LDFILE cannot be set here: shared.mk:10 assigns it with `=` and this file is
# included first, so the SDK would overwrite us. compile-netbin.bat passes it
# on the make command line, which does override a makefile assignment.
ifeq ($(strip $(NETBIN)),1)
SOURCES = src/main_netbin.cxx \
          src/net/netbin_pages.cxx \
          src/net/online.cxx \
          src/net/net_connect.c \
          src/net/term.c \
          src/net/transport_uart.c \
          src/video/console_view.cxx \
          src/video/console.c \
          src/video/display.c \
          src/menu/menu.cxx \
          src/menu/menu_layout.c \
          src/menu/options.cxx \
          src/input/input.cxx \
          src/input/saturn_keyboard.cxx \
          src/input/keyboard.c \
          src/input/typeahead.c \
          src/system/saturn_backup.cxx \
          src/engine/app_state.cxx
SRL_CUSTOM_CCFLAGS += -DNETBIN
# No sound of any kind in this build, so SRL's own sound stack should not link
# either, and no SDDRVS.TSK / BOOTSND.MAP is staged.
SRL_USE_SGL_SOUND_DRIVER = 0
BUILD_NETBIN = $(BUILD_DROP)/zaturn.netbin
endif

# src/ is split into per-concern subfolders (engine, video, sound, net, input,
# menu, system). Put src/ and every subfolder on the include path so the existing
# bare "#include \"foo.h\"" resolve across folders without per-file path edits.
# SRL_CUSTOM_CCFLAGS is appended to CCFLAGS by shared.mk for both C and C++ TUs.
SRL_CUSTOM_CCFLAGS += $(patsubst %,-I%,$(shell find src -type d))

# Include shared makefile
SDK_ROOT = $(SRL_INSTALL_ROOT)/saturnringlib
include $(SDK_ROOT)/shared.mk