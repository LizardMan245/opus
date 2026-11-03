/* castle's T1 page: the dotted castle on its island, every frame, the four encoders acting on it as the mockup
 * (castle_anim5.py) does. No values: the scene is the display. The keep's flag flies in drone mode.
 *
 *   blue    water pours off the platform's two front edges, stream by stream
 *   green   leaves fall from the trees, pile up under them (the pile has memory: it grows while green is up and clears
 *           slowly when it is down) and tumble off the platform's edge
 *   white   the castle flickers and lightning strikes the towers, the keep and the ground
 *   orange  the windows glow and flames come out of them, then out of the towers, then the keep
 *
 * Pixels straight into the frame (opus_page_pixels), the scene 1:1 in the panel's own pixels, the castle and its
 * island centred in what the window shows: its dots stay dots on every panel. Every colour is the user's encoder colour (opus_accent_now) or a grey, so a theme recolours it.
 * Integer only (the display thread has no floating point either); the time is the engine's clock in ms. The scene's
 * pixels and particles: castle_art.h (art/make_art.py), in scene coordinates around the castle's centre; distances the
 * particles travel are scaled with the scene (ART_SCALE). */
#include "opus_plugin.h"
#include "castle_page.h"
#include "castle_art.h"

#define GREY_RGB     0xA7A9ACu             /* the castle: #6F7173 x 1.5 */
#define GREY_DIM_RGB 0x434344u             /* the ground: #6F7173 x 0.6 */
#define POLE_RGB     0xD3D4D6u             /* the flagpole and pennant: a lighter grey, to read above the keep */
#define LILAC_RGB    0xDFD9FFu             /* the palette's lilac accent: the hottest part of a flame */
#define TURN         65536                 /* a turn, Q16 */
#define RAD          10430                 /* a radian in Q16 turns: 65536 / (2 pi) */
#define SC(px)       (((px) * ART_SCALE) >> 8)   /* a mockup distance in px, at the scene's size */

static short SIN[1024];                    /* sin, Q12, a turn in 1024 steps */
static unsigned short *fb;
static int R, PW, PH, X0, Y0;              /* the panel: its long side, width, height; the scene's place on it */
static unsigned seed = 1;
static int pile;                           /* the leaves' pile, Q15 */
static unsigned last_ms;

void castle_page_init(void)
{
    for (int i = 0; i < 1024; ++i) {
        int s, c, t = i * 4;
        opus_sincos(t > 2048 ? t - 4096 : t, &s, &c);
        SIN[i] = (short)s;
    }
}

static inline int sin16(unsigned turn)     /* turn Q16 -> sin Q12 */
{
    return SIN[(turn >> 6) & 1023];
}

static inline int rnd15(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (int)(seed >> 17);
}

/* a phase in Q16 of a turn after t ms at rate turns a second (Q16): exact in its low bits however long t runs */
static inline unsigned phase(unsigned t_ms, int rate_q16)
{
    const unsigned per_ms = ((unsigned)rate_q16 * 16777u) >> 16;  /* Q24 turns a ms: x 256 / 1000, without a divide */
    return ((t_ms * per_ms) >> 8) & 0xFFFF;
}

/* ---------------------------------------------------------------- colours */

static unsigned short c565(unsigned rgb, int s256)
{
    int r = (int)((rgb >> 16) & 255) * s256 >> 8, g = (int)((rgb >> 8) & 255) * s256 >> 8, b = (int)(rgb & 255) * s256 >> 8;
    r = r > 255 ? 255 : r;
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;
    return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static unsigned mixrgb(unsigned a, unsigned b, int t256)   /* a towards b by t */
{
    unsigned o = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const int x = (int)((a >> sh) & 255), y = (int)((b >> sh) & 255);
        o |= (unsigned)(x + (((y - x) * t256) >> 8)) << sh;
    }
    return o;
}

/* ---------------------------------------------------------------- pixels */

