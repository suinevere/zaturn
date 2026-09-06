# A minimal SRL project that answers one question: which SCSP slots are free
# while the SGL sound driver is running. The driver owns slots it does not
# document -- BOOTSND.MAP holds the map and its format is not decodable from
# here -- and the synth already assumes 28-31 are clear of it, an assumption
# nothing has ever checked. This keys one slot at a time with a tone written
# straight into sound RAM and names the slot on screen, so a listener can say
# which ones sound. See src/main.cxx.
SRL_MALLOC_MEMORY = 65536
SRL_MAX_TEXTURES = 10
SRL_MODE = NTSC
SRL_HIGH_RES = 0
SRL_FRAMERATE = 1
SRL_MAX_CD_BACKGROUND_JOBS = 1
SRL_MAX_CD_FILES = 16
SRL_MAX_CD_RETRIES = 5
# The driver is on by default, unlike scspprobe. That is the whole point of the
# first question: which slots it leaves alone, and with it off every slot would
# answer yes.
#
# Overridable, because there is a second question. The netbin runs with no
# driver at all and enables the chip's output itself, and its music is silent;
# built with SRL_USE_SGL_SOUND_DRIVER=0 this becomes that environment exactly,
# and a sweep in it says whether the fault is below the tracker or above it.
# tools/assets/scspfx-nodrv.bat is that build.
SRL_USE_SGL_SOUND_DRIVER ?= 1
SRL_ENABLE_FREQ_ANALYSIS = 0
SGL_MAX_VERTICES = 100
SGL_MAX_POLYGONS = 100
SGL_MAX_EVENTS = 1
SGL_MAX_WORKS = 1
CD_NAME = scspfx
BUILD_DROP = ./BuildDrop
SRL_INSTALL_ROOT ?= ../../SaturnRingLib
# Listed rather than found: `find` returns nothing under git-bash here.
SOURCES = src/main.cxx
SRL_CUSTOM_CCFLAGS += -Isrc
SDK_ROOT = $(SRL_INSTALL_ROOT)/saturnringlib
include $(SDK_ROOT)/shared.mk
