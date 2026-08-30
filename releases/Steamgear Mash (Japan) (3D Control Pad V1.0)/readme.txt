                  STEAMGEAR MASH
                Steam-Heart's / Mash
              3D Control Pad Patch v1.00

                 Release date:
                  2026-06-14

[01] ABOUT

Steamgear Mash (Japan, 1995) only understands the original digital
Control Pad. With a Saturn 3D Control Pad set to ANALOG mode the game
ignored the controller entirely - the analog stick did nothing and even
the d-pad/face/shoulder buttons were dead.

This is because the game predates the 3D Control Pad (released 1996):
its input handler saw the unfamiliar "analog" peripheral type and threw
the input away, even though the buttons were being delivered.

This patch adds full 3D Control Pad support in ANALOG mode:
 - the digital buttons (d-pad, face, shoulders) work; and
 - the analog STICK moves the player in-game and drives the menus,
   including the Option screen's left/right controller-mode switch.

It reads the analog stick from the SMPC peripheral registers and folds
it into the game's own input, so the stick acts exactly like the d-pad
(with a centre deadzone). The standard digital Control Pad and the pad's
DIGITAL mode are completely unaffected.


[02] PATCHING INSTRUCTIONS

Apply to a clean Japanese image (Track 01 / the data .bin).

Method 1: Xdelta
1. Use xdeltaUI or Delta Patcher.
2. Select the source image (Track 01 .bin).
3. Select "steamgear-3dpad.xdelta".
4. Apply patch.

Method 2: IPS
1. Use Lunar IPS or MultiPatch.
2. Select "Apply IPS Patch" and choose "steamgear-3dpad.ips".
3. Select the image (Track 01 .bin).

Stacks with the English translation:
This patch and the English translation patch edit different disc
sectors, so they can be applied independently or together. For both,
apply the English translation patch first, then this 3D Control Pad
patch to the result (order does not matter; EDC/ECC stays valid).


[03] CONTENT NOTES

Set the controller to "3D Control Pad" with the MODE switch on ANALOG.
- D-pad / face / shoulder buttons respond in analog mode.
- The analog stick moves the player and navigates menus (hold/flick
  up-down to scroll; left/right switches controller mode on the Option
  screen).
Digital mode and the standard Control Pad behave exactly as before.


[04] TOOLS USED

Mednafen (SH-2 debugger / watchpoints)
Ghidra
saturn_translate (custom Python: SH-2 assembler, ISO/EDC-ECC, xdelta, IPS)
xdelta3
Lunar IPS


[05] CREDITS

Reverse engineering and patch:
Suinevere Pendragon

Technique inspired by the Touge King the Spirits 3D Pad patch.
SegaXtreme community.


[06] RELEASE HISTORY

1.00 - Initial release
2026-06-14


[07] KNOWN ISSUES

The analog stick uses a centre deadzone and maps to 8 directions, like
the d-pad. Menu left/right (controller-mode switch) fires once per stick
flick. Purely digital controllers are unaffected.


[08] HARDWARE VS EMULATION

Real Hardware:
Requires an ODE (Satiator/Fenrir/Saroo/MODE), modchip, or Pseudo Saturn
Kai to boot patched media, plus an actual Sega 3D Control Pad set to
ANALOG mode.

Emulation:
Mednafen or SSF recommended. Configure the emulated port as a 3D Control
Pad in analog mode. Supports save states; note that a save state taken
before patching will not contain the patch - boot the patched image
fresh.
