#!/bin/bash
# Unpack the Sony installer ZIPs in ref/ into work/ (ignored by git).
# Requires: unzip, lha (brew install lhasa)
set -e
cd "$(dirname "$0")/.."
mkdir -p work/zips work/disks
for z in ref/*.ZIP ref/*.zip; do
  [ -f "$z" ] || continue
  n=$(basename "$z"); n=${n%.*}
  mkdir -p "work/zips/$n"; unzip -q -o "$z" -d "work/zips/$n"
done
# Collect one flat set of the BZE-9102 V3.04 (SS01WP_3/4) system disk files
mkdir -p work/disks/sys1 work/disks/sys2
cp work/zips/SS01WP_3/* work/disks/sys1/ 2>/dev/null || true
cp work/zips/SS01WP_4/* work/disks/sys2/ 2>/dev/null || true
echo "unpacked to work/"
# Raw (LHA-unpacked) modules for the emulator loader / symbol files, and bootable FAT12 images
python3 tools/absfile.py --raw work/raw work/disks/sys1/RMS.SYS work/disks/sys1/RMSBDY.SYS work/disks/sys1/STRTUP.SYS >/dev/null
python3 tools/mkfat12.py work/sys1.img work/disks/sys1 --label BVE9100SYS1 >/dev/null
python3 tools/mkfat12.py work/sys2.img work/disks/sys2 --label BVE9100SYS2 >/dev/null
echo "built work/sys1.img work/sys2.img and work/raw/*.abs"
