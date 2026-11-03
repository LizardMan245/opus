"""The castle's scene for the page: the voxel castle on its island, rasterised in dotted outlines (make_art.py reads the
part before the colour rendering). 0.3.0: a third bigger (tiles 8 x 4, voxels 7 high; were 6 x 3 and 5)."""
import numpy as np
from PIL import Image

N = 14
HW, HH, VH = 8, 4, 7
W, H = 320, 230
CX, CY = W // 2, 80
BLUE, GREEN = (30, 110, 255), (40, 200, 70)

vox = {}
OX = OY = OZ = 0


def add(x, y, z, g, win=False):
    vox[(x + OX, y + OY, z + OZ)] = (g, win)


def box(x0, x1, y0, y1, z0, z1, g):
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                add(x, y, z, g)


# island: flat, one voxel thick; moat is a 1-cell ring, flush with the ground
def moat(x, y):
    return 3 <= x <= 10 and 3 <= y <= 10 and not (4 <= x <= 9 and 4 <= y <= 9)


for x in range(N):
    for y in range(N):
        if moat(x, y):
            add(x, y, 0, 'bridge' if (6 <= x <= 7 and y == 10) else 'water')
        else:
            add(x, y, 0, 'path' if (6 <= x <= 7 and y >= 11) else 'ground')

# castle: 6x6 footprint, 2x2 corner towers, low walls between, 2x2 keep
OX = OY = 4
OZ = 1
towers = [(0, 0), (4, 0), (0, 4), (4, 4)]
for i, (tx, ty) in enumerate(towers):
    g = f'tower{i}'
    box(tx, tx + 1, ty, ty + 1, 0, 4, g)
    add(tx + 1, ty, 2, g, True)
    add(tx, ty + 1, 2, g, True)
    for x in (tx, tx + 1):                       # checker-board battlement
        for y in (ty, ty + 1):
            if (x + y) % 2 == 0:
                add(x, y, 5, g)
for x in (2, 3):                                 # walls (front wall has the gate)
    add(x, 0, 0, 'wall'); add(x, 0, 1, 'wall'); add(x, 0, 2, 'wall')
    for z in range(3):
        if z >= 2:
            add(x, 5, z, 'wall')                 # lintel only; gate opening below
for y in (2, 3):
    for z in range(3):
        add(0, y, z, 'wall'); add(5, y, z, 'wall')
box(2, 3, 2, 3, 0, 5, 'keep')
add(3, 2, 3, 'keep', True)
add(2, 3, 3, 'keep', True)
add(2, 2, 6, 'keep'); add(3, 3, 6, 'keep')
add(2, 3, 6, 'flag')

# four small trees
OX = OY = 0
OZ = 1


def tree(cx, cy, n):
    g = f'pine{n}'
    add(cx, cy, 0, f'trunk{n}'); add(cx, cy, 1, f'trunk{n}')
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            if abs(dx) + abs(dy) < 2:
                add(cx + dx, cy + dy, 2, g)
    add(cx, cy, 3, g)


for i, (x, y) in enumerate([(1, 3), (1, 11), (12, 4), (12, 8)]):
    tree(x, y, i)

# ------------------------------------------------------------------ rasteriser
ID = np.zeros((H, W), dtype=np.int32)
SEQ = np.zeros((H, W), dtype=np.int32)
FILL = np.zeros((H, W), dtype=np.uint8)
GC = np.zeros((H, W), dtype=np.uint8)
WIN = np.zeros((H, W), dtype=bool)
ids = {}
seq = 0


def P(x, y, z):
    return CX + (x - y) * HW, CY + (x + y) * HH - z * VH


STRUCT = (70, 110, 235)
LIGHT = (90, 190, 255)
GREEN = (110, 225, 120)
WHITE = (235, 235, 235)
RED = (235, 70, 60)
DIM = (45, 65, 140)
BROWN = (170, 120, 80)
PAL = [(0, 0, 0), STRUCT, LIGHT, GREEN, WHITE, RED, DIM, BROWN]


def color_of(g):
    if g == 'water': return 2
    if g.startswith('pine'): return 3
    if g == 'bridge': return 4
    if g == 'flag': return 5
    if g in ('ground', 'path'): return 6
    if g.startswith('trunk'): return 7
    return 1


