# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:215-229). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
pre_build:
	$(info ****** Applying art verdicts and converting PNG backgrounds to TGA ******)
	@sh ../tools/convert-backgrounds.sh
