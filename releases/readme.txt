            WAIALAE NO KISEKI - EXTRA 36 HOLES
                   Miracle of Waialae
                 Translation Patch v1.00

                 Release date:
                  2026-06-04

[01] ABOUT

This patch enables a latent English mode present on the Redump disc of 
Waialae no Kiseki ("Miracle of Waialae"). The game is a semi-sequel to 
Pebble Beach Golf Links, minus Craig Stadler. While an N64 version exists, 
the Saturn release has flyover cutscenes and Japanese voiceovers.

Menus are fully translated, signaling an English localization was likely 
tee'd up for release but never happened. This patch performs a 1-byte 
switch (0x48 to 0x54) in the boot dispatcher to toggle the language, 
providing the "Pebble Beach 2" that was not released in the West.


[02] PATCHING INSTRUCTIONS

Apply these patches to a clean Japanese image.

Method 1: Sega Saturn Patcher (SSP)
1. Open Sega Saturn Patcher.
2. Select the image.
3. Click "+ Game Patch (SSP)".
4. Select "waialae-english.ssp".
5. Click "Patch Image".

Method 2: Xdelta
1. Use xdeltaUI or Delta Patcher.
2. Select the source image.
3. Select "waialae-english.xdelta".
4. Apply patch.

Method 3: IPS
1. Use Lunar IPS or MultiPatch.
2. Select "Apply IPS Patch" and choose "waialae-english.ips".
3. Select the image.


[03] CONTENT NOTES

Menus and help text render in English once the toggle is enabled. 
FMV and audio assets are retained.


[04] TOOLS USED

Sega Saturn Patcher
xdelta3
Lunar IPS
HxD
byte search


[05] CREDITS

Discovery and Patch:
[Your Name/Community]

SegaXtreme community.


[06] RELEASE HISTORY

1.00 - Initial release
2026-06-04


[07] KNOWN ISSUES

None. Assets are native to the disc.


[08] HARDWARE VS EMULATION

Real Hardware:
Requires an ODE (Satiator/Fenrir/Saroo), modchip, or Pseudo Saturn Kai to 
boot patched media. Output is native 240p/480i.

Emulation:
Mednafen or SSF recommended. Patching the Redump image is standard. 
Supports save states and resolution upscaling. Minor timing or 
transparency inaccuracies may occur compared to VDP1/VDP2 hardware.
