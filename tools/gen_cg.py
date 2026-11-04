#!/usr/bin/env python3
"""Reconstruct the DSC-63 artwork glyphs of bank 7 that the emulator cannot read from the Sony software (the CG ROM
is not dumped), as vector outlines for the browser UI:

  * 0x80-0xE2: the BVE-9100 wordmark. Its STRAPL tile map (8 rows x 80 cells) gives the silhouette at cell
    resolution (0x81 solid, 0x83 blank, other codes = edge tiles). The letters have the traits of the classic Stencil
    face (bridge between the B's stem and bowls, the 0 split in two, the E's middle arm detached, thick/thin V),
    so each letter is drawn with an open-licence Stencil clone (Stardos Stencil Bold) fitted to its cell box.
  * 0x00-0x1F: the SONY wordmark, one letter per 4x2 cells, drawn with a bold serif (DejaVu Serif Bold).

Writes the outlines (SVG path strings in CG pixels, 8 x 16 per cell) into ui/index.html between the CG-DATA
markers, and preview PNGs into work/cg/. Needs fontTools, numpy and Pillow, DejaVu Serif Bold, and Stardos
Stencil Bold (OFL, https://github.com/google/fonts/tree/main/ofl/stardosstencil) in work/fonts/ or given with --stencil.

usage: gen_cg.py [--stencil FONT.ttf] [--slab FONT.ttf] [--strapl STRAPL.OBJ]
"""
import sys, os, re, argparse
import numpy as np
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.basePen import BasePen
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
ap = argparse.ArgumentParser()
ap.add_argument('--stencil', default=os.path.join(ROOT, 'work', 'fonts', 'StardosStencil-Bold.ttf'))
ap.add_argument('--slab', default='/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf')
ap.add_argument('--strapl', default=os.path.join(ROOT, 'work', 'disks', 'sys2', 'STRAPL.OBJ'), help='STRAPL.OBJ (LHA) or its raw .abs')
args = ap.parse_args()
OUT = os.path.join(ROOT, 'work', 'cg'); os.makedirs(OUT, exist_ok=True)
sys.path.insert(0, HERE)
from absfile import load
strapl = load(args.strapl)
# the wordmark tile map: 8 rows x 80 columns at data offset 0x1DD8 (drawn by STRAPL text 0xC600E5B4 into the overlay
# plane as bank 7 cells; row 0 is blank)
LOGO_MAP = strapl.data[0x1DD8:0x1DD8 + 640]
m = [[LOGO_MAP[r * 80 + c] for c in range(80)] for r in range(8)]

class PolyPen(BasePen):
    """Collects the contours as polygons (curves flattened), for the previews."""
    def __init__(self, gs): super().__init__(gs); self.polys = []
    def _moveTo(self, p): self.polys.append([p])
    def _lineTo(self, p): self.polys[-1].append(p)
    def _curveToOne(self, a, b, c):
        p0 = self.polys[-1][-1]
        for i in range(1, 9):
            t = i / 8; u = 1 - t
            self.polys[-1].append(tuple(u**3 * p0[k] + 3 * u * u * t * a[k] + 3 * u * t * t * b[k] + t**3 * c[k] for k in (0, 1)))
    def _qCurveToOne(self, a, b):
        p0 = self.polys[-1][-1]
        for i in range(1, 9):
            t = i / 8; u = 1 - t
            self.polys[-1].append(tuple(u * u * p0[k] + 2 * u * t * a[k] + t * t * b[k] for k in (0, 1)))

POLYS = {}   # path string -> polygons in CG pixels

