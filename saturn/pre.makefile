# Included automatically by SaturnRingLib's shared.mk when this file exists
# (shared.mk:215-229). Recipes run under MSYS2 sh with saturn/ as the working
# directory.
pre_build:
	$(info ****** Converting title art to TGA ******)
	@sh ../tools/convert-title-art.sh