static inline __attribute__((always_inline)) void put(int x, int y, unsigned short c)   /* scene coordinates */
{
    x += X0;
    y += Y0;
    if ((unsigned)x < (unsigned)PW && (unsigned)y < (unsigned)PH)
        fb[256 * (R - 1 - x) + y] = c;
}

/* The scene's fixed pixels as places in the frame, worked out once for the panel (0.3.2): drawing one is a store, no
 * test. The scene fits every panel (224 x 130 around the window's centre); a place that would not is sent to the frame's
 * first word (its corner, off the window) so nothing is written outside the frame. */
static unsigned OFF_GROUND[ART_GROUND_N], OFF_STRUCT[ART_STRUCT_N], OFF_WEDGE[ART_WATER_EDGE_N], OFF_WATER[ART_WATER_N];
static unsigned OFF_WIN[ART_WIN_N], OFF_LEAF[ART_LEAF_N];
static int offs_rows;

static void offsets(const short *xy, int n, unsigned *o)
{
    for (int i = 0; i < n; ++i) {
        const int x = xy[2 * i] + X0, y = xy[2 * i + 1] + Y0;
        o[i] = (unsigned)x < (unsigned)PW && (unsigned)y < (unsigned)PH ? (unsigned)(256 * (R - 1 - x) + y) : 0u;
    }
}

static void make_offsets(void)
{
    offsets(ART_GROUND, ART_GROUND_N, OFF_GROUND);
    offsets(ART_STRUCT, ART_STRUCT_N, OFF_STRUCT);
    offsets(ART_WATER_EDGE, ART_WATER_EDGE_N, OFF_WEDGE);
    offsets(ART_WATER, ART_WATER_N, OFF_WATER);
    offsets(ART_WIN, ART_WIN_N, OFF_WIN);
    offsets(ART_LEAF, ART_LEAF_N, OFF_LEAF);
    offs_rows = R;
}

static void list(const unsigned *o, int n, unsigned short c)
{
    for (int i = 0; i < n; ++i)
        fb[o[i]] = c;
}

/* a line in Q8 scene coordinates, a pixel a step */
static void line8(int x0, int y0, int x1, int y1, int dx, unsigned short c)
{
    int ax = x1 - x0, ay = y1 - y0;
    const int n = ((ax < 0 ? -ax : ax) > (ay < 0 ? -ay : ay) ? (ax < 0 ? -ax : ax) : (ay < 0 ? -ay : ay)) / 256 + 1;
    const int sx = ax / n, sy = ay / n;                            /* a step a pixel: two divides a line, not two a pixel */
    int x = x0 + 128, y = y0 + 128;
    for (int i = 0; i < n; ++i, x += sx, y += sy)
        put((x >> 8) + dx, y >> 8, c);
}

/* ---------------------------------------------------------------- the frame */

static int clamp15(int v)
{
    return v < 0 ? 0 : v > 32767 ? 32767 : v;
}

static int mound(int mf, int pl)            /* a pile spot's height, px: (1 - r / 2.4) x pile x 4, at the scene's size */
{
    return ((((mf * pl) >> 15) * 4) * ART_SCALE) >> 16;
}