def fill_of(kind, g, px, py):
    """dot-density modulus: a grid dot is drawn when hash % m == 0 (0 = no dots)"""
    if g == 'water': return 1 if kind == 1 else 0
    if g.startswith('pine'): return {1: 1, 3: 2, 2: 5}[kind]
    if g == 'bridge': return 2 if kind == 1 else 0
    if g == 'flag': return 1
    if g == 'ground': return 14 if kind == 1 else 0
    if g in ('path',) or g.startswith('trunk'): return 0
    return {1: 0, 2: 0, 3: 4}[kind]


def face(O, A, B, key, kind, g, win):
    global seq
    seq += 1
    fid = ids.setdefault(key, len(ids) + 1)
    wid = ids.setdefault(key + ('w',), len(ids) + 1)
    ox, oy = O
    xs = [ox, ox + A[0], ox + B[0], ox + A[0] + B[0]]
    ys = [oy, oy + A[1], oy + B[1], oy + A[1] + B[1]]
    det = A[0] * B[1] - A[1] * B[0]
    for py in range(int(min(ys)) - 1, int(max(ys)) + 2):
        for px in range(int(min(xs)) - 1, int(max(xs)) + 2):
            qx, qy = px + 0.5 - ox, py + 0.5 - oy
            u = (qx * B[1] - qy * B[0]) / det
            v = (A[0] * qy - A[1] * qx) / det
            if 0 <= u < 1 and 0 <= v < 1 and 0 <= px < W and 0 <= py < H:
                w = win and kind != 1 and 0.3 <= u < 0.7 and 0.2 <= v < 0.8
                ID[py, px] = wid if w else fid
                SEQ[py, px] = seq
                WIN[py, px] = w
                FILL[py, px] = 0 if w else fill_of(kind, g, px, py)
                GC[py, px] = color_of(g)


for (x, y, z) in sorted(vox, key=lambda c: (c[0] + c[1] + c[2], c[2])):
    g, win = vox[(x, y, z)]
    if (x, y, z + 1) not in vox:
        face(P(x, y, z + 1), (HW, HH), (-HW, HH), ('T', z, g), 1, g, win)
    if (x, y + 1, z) not in vox:
        face(P(x, y + 1, z + 1), (HW, HH), (0, VH), ('L', y, g), 2, g, win)
    if (x + 1, y, z) not in vox:
        face(P(x + 1, y, z + 1), (-HW, HH), (0, VH), ('R', x, g), 3, g, win)

def h(x, y):
    return (x * 73 + y * 151 + x * y * 7) % 1000 // 7


EDGE = np.zeros((H, W), dtype=bool)
for py in range(H):
    for px in range(W):
        if ID[py, px] == 0 or WIN[py, px]:
            continue
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = px + dx, py + dy
            nid = ID[ny, nx] if 0 <= nx < W and 0 <= ny < H else 0
            if nid == ID[py, px]:
                continue
            if nid == 0 or WIN[ny, nx] or SEQ[py, px] > SEQ[ny, nx]:
                EDGE[py, px] = True
                break

yy, xx = np.mgrid[0:H, 0:W]
mask = EDGE & ((xx + yy) % 3 != 0)                       # dotted outlines
dots = np.zeros((H, W), dtype=bool)
for py in range(0, H, 2):
    for px in range(0, W, 2):
        m = FILL[py, px]
        if ID[py, px] and not WIN[py, px] and m and h(px, py) % m == 0:
            dots[py, px] = True
            if h(px + 5, py + 3) % 5 == 0:               # occasional 2x2 dot
                for ex, ey in ((1, 0), (0, 1), (1, 1)):
                    if py + ey < H and px + ex < W and ID[py + ey, px + ex] == ID[py, px]:
                        dots[py + ey, px + ex] = True
mask |= dots

bbox = Image.fromarray((ID > 0).astype(np.uint8) * 255).getbbox()
print('sprite size', bbox[2] - bbox[0], 'x', bbox[3] - bbox[1])
pal = np.array(PAL, dtype=np.uint8)
rgb = pal[np.where(mask, GC, 0)]
mono = np.where(mask, 255, 0).astype(np.uint8)


def save(arr, name, mode):
    img = Image.fromarray(arr).crop(bbox)
    canvas = Image.new(mode, (320, 160), 0)
    canvas.paste(img, ((320 - img.width) // 2, (160 - img.height) // 2))
    canvas.save(f'{name}_native.png')
    canvas.resize((1280, 640), Image.NEAREST).save(f'{name}_preview.png')
    return img


save(rgb, 'castle_dot', 'RGB').save('castle_dot_sprite.png')
save(mono, 'castle_dot_mono', 'L')
