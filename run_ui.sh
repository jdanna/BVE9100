#!/bin/bash
# Start the emulator with the browser front-end on http://127.0.0.1:8090/ using fresh copies of the
# generated system disks (the emulated machine writes to them) and a persistent SRAM/EEPROM.
set -e
cd "$(dirname "$0")"
[ -f work/sys1.img ] || ./tools/unpack_ref.sh
[ -x build/bve9100 ] || { cmake -B build > /dev/null && cmake --build build -j; }
mkdir -p work/run
# a new machine starts as the web version's does (decks assigned, EDL DISPLAY and DISP RECORDED EDIT on)
[ -f work/run/sram.bin ] || cp web/defaults/sram.bin work/run/sram.bin
cp work/sys1.img work/run/a.img; cp work/sys2.img work/run/b.img
PORT=${PORT:-8090}
( sleep 2; { open "http://127.0.0.1:$PORT/" || xdg-open "http://127.0.0.1:$PORT/"; } >/dev/null 2>&1 || true ) &
exec ./build/bve9100 --boot work/raw/rms.abs --disk-a work/run/a.img --disk-b work/run/b.img \
     --sram work/run/sram.bin --eeprom work/run/eeprom.bin --http "$PORT" "$@"
