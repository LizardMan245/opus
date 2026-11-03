#!/usr/bin/env python3
"""Writes ../castle_art.h: everything the castle page draws, as lists of pixels and particles, from the same geometry
and seeds as the mockup (castle_anim5.py on the computer). Coordinates are the scene's, (0, 0) its centre (the page puts it
on the panel 1:1, centred). Run from this folder: python3 make_art.py

The scene: scene.py (the voxel castle on its island, rasterised in dotted outlines). What moves is drawn by
castle_page.c from these lists every frame.
"""
import math
import numpy as np
from PIL import Image

_src = open('scene.py').read()
g = {}
exec(_src[:_src.index('yy, xx = np.mgrid')], g)
ID, FILL, WIN, GC, EDGE, h, P, N, VH, H, W = (g[k] for k in
                                             ('ID', 'FILL', 'WIN', 'GC', 'EDGE', 'h', 'P', 'N', 'VH', 'H', 'W'))

yy, xx = np.mgrid[0:H, 0:W]
dots = np.zeros((H, W), dtype=bool)
for py in range(0, H, 2):
    for px in range(0, W, 2):
        m = FILL[py, px]
        if ID[py, px] and not WIN[py, px] and m and h(px, py) % m == 0:
            dots[py, px] = True

bx0, by0, bx1, by1 = Image.fromarray((ID > 0).astype(np.uint8) * 255).getbbox()
SCALE = 4 / 3                    # 0.3.0: the scene a third bigger; distances the particles travel grow with it
OXs = -((bx0 + bx1) // 2)        # the scene's coordinates: (0, 0) the castle's and island's centre (the page centres it)
OYs = -((by0 + by1) // 2)

edge_pat = EDGE & ((xx + yy) % 3 != 0)
hash_px = (xx * 37 + yy * 91 + xx * yy * 13) % 100
struct_mask = (edge_pat | dots) & np.isin(GC, (1, 4, 7))
ground_mask = (edge_pat | dots) & (GC == 6)
water_edge = edge_pat & (GC == 2)
water_px = (GC == 2) & (ID > 0) & ~EDGE & ((xx + yy) % 2 == 0)
leaf_mask = (edge_pat | dots) & (GC == 3)
flag_px = (GC == 5) & (ID > 0)

sy, sx = np.nonzero(struct_mask)
gy, gx = np.nonzero(ground_mask)
wey, wex = np.nonzero(water_edge)
wy, wx = np.nonzero(water_px)
ly, lx = np.nonzero(leaf_mask)
lh = hash_px[ly, lx]
fy, fx = np.nonzero(flag_px)
fon = (fx + fy) % 2 == 0
fy, fx = fy[fon], fx[fon]
wpy, wpx = np.nonzero(WIN)

rng = np.random.default_rng(7)

# BLUE: the waterfall streams along the platform's two front edges, in the order they start
edge_pts = [P(i + 0.5, N, 1) for i in range(N)] + [P(N, i + 0.5, 1) for i in range(N)]
order = rng.permutation(len(edge_pts))
streams = []
for r, k in enumerate(order):
    x, y = edge_pts[k]
    streams.append((int(x), int(y), r / len(edge_pts), rng.uniform(0, 1), rng.uniform(28, 40) * SCALE))

# GREEN: falling leaves, the piles, the leaves blown off the platform
LEAVES = [(int(rng.integers(len(lx))), rng.uniform(0, 1), rng.uniform(0.25, 0.45), rng.uniform(0, 6.3))
          for _ in range(70)]
TREES = [(1, 3), (1, 11), (12, 4), (12, 8)]
tree_base = {i: P(cx + 0.5, cy + 0.5, 1)[1] for i, (cx, cy) in enumerate(TREES)}
PILE = []
for i, (cx, cy) in enumerate(TREES):
    spots = []
    for _ in range(150):
        a, r = rng.uniform(0, 6.3), min(2.4, abs(rng.normal(0, 1.1)) + 0.2)
        dx, dy = math.cos(a) * r, math.sin(a) * r
        if not (0.2 < cx + 0.5 + dx < N - 0.2 and 0.2 < cy + 0.5 + dy < N - 0.2):
            continue
        X, Y = P(cx + 0.5 + dx, cy + 0.5 + dy, 1)
        spots.append((X, Y, r, i, rng.choice([1.0, 0.8, 0.6]), cx + cy + 1 + dx + dy))
    PILE += sorted(spots, key=lambda q: q[2])
PILE.sort(key=lambda q: q[2] + rng.uniform(0, 0.4))
leaf_tree = np.array([min(range(4), key=lambda i: (P(TREES[i][0] + .5, TREES[i][1] + .5, 1)[0] - lx[j]) ** 2
                          + (P(TREES[i][0] + .5, TREES[i][1] + .5, 1)[1] - ly[j]) ** 2) for j in range(len(lx))])
leaf_ground = [min((b for b in tree_base.values() if b > ly[j]), default=ly[j] + 20) + 4 for j in range(len(lx))]

# ORANGE: flame emitters: the windows first, then the towers, then the keep
win_ids = sorted(set(ID[WIN].tolist()), key=lambda i: (i * 2654435761) % 1000)
emitters = []
win_rank = {}
for n_, wid in enumerate(win_ids):
    ys, xs = np.nonzero((ID == wid) & WIN)
    emitters.append((int(xs.mean()), int(ys.min()), 0.6 * SCALE))
    win_rank[wid] = n_
for tx, ty in [(0, 0), (4, 0), (0, 4), (4, 4)]:
    x, y = P(tx + 4 + 1, ty + 4 + 1, 7)
    emitters.append((int(x), int(y), 1.0 * SCALE))
x, y = P(7, 7, 8)
emitters.append((int(x), int(y), 1.4 * SCALE))

CAST = (np.isin(GC, (1, 5)) & (ID > 0)) | WIN
CASTLE_DEPTH = 13

BOLT_TARGETS = [(x, y) for (x, y, sz) in emitters if sz >= 1.0 * SCALE] + [P(2, 12, 1), P(12, 2, 1), P(7, 13, 1)]

EXIT = {}
for i, (cx, cy) in enumerate(TREES):
    if N - cy <= N - cx:
        EXIT[i] = [P(cx + 0.5 + d, N, 1) for d in (-1.2, -0.4, 0.4, 1.2)]
    else:
        EXIT[i] = [P(N, cy + 0.5 + d, 1) for d in (-1.2, -0.4, 0.4, 1.2)]
SPILL = [(int(rng.integers(len(PILE))), rng.uniform(0, 1), rng.uniform(0.18, 0.3), rng.uniform(0, 6.3))
         for _ in range(36)]

# ---------------------------------------------------------------- to C
TURN = 65536                     # a turn (Q16)


def sx_(x):
    return int(round(x)) + OXs


def sy_(y):
    return int(round(y)) + OYs


def xy_list(name, ys, xs):
    vals = []
    for y, x in zip(ys, xs):
        vals += [sx_(x), sy_(y)]
    return f"#define {name}_N {len(ys)}\nstatic const short {name}[{2 * len(ys)}] = {{\n" + rows(vals, 16) + "\n};\n"


def rows(vals, per=12):
    return "\n".join("    " + ", ".join(str(int(v)) for v in vals[i:i + per]) + "," for i in range(0, len(vals), per))


def arr(ctype, name, vals, per=12):
    return f"static const {ctype} {name}[{len(vals)}] = {{\n" + rows(vals, per) + "\n};\n"


out = ["/* Generated by art/make_art.py from art/scene.py: do not edit. The scene's coordinates: (0, 0) its centre. */",
       "#ifndef CASTLE_ART_H", "#define CASTLE_ART_H", ""]
out.append(f"#define ART_SIDE {VH}               /* the platform's side, px (the water's lip) */")
out.append(f"#define ART_SCALE {round(SCALE * 256)}           /* the particles' distances, Q8 (the scene's size) */")
out.append(f"#define ART_TOP {by0 + OYs}             /* the castle's top, scene px */")
out.append(xy_list('ART_GROUND', gy, gx))
out.append(xy_list('ART_STRUCT', sy, sx))
out.append(xy_list('ART_WATER_EDGE', wey, wex))
out.append(xy_list('ART_WATER', wy, wx))
out.append("/* each water pixel's ripple phase (0.45 x + 0.9 y radians), Q16 of a turn */")
out.append(arr('unsigned short', 'ART_WATER_PH', [int(((0.45 * x + 0.9 * y) / SCALE / (2 * math.pi)) * TURN) & 0xFFFF
                                                   for y, x in zip(wy, wx)]))

out.append(xy_list('ART_WIN', wpy, wpx))
out.append("/* each window pixel's window: its rank (the order the windows light), for the glow */")
out.append(arr('unsigned char', 'ART_WIN_RANK', [win_rank[ID[y, x]] for y, x in zip(wpy, wpx)], 24))
# leaves: x, y, hash (0-99, the order they fall from the canopy), tree, ground y under them
out.append(xy_list('ART_LEAF', ly, lx))
out.append(arr('unsigned char', 'ART_LEAF_HASH', lh.tolist(), 24))
out.append(arr('unsigned char', 'ART_LEAF_TREE', leaf_tree.tolist(), 24))

# the pile: x, y, mound factor (Q8: 1 - r / 2.4), tree, shade (Q8), and which of its two pixels the castle hides at
# each mound height 0-5 (bit 2m: x, bit 2m + 1: x + 1)
pv = []
for (X, Y, r, ti, shade, dep) in PILE:
    Xi, Yi = int(X), int(round(Y))
    hide = 0
    for m in range(6):
        for b, xq in enumerate((Xi, Xi + 1)):
            yq = Yi - m
            if dep < CASTLE_DEPTH and 0 <= yq < H and 0 <= xq < W and CAST[yq, xq]:
                hide |= 1 << (2 * m + b)
    pv += [Xi + OXs, Yi + OYs, int(round(max(0.0, 1 - r / 2.4) * 256)), ti, int(round(shade * 256)), hide]
out.append(f"#define ART_PILE_N {len(PILE)}\n/* x, y, mound (Q8), tree, shade (Q8), hidden bits */")
out.append(arr('short', 'ART_PILE', pv, 12))
for i in range(4):                                       # each tree's spots, in pile order
    idx = [k for k, q in enumerate(PILE) if q[3] == i]
    out.append(f"#define ART_TREE{i}_N {len(idx)}")
    out.append(arr('short', f'ART_TREE{i}', idx, 16))
out.append("static const short *const ART_TREE_SPOTS[4] = { ART_TREE0, ART_TREE1, ART_TREE2, ART_TREE3 };")
out.append("static const short ART_TREE_SPOTS_N[4] = { ART_TREE0_N, ART_TREE1_N, ART_TREE2_N, ART_TREE3_N };\n")
# particles: index, phase (Q16 turn), speed (Q16 turns a second), sway phase (Q16 of a turn of radians: sw / 2 pi)
lv = []
for s_, ph, spd, sw in LEAVES:
    lv += [s_, int(ph * TURN) & 0xFFFF, int(spd * TURN), int(sw / (2 * math.pi) * TURN) & 0xFFFF]
out.append(f"#define ART_FALL_N {len(LEAVES)}\n/* leaf pixel, phase, speed, sway (Q16) */")
out.append("static const unsigned ART_FALL[%d] = {\n%s\n};\n" % (len(lv), rows(lv, 12)))
sv = []
for si, ph, spd, sw in SPILL:
    sv += [si, int(ph * TURN) & 0xFFFF, int(spd * TURN), int(sw / (2 * math.pi) * TURN) & 0xFFFF]
out.append(f"#define ART_SPILL_N {len(SPILL)}\n/* pile spot, phase, speed, sway (Q16) */")
out.append("static const unsigned ART_SPILL[%d] = {\n%s\n};\n" % (len(sv), rows(sv, 12)))
ev = []
for i in range(4):
    for (x, y) in EXIT[i]:
        ev += [sx_(x), sy_(y)]
out.append("/* where each tree's leaves go over the edge: four points a tree */")
out.append(arr('short', 'ART_EXIT', ev, 16))
stv = []
for x, y, rank, ph, length in streams:
    stv += [x + OXs, y + OYs, int(rank * 32768), int(ph * 30 * 256), int(length * 256)]
out.append(f"#define ART_STREAM_N {len(streams)}\n/* x, y, rank (Q15: the blue level it starts at), phase (px Q8), length (px Q8) */")
out.append("static const int ART_STREAM[%d] = {\n%s\n};\n" % (len(stv), rows(stv, 15)))
emv = []
for x, y, sz in emitters:
    emv += [x + OXs, y + OYs, int(round(sz * 256))]
out.append(f"#define ART_EMIT_N {len(emitters)}\n/* x, y, size (Q8) */")
out.append(arr('short', 'ART_EMIT', emv, 15))
bv = []
for x, y in BOLT_TARGETS:
    bv += [sx_(x), sy_(y)]
out.append(f"#define ART_BOLT_N {len(BOLT_TARGETS)}")
out.append(arr('short', 'ART_BOLT', bv, 16))
out.append("#endif")
open('../castle_art.h', 'w').write("\n".join(out) + "\n")
print('struct', len(sy), 'ground', len(gy), 'water', len(wy), 'leaves', len(ly), 'pile', len(PILE),
      'windows', len(wpy), 'emitters', len(emitters), 'scene offset', OXs, OYs)
