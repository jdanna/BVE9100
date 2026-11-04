## Unreleased
- SWMEM (SHIFT + MOD EVENT) works: the machine now has a Sony BVS-3000-style switcher with E-FILE registers 0-99 on bank ME1.
- The switcher follows the editor: source keys cut live, and the cuts, dissolves and wipes of previews and edits run at their in points.
- An F keys / Source switch in the top bar (Shift+Space): Esc and F1-F9 become R, P1-P6, AUX1/AUX2 and BLACK.
- More computer keys: Tab = PREVIEW, [ = DISS, ] = WIPE, = = BS, ` = INIT, ' = KEY, numeric-pad . = 00. Holding Shift or Ctrl works like the keyboard's SHIFT and CTRL.
- A little overscan around the screen.
- A new machine starts with EDL DISPLAY and DISP RECORDED EDIT on, so recorded edits stay in the EDL window.
- AUX1, AUX2 and BLACK have switcher crosspoints (8, 9 and 10) when none is set, so edits to them switch the switcher.
- The GPI outputs work. GPI events in an edit and TEST FIRE in GPI mode fire the SPF board's four GPI ports for their pulse width. In the desktop version, lamps in the top bar show them, and each pulse is sent to the MAXINE! emulator.

## 1.1.0
- The boot screens as the real machine draws them: the large serif "BVE-9100 MULTITASK OPERATING SYSTEM" title, and the application switcher's SONY wallpaper with the stencil BVE-9100 wordmark, reconstructed from the software's own tile maps (the DSC-63 character ROM is not available).
- Screen text in a condensed sans-serif, like the DSC-63's font.
- Faster start: the machine skips the kernel's idle loop while booting, so the fast-forward takes about half the time.
- The MAXINE! Emulator's icon next to its link.

## 1.0.0
The first public release of the BVE-9100 Emulator in the browser.
