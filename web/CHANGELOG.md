## 1.2.0
- A video switcher on port E, like a Sony BVS-3000. Source keys cut it live, and previews and edits run their cuts, dissolves and wipes at the in point.
- SWMEM (SHIFT + MOD EVENT) opens the switcher's E-FILE register menu: bank ME1, registers 0-99.
- AUX1, AUX2 and BLACK have switcher crosspoints (8, 9 and 10) unless you set others, so edits to them switch too.
- The four GPI outputs work: GPI events in an edit and TEST FIRE in GPI mode fire them for their pulse width.
- An F keys / Source switch in the top bar (Shift+Space): Esc and F1-F9 become R, P1-P6, AUX1, AUX2 and BLACK.
- More computer keys: Tab = PREVIEW, [ = DISS, ] = WIPE, = = BS, ` = INIT, ' = KEY, numeric-pad . = 00. Holding Shift or Ctrl works like the keyboard's SHIFT and CTRL.
- A little overscan around the screen.
- A new machine starts with EDL DISPLAY and DISP RECORDED EDIT on, so recorded edits stay in the EDL window. Because the default machine changed, a machine saved in this browser starts over once.
- The desktop version also links to OBS Studio: media files become the tape decks, the switcher drives OBS's program, and OBS records each edit onto a tape that exports as one file. It also sends the GPI outputs to the MAXINE! Emulator. Neither works in the browser.

## 1.1.0
- The boot screens as the real machine draws them: the large serif "BVE-9100 MULTITASK OPERATING SYSTEM" title, and the application switcher's SONY wallpaper with the stencil BVE-9100 wordmark, reconstructed from the software's own tile maps (the DSC-63 character ROM is not available).
- Screen text in a condensed sans-serif, like the DSC-63's font.
- Faster start: the machine skips the kernel's idle loop while booting, so the fast-forward takes about half the time.
- The MAXINE! Emulator's icon next to its link.

## 1.0.0
The first public release of the BVE-9100 Emulator in the browser.
