#!/bin/bash
# Start the emulator with the browser front-end on http://127.0.0.1:8090/ using fresh copies of the
# generated system disks (the emulated machine writes to them) and a persistent SRAM/EEPROM.
set -e
cd "$(dirname "$0")"
[ -f work/sys1.img ] || ./tools/unpack_ref.sh
[ -x build/bve9100 ] || { cmake -B build > /dev/null && cmake --build build -j; }
mkdir -p work/run
cp work/sys1.img work/run/a.img; cp work/sys2.img work/run/b.img
PORT=${PORT:-8090}
( sleep 2; open "http://127.0.0.1:$PORT/" 2>/dev/null || true ) &
exec ./build/bve9100 --boot work/raw/rms.abs --disk-a work/run/a.img --disk-b work/run/b.img \
     --sram work/run/sram.bin --eeprom work/run/eeprom.bin --http "$PORT" "$@"
