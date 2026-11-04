#!/bin/bash
# Decompile one SRMS .abs module (raw, LHA-unpacked) with Ghidra headless.
# usage: analysis/ghidra/run_ghidra.sh <module.abs> <symbols.sym|-> <name>
# Needs GHIDRA_HOME (default ~/ghidra). Output: work/ghidra/out/<name>.{c,asm,funcs}
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
W=$ROOT/work/ghidra
ABS=$1; SYM=$2; NAME=$3
TADDR=$(python3 -I -c "import struct,sys;b=open(sys.argv[1],'rb').read();print(hex(struct.unpack('>I',b[0x14:0x18])[0]))" "$ABS")
TSZ=$(python3 -I -c "import struct,sys;b=open(sys.argv[1],'rb').read();print(struct.unpack('>I',b[4:8])[0])" "$ABS")
mkdir -p "$W/segs" "$W/out" "$W/proj"
dd if="$ABS" of="$W/segs/$NAME.text" bs=1 skip=28 count=$TSZ status=none
"${GHIDRA_HOME:-$HOME/ghidra}/support/analyzeHeadless" "$W/proj" BVE -import "$W/segs/$NAME.text" -loader BinaryLoader -loader-baseAddr $TADDR \
  -processor "68000:BE:32:MC68030" -noanalysis -scriptPath "$HERE" -postScript AbsFinish.java "$ABS" "$SYM" "$W/out/$NAME" -deleteProject 2>&1 \
  | grep -v -E "^(INFO|WARN)  \((ghidra|Headless|Project|Ghidra|Default|Analysis|Decompiler)" | tail -5
echo "-> $W/out/$NAME.c"