def outline(font, ch, box=None, size=None, dx=0):
    """SVG path of `ch` in CG pixels (y down). box=(x0, y0, x1, y1): the ink is stretched to fill it.
    size=(px, w, h): drawn at px pixels per em with its ink centred in a w x h box (moved right by dx)."""
    gs = font.getGlyphSet(); name = font.getBestCmap()[ord(ch)]
    bp = BoundsPen(gs); gs[name].draw(bp); xmin, ymin, xmax, ymax = bp.bounds
    if box:
        x0, y0, x1, y1 = box
        sx, sy = (x1 - x0) / (xmax - xmin), (y1 - y0) / (ymax - ymin)
    else:
        px, w, h = size
        sx = sy = px / font['head'].unitsPerEm
        x0, y0 = dx + (w - (xmax - xmin) * sx) / 2, (h - (ymax - ymin) * sy) / 2
    pen = SVGPathPen(gs, ntos=lambda v: ('%.2f' % v).rstrip('0').rstrip('.'))
    tf = (sx, 0, 0, -sy, x0 - xmin * sx, y0 + ymax * sy)
    gs[name].draw(TransformPen(pen, tf))
    pp = PolyPen(gs); gs[name].draw(TransformPen(pp, tf))
    d = pen.getCommands(); POLYS[d] = pp.polys
    return d

def raster(paths, w, h, scale=1):
    """Fill the outlines (non-zero winding approximated by even-odd per glyph: the fonts have no overlaps)."""
    im = np.zeros((h * scale, w * scale), bool)
    for d in paths:
        for poly in POLYS[d]:
            mask = Image.new('1', (w * scale, h * scale), 0)
            ImageDraw.Draw(mask).polygon([(x * scale, y * scale) for x, y in poly], fill=1)
            im ^= np.asarray(mask)
    return im

def preview(paths, w, h, name, scale=3):
    Image.fromarray(raster(paths, w, h, scale).astype(np.uint8) * 255).save(os.path.join(OUT, name))

# ---------------------------------------------------------------- 1. BVE-9100 wordmark
# letter boxes in cells: (char, col0, col1 inclusive, row0, row1 inclusive) in map coordinates
LETTERS = [('B', 1, 9, 1, 7), ('V', 12, 21, 1, 7), ('E', 24, 32, 1, 7), ('-', 34, 37, 3, 5),
           ('9', 39, 47, 1, 7), ('1', 50, 55, 1, 7), ('0', 59, 67, 1, 7), ('0', 70, 78, 1, 7)]
stencil = TTFont(args.stencil)
letters = [outline(stencil, ch, box=(c0 * 8, r0 * 16, (c1 + 1) * 8, (r1 + 1) * 16)) for ch, c0, c1, r0, r1 in LETTERS]
# The map's solid cells suggest heavier strokes than the font's, but emboldening the letters or forcing the map's
# cells onto them gives lumpy shapes, so the fitted letters are used as they are.
logo = ' '.join(letters)
# the first drawn cell of the map in raster order: the UI places the outlines from the cells the software wrote
first = next((r, c) for r in range(8) for c in range(80) if 0x80 <= m[r][c] < 0xe4 and m[r][c] != 0x83)
preview(letters, 640, 128, 'logo.png', 2)

# ---------------------------------------------------------------- 2. SONY wallpaper letters (32 x 32 CG pixels each)
slab = TTFont(args.slab)
sony = [outline(slab, ch, size=(34, 32, 32)) for ch in 'SONY']
preview([outline(slab, ch, size=(34, 32, 32), dx=32 * i) for i, ch in enumerate('SONY')], 128, 32, 'sony.png', 4)

# ---------------------------------------------------------------- write the outlines into ui/index.html
js = ('const CG_SONY=[' + ','.join("'%s'" % p for p in sony) + '];\n' +
      "const CG_LOGO={first:[%d,%d], path:'%s'};\n" % (first[0], first[1], logo))
ui = os.path.join(ROOT, 'ui', 'index.html')
page = open(ui).read()
a, b = page.index('// CG-DATA-BEGIN\n') + len('// CG-DATA-BEGIN\n'), page.index('// CG-DATA-END')
open(ui, 'w').write(page[:a] + js + page[b:])
print('wordmark %d bytes, SONY %d bytes; first drawn map cell %s' % (len(logo), sum(map(len, sony)), first))
print('wrote the outlines into', ui, 'and previews into', OUT)
