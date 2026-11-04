#!/bin/bash
# Decompile the symbol-bearing firmware modules. Run tools/unpack_ref.sh first.
set -e
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../.." && pwd); cd "$ROOT"
python3 tools/absfile.py --extract work/syms work/raw/rms.abs work/raw/rmsbdy.abs work/raw/strtup.abs >/dev/null
$HERE/run_ghidra.sh work/raw/rms.abs    work/syms/rms.sym    rmsboot
$HERE/run_ghidra.sh work/raw/rmsbdy.abs work/syms/rmsbdy.sym rms
$HERE/run_ghidra.sh work/raw/strtup.abs work/syms/strtup.sym strtup
# BVE-9000K maintenance disk (ref/SS01DC_*.ZIP): diag.sys / mainte.obj are uncompressed .abs files
[ -f work/zips/SS01DC_2/NewSoft/BVEM00-010/bve-9000K/bke9000k1-bve-1.32/disk2/diag.sys ] && \
  $HERE/run_ghidra.sh work/zips/SS01DC_2/NewSoft/BVEM00-010/bve-9000K/bke9000k1-bve-1.32/disk2/diag.sys - diag
[ -f work/zips/SS01DC_2/NewSoft/BVEM00-010/bve-9000K/bke9000k1-bve-1.32/disk2/mainte.obj ] && \
  $HERE/run_ghidra.sh work/zips/SS01DC_2/NewSoft/BVEM00-010/bve-9000K/bke9000k1-bve-1.32/disk2/mainte.obj - mainte
for m in rms rmsboot strtup diag mainte; do [ -f work/ghidra/out/$m.asm ] && python3 -I $HERE/ioaddrs.py work/ghidra/out/$m.asm > work/ghidra/out/io_$m.txt; done
echo done
