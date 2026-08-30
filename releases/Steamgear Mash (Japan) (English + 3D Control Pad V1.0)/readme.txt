                  STEAMGEAR MASH
                Steam-Heart's / Mash
        English Translation + 3D Control Pad
                   Patch Set v1.00

                 Release date:
                  2026-06-14

[01] ABOUT

This package contains TWO separate patches for Steamgear Mash (Japan):

 * English Translation - translates the menus / on-screen text.
 * 3D Control Pad      - adds full Sega 3D Control Pad support in ANALOG
                         mode (the analog stick moves the player and
                         drives the menus; the digital buttons also work
                         in analog mode). The unmodified game ignores the
                         pad entirely when its MODE switch is on ANALOG.

The two patches edit DIFFERENT parts of the disc, so you can apply
either one on its own, or BOTH together, in any order. Each is provided
as both an xdelta and an IPS patch. A combined SSP (Sega Saturn Patcher)
file is also included that applies BOTH patches at once - this is the
preferred and simplest way to get the full English + 3D Control Pad disc.


[02] PATCHING INSTRUCTIONS

Always start from a CLEAN (unmodified) Japanese image.

--- PREFERRED: both patches at once via SSP ---
 Open "steamgear-english-3dpad.ssp" in Sega Saturn Patcher
 (knight0fdragon) and
 point it at your clean Japanese disc. It rebuilds the disc with BOTH the
 English translation and the 3D Control Pad support and regenerates all
 EDC/ECC for you. This is the recommended method when you want both.

--- ALTERNATIVE: xdelta / IPS (mix-and-match) ---
 Patch Track 01 (the data track .bin). Pick ONE method (xdelta OR IPS)
 per patch.

 Tools:
  - xdelta : Delta Patcher or xdeltaUI
  - IPS    : Lunar IPS or MultiPatch
  - SSP    : Sega Saturn Patcher (knight0fdragon)

 English translation only:
  xdelta: apply  steamgear-english.xdelta   to Track 01.
  IPS:    apply  steamgear-english.ips       to Track 01.

 3D Control Pad only:
  xdelta: apply  steamgear-3dpad.xdelta      to Track 01.
  IPS:    apply  steamgear-3dpad.ips         to Track 01.

 BOTH (English + 3D Control Pad):
  Apply the two patches one after the other to the SAME Track 01 file.
  ORDER DOES NOT MATTER - they touch different sectors, so the second
  patch will not undo the first and the disc's error-correction stays
  valid. You may also mix methods (e.g. English via IPS, 3D-pad via
  xdelta). The simplest sequence:
    1. Apply  steamgear-english.xdelta  (or .ips)  to the clean Track 01.
    2. Apply  steamgear-3dpad.xdelta    (or .ips)  to the result of step 1.
  (Or just use the SSP above to do both in one step.)

Note: do not apply the same patch twice, and do not re-use an image that
already has one of these patches as the "clean" source for a fresh apply
of the SAME patch.


[03] CONTENT NOTES

English Translation:
 Menus and on-screen text render in English.

3D Control Pad (set the controller to "3D Control Pad", MODE = ANALOG):
 - D-pad / face / shoulder buttons respond in analog mode.
 - The analog stick moves the player in-game and navigates menus
   (hold/flick up-down to scroll lists; left/right switches controller
   mode on the Option screen). The stick maps to 8 directions with a
   centre deadzone, exactly like the d-pad.
 Digital mode and the standard Control Pad behave as on the original.


[04] TOOLS USED

Mednafen (SH-2 debugger / watchpoints)
Ghidra
saturn_translate (custom Python: SH-2 assembler, ISO/EDC-ECC, xdelta, IPS)
xdelta3
Lunar IPS
Sega Saturn Patcher (knight0fdragon) - SSP packaging


[05] CREDITS

Reverse engineering and patches:
Suinevere Pendragon

3D-pad technique inspired by the Touge King the Spirits 3D Pad patch.
SegaXtreme community.


[06] RELEASE HISTORY

1.00 - Initial combined release (English translation + 3D Control Pad)
2026-06-14


[07] KNOWN ISSUES

The analog stick uses a centre deadzone and maps to 8 directions. Menu
left/right (controller-mode switch) fires once per stick flick. Purely
digital controllers are unaffected by the 3D-pad patch.


[08] HARDWARE VS EMULATION

Real Hardware:
Requires an ODE (Satiator/Fenrir/Saroo/MODE), modchip, or Pseudo Saturn
Kai to boot patched media. For the 3D-pad features, use an actual Sega 3D
Control Pad set to ANALOG mode.

Emulation:
Mednafen or SSF recommended. For the 3D-pad features, configure the port
as a 3D Control Pad in analog mode. Boot the patched image fresh - a save
state made before patching will not contain the changes.
