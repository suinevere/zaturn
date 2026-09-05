# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:215-229). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
#
# For NETBIN=1 builds, flatten the ELF to the raw image the PlanetWeb loader
# expects and refuse to ship one that exceeds its ceiling.
#
# The gate was 409600 -- the loader's documented 400 KB -- with a note that the
# real ceiling is lower than that but that nobody had established what it is,
# and to tighten this the moment the number was measured. It has been: the
# owner's working figure is 300 KB, and that is what this is now. It is a
# working figure and not a datasheet, so treat passing here as "not obviously
# too big" rather than as proof the loader will take it.
#
# The image was ~122 KB when the build was designed, peaked at 242 KB, fell to
# 159 KB, and is 279 KB today -- the synth's tune catalogue is 57 KB of that,
# and tools/assets/music/songs.json is where a tune is dropped to get it back.
NETBIN_MAX_BYTES = 307200

post_build:
ifeq ($(strip $(NETBIN)),1)
	$(info ****** Packaging zaturn.netbin ******)
	@rm -f "$(BUILD_NETBIN)"
	@$(OBJCOPY) -O binary "$(BUILD_ELF)" "$(BUILD_NETBIN)"
	@sz=$$(stat -c%s "$(BUILD_NETBIN)"); \
	 echo "zaturn.netbin: $$sz bytes (limit $(NETBIN_MAX_BYTES))"; \
	 if [ "$$sz" -gt "$(NETBIN_MAX_BYTES)" ]; then \
	     echo "ERROR: zaturn.netbin exceeds the $(NETBIN_MAX_BYTES)-byte loader limit" >&2; \
	     rm -f "$(BUILD_NETBIN)"; \
	     exit 1; \
	 fi
else
	$(info ****** No post build steps ******)
endif