int castle_page_draw(const short *knobs, unsigned t, int drone)
{
    int rows;
    fb = opus_page_pixels(&rows);
    if (!fb || (rows != 320 && rows != 376 && rows != 400))
        return 0;
    R = rows, PW = rows, PH = rows / 2;
    X0 = PW / 2;                           /* the scene's centre: the middle of what the window shows (to about 147 of
                                              the frame's 160) */
    Y0 = PH * 147 / 320;
    if (offs_rows != R)
        make_offsets();
    const int blue = clamp15(knobs[0]), green = clamp15(knobs[1]), white = clamp15(knobs[2]), orange = clamp15(knobs[3]);
    const unsigned e1 = opus_accent_now(0), e2 = opus_accent_now(1), e3 = opus_accent_now(2), e4 = opus_accent_now(3);
    seed ^= t * 2654435761u;

    /* the pile's memory: up 0.32 a second while green is full, down 0.12 a second while it is down */
    int dt = last_ms ? (int)(t - last_ms) : 0;
    dt = dt < 0 || dt > 250 ? 0 : dt;
    last_ms = t;
    pile += (((green * 10486) >> 15) - (((32768 - green) * 3932) >> 15)) * dt / 1000;
    pile = clamp15(pile);

    list(OFF_GROUND, ART_GROUND_N, c565(GREY_DIM_RGB, 256));

    /* the moat: a gentle ripple, always */
    const unsigned rip = phase(t, 39322);                          /* 0.6 a second */
    const unsigned short wb = c565(e1, 256), wh = c565(mixrgb(e1, 0xFFFFFF, 128), 256);
    for (int i = 0; i < ART_WATER_N; ++i) {
        const int v = sin16(ART_WATER_PH[i] - rip);
        if (v > 1434)
            fb[OFF_WATER[i]] = v > 3686 ? wh : wb;
    }
    list(OFF_WEDGE, ART_WATER_EDGE_N, c565(e1, 141));

    /* WHITE: the castle flickers (pixels crackle, now and then it nearly drops out), its windows flash */
    if (white > 655) {
        const int keep = (white * 18022) >> 15, blink = rnd15() < ((white * 11469) >> 15);
        const int s1 = 32768 - ((white * rnd15()) >> 16), s2 = (((white * rnd15()) >> 15) * 9830) >> 15;
        unsigned rgb = 0;
        for (int sh = 0; sh <= 16; sh += 8) {
            const int c = ((int)((GREY_RGB >> sh) & 255) * s1 + (int)((e3 >> sh) & 255) * s2) >> 15;
            rgb |= (unsigned)(c > 255 ? 255 : c) << sh;
        }
        const unsigned short cc = c565(rgb, 256);
        for (int i = 0; i < ART_STRUCT_N; ++i)
            if (rnd15() > keep && (!blink || rnd15() > 27853))
                fb[OFF_STRUCT[i]] = cc;
        const unsigned short wf = c565(e3, 205);
        for (int i = 0; i < ART_WIN_N; ++i)
            if (rnd15() < white >> 1)
                fb[OFF_WIN[i]] = wf;
    } else
        list(OFF_STRUCT, ART_STRUCT_N, c565(GREY_RGB, 256));

    /* the keep's flagpole: in drone mode a pennant flies from it, waving; out of drone mode the pole stands bare */
    {
        const int fx = ART_EMIT[3 * (ART_EMIT_N - 1)], fy = ART_EMIT[3 * (ART_EMIT_N - 1) + 1];   /* the keep's roof */
        const int pole = SC(16), len = SC(11), hgt = SC(6);
        const unsigned short pc = c565(POLE_RGB, 256);
        for (int j = 0; j < pole; ++j)
            put(fx, fy - j, pc);
        put(fx, fy - pole, pc);
        if (drone) {
            const unsigned fp = phase(t, 78643);                   /* 1.2 waves a second along it */
            const unsigned short cc = c565(POLE_RGB, 256), cd = c565(POLE_RGB, 180);
            const int taper = 65536 / (len + 1);
            for (int i = 1; i <= len; ++i) {
                const int h = hgt - ((hgt * i * taper) >> 16);     /* a pennant: tapering to its tip */
                const int wv = (sin16(fp - (unsigned)(i * 7000)) * i) >> 13;   /* the wave grows toward the tip */
                const int top = fy - pole + wv;
                for (int k = 0; k <= h; ++k)
                    if (k == 0 || k == h || ((i + k) & 1) == 0)  /* dotted inside, its edges whole */
                        put(fx + i, top + k, k == 0 || k == h ? cc : cd);
            }
        }
    }

    /* ORANGE: the windows glow, then flames from the windows, the towers, the keep */
    if (orange > 655) {
        const int lv = orange * 3 / 2 > 32767 ? 32767 : orange * 3 / 2;
        const int fl = 26214 + ((sin16(phase(t, 114737)) * 6554) >> 12);   /* 0.8 + 0.2 sin(11 t) */
        const unsigned short gc = c565(e4, (lv * fl >> 15) >> 7);
        list(OFF_WIN, ART_WIN_N, gc);
    }
    const int active = (orange * ART_EMIT_N + 16384) >> 15;
    const unsigned short hot = c565(LILAC_RGB, 256), mid = c565(e4, 256);
    const unsigned short hot_lo = c565(LILAC_RGB, 179), mid_lo = c565(e4, 179);
    unsigned short tip[16];                                        /* the tips' fade: 0.65 .. 1 of the cycle, 16 steps */
    for (int i = 0; i < 16 && active; ++i)
        tip[i] = c565(e4, (177 * (88474 - (42598 + i * 1434))) >> 16);
    const unsigned sway = phase(t, 93874);                         /* sin(9 t): 9 / 2 pi a second */
    for (int e = 0; e < active; ++e) {
        const int ex = ART_EMIT[3 * e], ey = ART_EMIT[3 * e + 1], size = ART_EMIT[3 * e + 2];
        const int hgt = ((1536 + ((orange * 3584) >> 15)) * size) >> 8;   /* (6 + 14 orange) x size, Q8 px */
        const int K = 10 + ((22 * size * orange) >> 23);
        const int kstep = 65536 / K;                               /* the particles spread along the flame's cycle */
        for (int k = 0; k < K; ++k) {
            const int frk = (k * 39977) & 0xFFFF;                  /* frac(0.61 k) */
            const unsigned a = (phase(t, 72090 + (frk >> 1)) + (unsigned)(k * kstep) + (unsigned)(e * 11141)) & 0xFFFF;
            const int reach = 36045 + ((((k * 24248) & 0xFFFF) * 29491) >> 16);   /* 0.55 + 0.45 frac(0.37 k) */
            const int t1 = (((((int)(65536 - a) >> 8) * size >> 8) * 666 >> 8) * sin16((unsigned)(k * 23990))) >> 12;
            const int t2 = (((sin16(sway + (unsigned)(k * RAD)) * 179) >> 8) * ((int)a >> 8)) >> 12;
            const int x = ex + ((t1 + t2 + 128) >> 8);
            const int y = ey - ((((((int)a >> 8) * hgt) >> 8) * (reach >> 8)) >> 16);
            const unsigned short c = a < 19661 ? hot : a < 42598 ? mid : tip[(((a - 42598) * 45) >> 16) > 15 ? 15 : ((a - 42598) * 45) >> 16];
            put(x, y, c);
            if (a < 32768)
                put(x, y + 1, a < 19661 ? hot_lo : mid_lo);
        }
    }

    /* GREEN: the canopies thin out, the pile grows, leaves fall onto it and tumble off the platform */
    const unsigned short lc = c565(e2, 256);
    {
        const int thr = (green * 60) >> 15;
        if (thr == 0)                                              /* green at 0: the whole canopy */
            list(OFF_LEAF, ART_LEAF_N, lc);
        else
            for (int i = 0; i < ART_LEAF_N; ++i)
                if (ART_LEAF_HASH[i] >= thr)
                    fb[OFF_LEAF[i]] = lc;
    }
    const int shown = (pile * ART_PILE_N) >> 15;
    unsigned short shade_c[5];                                     /* the pile's shades (154, 205, 256: by >> 6) */
    for (int i = 0; i < 5; ++i)
        shade_c[i] = c565(e2, i == 2 ? 154 : i == 3 ? 205 : 256);
    for (int i = 0; i < shown; ++i) {
        const short *q = ART_PILE + 6 * i;
        const int m = mound(q[2], pile), hide = q[5] >> (2 * m);
        const unsigned short c = shade_c[q[4] >> 6 > 4 ? 4 : q[4] >> 6];
        if (!(hide & 1))
            put(q[0], q[1] - m, c);
        if (!(hide & 2))
            put(q[0] + 1, q[1] - m, c);
    }
    const int nfall = (green * ART_FALL_N) >> 15, reach = (shown > 8 ? shown : 8) + 8;
    for (int j = 0; j < nfall; ++j) {
        const unsigned *f = ART_FALL + 4 * j;
        const int s = (int)f[0];
        const unsigned p = (phase(t, (int)f[2]) + f[1]) & 0xFFFF;
        const int x0 = ART_LEAF[2 * s], y0 = ART_LEAF[2 * s + 1], tr = ART_LEAF_TREE[s];
        int cnt = 0;
        const short *spots = ART_TREE_SPOTS[tr];
        while (cnt < ART_TREE_SPOTS_N[tr] && spots[cnt] < reach)
            ++cnt;
        int X = x0, Y = y0 + 20;
        if (cnt) {
            int jj = j;
            while (jj >= cnt)                                      /* j % cnt: j is under 70, cnt rarely under 8 */
                jj -= cnt;
            const short *q = ART_PILE + 6 * spots[jj];
            X = q[0], Y = q[1] - mound(q[2], pile);
        }
        const int y = y0 + (((Y - y0) * (int)p) >> 16);
        const int sw = sin16(((p * 93874u) >> 16) + f[3]);        /* sin(9 p + sway) */
        const int x = x0 + (((X - x0) * (int)p) >> 16) + ((((sw * SC(640)) >> 8) * (int)((65536 - p) >> 4)) >> 24);
        put(x, y, lc);
        put(x + 1, y, lc);
    }
    const int nspill = (int)(((unsigned)(green * pile) >> 15) * ART_SPILL_N >> 15);
    for (int j = 0; j < nspill; ++j) {
        const unsigned *f = ART_SPILL + 4 * j;
        const short *q = ART_PILE + 6 * f[0];
        const int X0_ = q[0], Y0_ = q[1], tr = q[3], shade = q[4];
        const int ex = ART_EXIT[(tr * 4 + (j & 3)) * 2], ey = ART_EXIT[(tr * 4 + (j & 3)) * 2 + 1];
        const unsigned p = (phase(t, (int)f[2]) + f[1]) & 0xFFFF;
        int x, y;
        unsigned short c;
        if (p < 29491) {                                           /* tumbling along the ground to the edge */
            const int qq = (int)((p * 72818u) >> 15);                 /* p / 0.45, without a divide */
            int hop = sin16(((unsigned)qq * 73013u >> 16) + f[3]); /* sin(7 q + sway) */
            hop = hop < 0 ? -hop : hop;
            x = X0_ + (((ex - X0_) * qq) >> 16);
            y = Y0_ + (((ey - Y0_) * qq) >> 16) - ((hop * SC(2)) >> 12);
            c = c565(e2, shade);
        } else {                                                   /* over the edge and down, fading */
            const int qq = (int)(((p - 29491u) * 59577u) >> 15);      /* (p - 0.45) / 0.55 */
            const int sw = sin16(((unsigned)qq * 83443u >> 16) + f[3]);   /* sin(8 q + sway) */
            x = ex + ((sw * SC(3)) >> 12) + ((qq * SC(4)) >> 16);
            y = ey + ((qq * SC(46)) >> 16);
            c = c565(e2, (((102 + ((154 * shade) >> 8)) * (65536 - ((qq * 29491) >> 16))) >> 16));
        }
        put(x, y, c);
        put(x + 1, y, c);
    }

    /* BLUE: the water pours off the platform's edges, a stream at a time as the knob goes up */
    {
        const unsigned flow = t * (unsigned)SC(655) / 64u;          /* 40 px a second at the scene's size, Q8 */
        unsigned short fall[17];                                   /* a stream's fade, 1 .. 0.4 in 16 steps */
        for (int i = 0; i <= 16 && blue > ART_STREAM[2]; ++i)     /* (the first stream's rank: none flow below it) */
            fall[i] = c565(e1, 256 - ((154 * i) >> 4));
        for (int i = 0; i < ART_STREAM_N; ++i) {
            const int *st = ART_STREAM + 5 * i;
            if (st[2] >= blue)
                continue;
            const int grow = (blue - st[2]) * 4 > 32768 ? 32768 : (blue - st[2]) * 4;
            const int L = ART_SIDE * 256 + ((st[4] * grow) >> 15);   /* Q8 */
            put(st[0], st[1], wh);
            const int n = L / 768;
            const int base = (int)((flow + (unsigned)st[3]) % (unsigned)L), fade = (256 << 16) / L;   /* d / L, Q8 */
            for (int k = 0; k < n; ++k) {
                int d = base + k * 768;                            /* under 2 L: one subtraction for the wrap */
                d = d >= L ? d - L : d;
                const unsigned short c = d < ART_SIDE * 256 ? wh : fall[(d * fade) >> 16 >> 4 > 16 ? 16 : (d * fade) >> 20];
                put(st[0] + ((k & 1) && d > ART_SIDE * 256), st[1] + (d >> 8), c);
            }
            if (grow >= 32768 && rnd15() < 16384)
                put(st[0] + ((rnd15() * 5) >> 15) - 2, st[1] + (L >> 8) + 1, c565(e1, 154));
        }
    }

    /* WHITE: lightning. A strike may come every 0.22 s (its chance white^2 x 0.55), shows 0.14 s, flashes 0.05 s. */
    if (white > 1638) {
        const unsigned k = t / 220, age = t % 220;
        if (age <= 140 && (int)((k * 2654435761u) % 1000u) < ((((white * white) >> 15) * 550) >> 15)) {
            const int bright = age < 50 ? 256 : 141;
            const unsigned saved = seed;
            seed = 1000 + k * 7919u;
            if (age < 50)
                list(OFF_STRUCT, ART_STRUCT_N, c565(e3, 218));
            const int ti = rnd15() % ART_BOLT_N;
            const int tx = ART_BOLT[2 * ti] * 256, ty = ART_BOLT[2 * ti + 1] * 256;
            int px[10], py[10];
            const int top = (-Y0 - 4) * 256;                       /* from above the window's top */
            px[0] = tx + rnd15() % (SC(70) * 256) - SC(35) * 256;
            py[0] = top;
            for (int i = 1; i <= 9; ++i) {
                px[i] = px[i - 1] + (tx - px[i - 1]) / (10 - i) + (rnd15() % (SC(10) * 256) - SC(5) * 256) * (9 - i) / 9;
                py[i] = top + (ty - top) * i / 9;
            }
            px[9] = tx, py[9] = ty;
            const int b0 = 2 + rnd15() % 3;
            int bx[5], by[5];
            bx[0] = px[b0], by[0] = py[b0];
            for (int i = 1; i <= 4; ++i) {
                bx[i] = bx[i - 1] + (rnd15() < 16384 ? rnd15() % (SC(9) * 256) - SC(7) * 256 : rnd15() % (SC(9) * 256) - SC(2) * 256);
                by[i] = by[i - 1] + SC(5) * 256 + rnd15() % (SC(4) * 256);
            }
            const unsigned short cb = c565(e3, bright), ce = c565(e3, (bright * 90) >> 8), cr = c565(e3, (bright * 179) >> 8);
            for (int i = 0; i < 9; ++i)
                line8(px[i], py[i], px[i + 1], py[i + 1], 1, ce);   /* a soft edge */
            for (int i = 0; i < 9; ++i)
                line8(px[i], py[i], px[i + 1], py[i + 1], 0, cb);
            for (int i = 0; i < 4; ++i)
                line8(bx[i], by[i], bx[i + 1], by[i + 1], 0, cr);
            for (int a = 0; a < 8; ++a) {                          /* sparks where it lands */
                const unsigned ang = (unsigned)(a * 8192) + k * RAD;
                put(tx / 256 + ((sin16(ang + 16384) * SC(4)) >> 12), ty / 256 + ((sin16(ang) * SC(3)) >> 12), cb);
            }
            seed = saved;
        }
    }

    return 1;
}
