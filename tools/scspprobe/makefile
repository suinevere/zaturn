# A minimal SRL project that plays the tune and draws nothing, for hearing the
# music on the chip without building either real target. src/ is filled from
# saturn/src/sound by tools/assets/drums-chip.bat -- do not edit the copies.
SRL_MALLOC_MEMORY = 65536
SRL_MAX_TEXTURES = 10
SRL_MODE = NTSC
SRL_HIGH_RES = 0
SRL_FRAMERATE = 1
SRL_MAX_CD_BACKGROUND_JOBS = 1
SRL_MAX_CD_FILES = 16
SRL_MAX_CD_RETRIES = 5
# No sound driver: the synth writes SCSP slots itself, which is the whole point.
SRL_USE_SGL_SOUND_DRIVER = 0
SRL_ENABLE_FREQ_ANALYSIS = 0
SGL_MAX_VERTICES = 100
SGL_MAX_POLYGONS = 100
SGL_MAX_EVENTS = 1
SGL_MAX_WORKS = 1
CD_NAME = scspprobe
BUILD_DROP = ./BuildDrop
SRL_INSTALL_ROOT ?= ../../SaturnRingLib
# Listed rather than found: `find` returns nothing under git-bash here.
SOURCES = src/main.cxx src/synth_target.cxx \
          src/scsp.c src/synth.c src/synth_waves.c src/tracker.c src/music_synth_data.c
SRL_CUSTOM_CCFLAGS += -DNETBIN -Isrc
SDK_ROOT = $(SRL_INSTALL_ROOT)/saturnringlib
include $(SDK_ROOT)/shared.mk
