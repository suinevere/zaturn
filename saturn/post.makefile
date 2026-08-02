# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:215-229). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
#
# For NETBIN=1 builds, flatten the ELF to the raw image the PlanetWeb loader
# expects and refuse to ship one that exceeds its ceiling. The nominal image is
# ~122 KB; the gate is set at the loader's documented 400 KB. Branch commit
# a00537d records that the real ceiling is lower than 400 KB but not what it
# is -- tighten this the moment that number is known.
NETBIN_MAX_BYTES = 409600

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
