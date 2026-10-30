/* outatime's T1: the time-machine page (the design reference v3, its drawing code ported), its instruments taken
 * over by meaning (2026-10-26):
 *
 * What the user set (DEST, the meter's arc and digits, the readout, the tabs' numbers) shows opus_engine_knobs (the
 * stored knobs in core 2.3-2.7; since core 2.8 the LFO in while a note sounds, 2026-10-28: every value
 * follows the modulation); what the sound does (PRES, the needle and the car, the rods, the fire) the live ones
 * (opus_engine_live), as the OS's own engine screens move with it.
 *
 *   year  (blue)    the time circuits, each row the OP-1's own month, day and time (opus_clock, core 2.4; only
 *                   the year changes with the knob): DEST the destination, PRES
 *                   the first arm's year as the time drift moves it (ota_arm_year), LAST the destination before the
 *                   last change; PRES a darker shade of the green
 *                   encoder's colour
 *   speed (green)   the speedometer (its arc and digits the knob, the needle the car's speed), the road, the wheels
 *                   (their spokes in the first encoder's colour, as v3's)
 *   power (white)   the flux capacitor: its rods are the three arms, each glowing with power and, while a note plays,
 *                   breathing at its place in the time drift; the readout in gigawatts; the current along all three
 *                   time cables, at one speed, fewer pulses on a shorter cable, none where a cable passes behind the body
 *   trail (orange)  the fire: once the needle reaches the set speed, as high as trail while a key is
 *                   down, burning down after the last one over the trail's time (40 ms to 1.4 s, the ignition's), new
 *                   flames only while the needle stays there; each flame's colour, glow and size fade with its age, its
 *                   flicker blended between draws, its intensity kept while notes are held down; the white-hot
 *                   core fades out with its age and its height, so the small flames inside the big one fade too; a
 *                   trail half as long again as v3's
 *   shift           spread, tone and flux on the blue, green and white tabs; the overdrive's type on trail's (1.2:
 *                   shift + orange turns it)
 *
 * The plugin's setting "always driving" (outatime.c) drives the car with no key down: at the set speed, its fire at
 * trail's height, the current running.
 *
 * The static art (art/page.js -> ota_art.h, written to /mod/opus.outatime/t1.svg) is drawn once and its pixels kept;
 * the parts that change with a setting (the rows, the meter, the readout, the tabs' names) are drawn when they change
 * and kept too (zones); every frame copies them back and draws what moves (the road, the fire, the wheels, the needle,
 * the rods, the current) with the core's smoothed lines (path.c: far cheaper than the OS's renderer). The animation
 * runs on the cycle counter (400 MHz) as v3's preview runs on the clock. The colours follow the encoders'
 * (the user's picks, a theme): everything is made again when one changes. Its cost, a frame on rev 2 with the cache
 * timing (test_outatime_page, 2026-10-26): parked 0.93 M cycles, a note held 1.24 M, flat out 1.05 M, the worst frame
 * 3.5 M (the year crossing 1970 or 2000: every row's fields at once), the art made 21.8 M once; 28.5 frames a second
 *, so 0.93 M is about 6.6 % of the CPU. */
#include "opus_plugin.h"
#include "op1fw.h"
#include "ota_dsp.h"
#include "ota_page.h"
#include "ota_art.h"

typedef unsigned short u16;
typedef unsigned int u32;

#define U(v)            ((int)((v) * 256 + 0.5))   /* units x 256 (constants only: folded by the compiler) */
#define LAYER_WORDS     (20 * 1024)        /* the kept art: 13,731 words at most (rev 1; rev 2 13,537, the FE 11,638) */
#define ROW_WORDS       3072               /* the meter */
#define FIELD_WORDS     1536               /* a time circuits row's field */
#define TAB_WORDS       1024               /* a tab's name, the readout */
#define FLAMES          40
#define FLAME_GAP       U(6.5)             /* a flame every 6.5 units the car drives (v3) */
#define FLAME_LIFE      2250               /* ms: a flame left behind burns out by then (the car stopped) */
#define THIRD           0x55555555u

/* the stock palette (colors.svg) */
#define DKBLUE          0x383572u
#define OFFBLUE         0x1D2040u
#define BLUE            0x698EFFu
#define LIGHTBLUE       0xBFD8FFu
#define LAVENDER        0xADADFFu
#define GREY            0x7C7990u
#define WHITE           0xFFFFFFu

/* the zones: four tabs, each time circuits row in three fields (the date, the year, the time: the drift redraws the
 * PRES row's year alone), the meter, the readout */
enum { Z_TRAIL, Z_SPEED, Z_YEAR, Z_POWER, Z_ROWS, Z_METER = Z_ROWS + 9, Z_READ, Z_COUNT };
struct zone {
    int x0, y0, x1, y1;                    /* units x 256 */
    unsigned words;
    u16 *runs;
    int used;                              /* -1: draw it again */
    char key[24];
};
static struct zone zones[Z_COUNT];

static int art = -1;
static unsigned char *e4_slots[64], *e2_slots[4], *cable_slots[32];
static int e4_n = -1, e2_n = -1, cable_n = -1;
static u16 *layer;
static int layer_used = -1, made_rows;
static unsigned made_accent[4];
static int rows_;
static u16 *buf_;

/* the animation */
static unsigned last_cycles, now_ms;
static int act;                            /* Q15: how much the car drives (up in 0.25 s with a key, down in 0.7 s) */
static int road, wheel, streak, emit;      /* units x 256 driven (the dashes, the streaks, since the last flame);
                                              wheel: the wheels' turn, 1/4096 */
static int cable_d[CABLES];                /* the current: units x 256 along each cable, its pulses' first */
static struct flame { int x, lv, L; unsigned id, born; } flame[FLAMES];
static int flames;
static unsigned flame_id;
static int dest_year = -1, last_year = -1, moving, moved_ms;   /* LAST: the destination before the last change */
static const void *lit_voice;              /* the note whose fire is lit, and its ignition a frame ago */
static int lit_env, fire_lv;               /* fire_lv: Q15, the fire's height (trail while held, then burning down) */
static int cable_at[CABLE_N], cable_len[CABLES];   /* units x 256 along its cable to each point; each one's length */
static int at_speed;                       /* the needle at the set speed (the fire), a frame ago */
static int rod_u[3][2];                    /* each rod's direction, Q12 */
static u32 rs;                             /* v3's xorshift */

volatile unsigned ota_page_frames, ota_page_cycles, ota_page_cycles_max, ota_page_make_cycles;
volatile int ota_page_flames;              /* the flames burning (the tests) */
volatile int ota_page_layer_used = -1;

/* ---------------------------------------------------------------- helpers */

static inline unsigned cycles(void)
{
    unsigned c;
    __asm__ volatile ("%0 = CYCLES;" : "=d"(c));
    return c;
}

static unsigned isqrt(unsigned v)
{
    unsigned r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

static int clampk(int v) { return v < 0 ? 0 : v > 32767 ? 32767 : v; }
static int rnd(void)                       /* Q15 */
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return (int)(rs >> 17);
}
static void seed(u32 s) { rs = s ? s : 0x9E3779B9u; }

static unsigned accent(int i) { return opus_accent_now(i); }

static unsigned shade(unsigned rgb, int f256)
{
    return ((((rgb >> 16) & 0xFF) * f256 >> 8) << 16) | ((((rgb >> 8) & 0xFF) * f256 >> 8) << 8) | ((rgb & 0xFF) * f256 >> 8);
}

static unsigned mix(unsigned a, unsigned b, int t256)
{
    unsigned out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        const int ca = (int)((a >> sh) & 0xFF), cb = (int)((b >> sh) & 0xFF);
        out |= (unsigned)(ca + ((cb - ca) * t256 >> 8)) << sh;
    }
    return out;
}

/* v3's ramps: colours at stops (Q15), t between them */
static unsigned ramp(const unsigned *col, const int *at, int n, int t)
{
    for (int i = 1; i < n; ++i)
        if (t <= at[i])
            return mix(col[i - 1], col[i], ((t - at[i - 1]) << 8) / (at[i] - at[i - 1]));
    return col[n - 1];
}
static unsigned rod_col(int c)             /* the rods: dark blue, blue, light blue, white hot */
{
    static const unsigned col[4] = { DKBLUE, BLUE, LIGHTBLUE, WHITE };
    static const int at[4] = { 0, 13107, 26214, 32767 };
    return ramp(col, at, 4, c);
}
static unsigned rod_halo(int c)
{
    static const unsigned col[3] = { OFFBLUE, DKBLUE, BLUE };
    static const int at[3] = { 0, 16384, 32767 };
    return ramp(col, at, 3, c);
}

static void ln(int x0, int y0, int x1, int y1, int w, unsigned rgb)
{
    opus_page_stroke_line(x0, y0, x1, y1, w, OPUS_RGB(rgb));
}
static void dot(int x, int y, int r, unsigned rgb) { ln(x, y, x, y, 2 * r, rgb); }   /* a point: a dot as wide */

static int px(int u) { return u * rows_ / (320 * 256); }

static void sincos(int turn, int *s, int *c)   /* 1/4096 of a turn, any; x 4096 */
{
    turn &= 4095;
    opus_sincos(turn > 2048 ? turn - 4096 : turn, s, c);
}

static int pow07(int x)                    /* Q15: x^0.7 (v3's flame height over its life) */
{
    static const unsigned short T[17] = { 0, 4706, 7645, 10152, 12416, 14516, 16492, 18370, 20172, 21906, 23583,
                                          25208, 26791, 28334, 29845, 31320, 32768 };
    x = clampk(x);
    const int i = x >> 11, f = x & 2047;
    return T[i] + ((T[i + 1] - T[i]) * f >> 11);
}

static void two(char *p, int v) { p[0] = (char)('0' + v / 10 % 10); p[1] = (char)('0' + v % 10); }

static int same_key(const char *a, const char *b)
{
    int i = 0;
    while (i < 23 && a[i] && a[i] == b[i])
        ++i;
    return i == 23 || a[i] == b[i];
}

/* ---------------------------------------------------------------- zones: drawn when their key changes, kept */

static void zone_at(int z, int x0, int y0, int x1, int y1, unsigned words, u16 *mem)
{
    zones[z].x0 = x0;
    zones[z].y0 = y0;
    zones[z].x1 = x1;
    zones[z].y1 = y1;
    zones[z].words = words;
    zones[z].runs = mem;
    zones[z].used = -1;
}

/* 1: its pixels copied back (the key the same); 0: the caller draws it, then keeps it */
static int zone_put(int z, const char *key)
{
    struct zone *const q = &zones[z];
    if (q->used < 0 || !same_key(key, q->key))
        return 0;
    opus_page_put(q->runs, q->used);
    return 1;
}

static void zone_keep(int z, const char *key)
{
    struct zone *const q = &zones[z];
    int i = 0;
    q->used = opus_page_keep(q->runs, q->words, px(q->x0), px(q->x1), px(q->y0), px(q->y1));
    for (; i < 23 && key[i]; ++i)
        q->key[i] = key[i];
    q->key[i] = 0;
}

/* ---------------------------------------------------------------- the letters and segments */

static int glyph_of(char ch)
{
    for (int i = 0; GLYPH_CHARS[i]; ++i)
        if (GLYPH_CHARS[i] == ch)
            return i;
    return -1;
}

/* The OP-1's letters (opfont's strokes, the tabs' names): s from x, y its cap's top left, cap high, w the stroke */
static void text(const char *s, int x, int y, int cap, int w, unsigned rgb)
{
    int cx = x, prev = 0;
    for (; *s; ++s) {
        if (prev)
            cx += GLYPH_GAP * cap >> 12;
        prev = 1;
        if (*s == ' ') {
            cx += GLYPH_SPACE * cap >> 12;
            continue;
        }
        const int g = glyph_of(*s);
        if (g < 0)
            continue;
        const short *p = GLYPH + GLYPH_AT[g];
        const int gw = *p++;
        for (int n; (n = *p++) != 0; p += 2 * n) {
            int lx = cx + (p[0] * cap >> 12), ly = y + (p[1] * cap >> 12);
            if (n == 1)
                dot(lx, ly, w / 2, rgb);
            for (int i = 1; i < n; ++i) {
                const int nx = cx + (p[2 * i] * cap >> 12), ny = y + (p[2 * i + 1] * cap >> 12);
                ln(lx, ly, nx, ny, w, rgb);
                lx = nx;
                ly = ny;
            }
        }
        cx += gw * cap >> 12;
    }
}

struct segs { int w, h, sw, slant, glow_w; unsigned lit, glow; };   /* slant: Q12 of h, the top leaning right */

/* A segment character (op1draw.js's segChar): its cell's top left at x, y; the lit segments, each with a glow under
 * it (glow_w 0: none), every end pulled in by 0.95 of the stroke. */
static void seg_char(int ch, int x, int y, int fourteen, const struct segs *o)
{
    const signed char (*t)[4] = fourteen ? SEG14 : SEG7;
    const int n = fourteen ? 14 : 7;
    unsigned m = 0;
    if (ch >= '0' && ch <= '9')
        m = fourteen ? MAP14_DIGIT[ch - '0'] : MAP7[ch - '0'];
    else if (fourteen && ch >= 'A' && ch <= 'Z')
        m = MAP14_ALPHA[ch - 'A'];
    for (int pass = o->glow_w ? 0 : 1; pass < 2; ++pass)
        for (int k = 0; k < n; ++k) {
            if (!(m >> k & 1))
                continue;
            const int x1 = x + t[k][0] * o->w / 2 + (((2 - t[k][1]) * o->h / 2) * o->slant >> 12), y1 = y + t[k][1] * o->h / 2;
            const int x2 = x + t[k][2] * o->w / 2 + (((2 - t[k][3]) * o->h / 2) * o->slant >> 12), y2 = y + t[k][3] * o->h / 2;
            const int dx = x2 - x1, dy = y2 - y1, L = (int)isqrt((unsigned)(dx * dx + dy * dy));
            int g = o->sw * 95 / 100;
            if (g > L * 3 / 10)
                g = L * 3 / 10;
            const int ox = L ? dx * g / L : 0, oy = L ? dy * g / L : 0;
            if (pass)
                ln(x1 + ox, y1 + oy, x2 - ox, y2 - oy, o->sw, o->lit);
            else
                ln(x1 + ox, y1 + oy, x2 - ox, y2 - oy, o->glow_w, o->glow);
        }
}

/* ---------------------------------------------------------------- the instruments */

/* A time circuits row's field f of "MMMDDYYYYAPHHMM": 0 the date (MMMDD), 1 the year, 2 the time (APHHMM) */
static const unsigned char FIELD_AT[4] = { 0, 5, 9, 15 };

static void row(int r, int f, const char *s, unsigned lit, unsigned glow)
{
    const int y = r == 0 ? TC_ROW0 : r == 1 ? TC_ROW1 : TC_ROW2;
    const struct segs o14 = { TC_W14, TC_H, TC_SW, 205, glow ? TC_SW * 23 / 10 : 0, lit, glow };
    const struct segs o7 = { TC_W7, TC_H, TC_SW, 205, glow ? TC_SW * 23 / 10 : 0, lit, glow };
    static const int at[15] = { TC_MON, TC_MON + TC_PITCH, TC_MON + 2 * TC_PITCH, TC_DAY, TC_DAY + TC_PITCH, TC_YEAR,
                                TC_YEAR + TC_PITCH, TC_YEAR + 2 * TC_PITCH, TC_YEAR + 3 * TC_PITCH, TC_AMPM,
                                TC_AMPM + TC_PITCH, TC_HOUR, TC_HOUR + TC_PITCH, TC_MIN, TC_MIN + TC_PITCH };
    for (int i = FIELD_AT[f]; i < FIELD_AT[f + 1]; ++i) {
        const int ch = s[i];
        if (i < 3 || i == 9 || i == 10)    /* the letters: fourteen segments */
            seg_char(ch, at[i], y, 1, &o14);
        else
            seg_char(ch, ch == '1' ? at[i] - TC_W7 * 45 / 100 : at[i], y, 0, &o7);
    }
    if (f == 2) {
        dot(TC_MIN - U(2.6), y + TC_COLON1, U(0.85), lit);
        dot(TC_MIN - U(2.6), y + TC_COLON2, U(0.85), lit);
    }
}

/* A row: each field drawn again only when it changed */
static void row_zones(int r, const char *s, unsigned lit, unsigned glow)
{
    for (int f = 0; f < 3; ++f) {
        char key[8];
        int n = 0;
        for (int i = FIELD_AT[f]; i < FIELD_AT[f + 1]; ++i)
            key[n++] = s[i];
        key[n] = 0;
        if (!zone_put(Z_ROWS + 3 * r + f, key)) {
            row(r, f, s, lit, glow);
            zone_keep(Z_ROWS + 3 * r + f, key);
        }
    }
}

/* A year as the time circuits show it: the OP-1's month, day and time (twelve hours, as the films' circuits) */
static void stamp(int year, const struct opus_clock *c, char *s)
{
    static const char MON[] = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";
    const int m = c->month >= 1 && c->month <= 12 ? c->month - 1 : 0, h12 = c->hour % 12 ? c->hour % 12 : 12;
    for (int i = 0; i < 3; ++i)
        s[i] = MON[3 * m + i];
    two(s + 3, c->day);
    s[5] = (char)('0' + year / 1000);
    s[6] = (char)('0' + year / 100 % 10);
    two(s + 7, year % 100);
    s[9] = c->hour < 12 ? 'A' : 'P';
    s[10] = 'M';
    two(s + 11, h12);
    two(s + 13, c->minute);
    s[15] = 0;
}

static void arc(int a0, int a1, int r, int w, unsigned rgb)   /* the meter's arc, 1/4096 of a turn, counter-clockwise */
{
    int n = ((a1 > a0 ? a1 - a0 : a0 - a1) + 56) / 57, lx = 0, ly = 0;
    n = n < 1 ? 1 : n;
    for (int i = 0; i <= n; ++i) {
        int s, c;
        sincos(a0 + (a1 - a0) * i / n, &s, &c);
        const int x = GA_CX + (c * r >> 12), y = GA_CY - (s * r >> 12);
        if (i)
            ln(lx, ly, x, y, w, rgb);
        lx = x;
        ly = y;
    }
}

static int meter_angle(int v) { return GA_A0 + ((GA_A1 - GA_A0) * clampk(v) >> 15); }

static void meter(int mph, unsigned lit, unsigned glow)
{
    const int drv = mph * 32767 / 88;
    if (drv > 164) {
        arc(GA_A0, meter_angle(drv), GA_R, U(3.4), glow);
        arc(GA_A0, meter_angle(drv), GA_R, U(1.5), lit);
    }
    const struct segs o = { MPH_W, MPH_H, U(1.5), 328, U(1.5) * 24 / 10, lit, glow };
    seg_char('0' + mph / 10 % 10, MPH_X, MPH_Y, 0, &o);
    seg_char('0' + mph % 10, MPH_X + MPH_PITCH, MPH_Y, 0, &o);
}

static void readout(int gw100, unsigned lit, unsigned glow)   /* gigawatts: d.dd (segText's point between cells) */
{
    const struct segs o = { READ_W, READ_H, U(1.4), 328, U(1.4) * 23 / 10, lit, glow };
    const int r = U(1.4) * 62 / 100;
    int cx = READ_X;
    seg_char('0' + gw100 / 100 % 10, cx, READ_Y, 0, &o);
    cx += READ_PITCH;
    dot(cx - READ_PITCH * 18 / 100, READ_Y + READ_H, 2 * r, glow);
    dot(cx - READ_PITCH * 18 / 100, READ_Y + READ_H, r, lit);
    cx += READ_PITCH * 36 / 100;
    seg_char('0' + gw100 / 10 % 10, cx, READ_Y, 0, &o);
    seg_char('0' + gw100 % 10, cx + READ_PITCH, READ_Y, 0, &o);
}

static void tab_name(int i, const char *s, unsigned rgb)
{
    text(s, TAB[i][0] + TAB_PAD, TAB[i][1] - TAB_CAP / 2, TAB_CAP, U(1.3), rgb);
}

/* ---------------------------------------------------------------- what moves */

/* A cubic from (x0, y0), in four lines (a polyline: the core joins them) */
static void cubic(int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3, int w, unsigned rgb)
{
    int lx = x0, ly = y0;
    for (int i = 1; i <= 4; ++i) {
        const int u = 4 - i, a = u * u * u, b = 3 * u * u * i, c = 3 * u * i * i, d = i * i * i;
        const int x = (a * x0 + b * x1 + c * x2 + d * x3) / 64, y = (a * y0 + b * y1 + c * y2 + d * y3) / 64;
        ln(lx, ly, x, y, w, rgb);
        lx = x;
        ly = y;
    }
}

static void tongue(int x, int gy, int h, int w, int lean, int lw, unsigned rgb)   /* a flame (v3's) */
{
    const int xl = x - w / 2, xr = x + w / 2, tx = x + lean, ty = gy - h;
    cubic(xl, gy, xl, gy - h * 55 / 100, tx - w * 18 / 100, gy - h * 82 / 100, tx, ty, lw, rgb);
    cubic(tx, ty, tx + w * 12 / 100, gy - h * 72 / 100, xr, gy - h / 2, xr, gy, lw, rgb);
}

/* Cable c at d (units x 256 from its start): the point, and where the visible stretch it is on ends; -1 when d is on a
 * hidden one (the cable behind the body) */
static int cable_point(int c, int d, int *x, int *y)
{
    int i = CABLE_AT[c] + 1;
    while (i < CABLE_AT[c + 1] - 1 && cable_at[i] < d)
        ++i;
    const int a = cable_at[i - 1], b = cable_at[i], u = b > a ? ((d - a) << 12) / (b - a) : 0;
    *x = CABLE[i - 1][0] + ((CABLE[i][0] - CABLE[i - 1][0]) * u >> 12);
    *y = CABLE[i - 1][1] + ((CABLE[i][1] - CABLE[i - 1][1]) * u >> 12);
    if (!CABLE[i][2])
        return -1;
    while (i < CABLE_AT[c + 1] - 1 && CABLE[i + 1][2])
        ++i;
    return cable_at[i];
}

/* The clock: the time since the last frame (at most 250 ms, v3's), the car's drive, the road, the fire */
static int step(int held, int speed01, int lv, int dt)
{
    const int target = held ? 32767 : 0, tau = held ? 250 : 700;
    const int x = (dt << 15) / tau, f = x - (x * x >> 16) + ((x * x >> 15) * x >> 15) / 6;   /* 1 - e^-x */
    act += (target - act) * (f > 32767 ? 32767 : f) >> 15;
    if (!held && act < 66)
        act = 0;
    const int vel = U(26) + ((U(270) >> 4) * speed01 >> 11);   /* units x 256 a second along the road */
    const int dx = act ? vel * dt / 1000 : 0;
    road = (road + dx) % U(26);
    wheel = (wheel - (dx * 869 >> 12)) & 4095;   /* v3: a radian each 12 units */
    streak = (streak + dx) % U(1700);
    if (act) {                             /* v3's 0.4 + 1.2 x speed passes a second of the longest, on every cable */
        const int adv = (cable_len[0] * (410 + (1229 * speed01 >> 15)) >> 10) * dt / 1000;
        for (int c = 0; c < CABLES; ++c)
            cable_d[c] = (cable_d[c] + adv) % cable_len[c];
    }
    int n = 0;
    for (int i = 0; i < flames; ++i) {
        struct flame f1 = flame[i];
        f1.x += dx;
        const int age = ((f1.x - EMIT_X) << 7) / (f1.L >> 8) + (int)((now_ms - f1.born) * 32768 / FLAME_LIFE);
        if (age < 32768 && f1.x < U(322))
            flame[n++] = f1;
    }
    flames = n;
    emit += dx;
    while (emit >= FLAME_GAP) {
        emit -= FLAME_GAP;
        if (lv > 655 && flames < FLAMES) {
            struct flame *const f1 = &flame[flames++];
            f1->x = EMIT_X + emit;
            f1->lv = lv;
            f1->L = U(30) + ((U(225) >> 4) * lv >> 11);   /* units driven to burn out: v3's 20 + 150 x trail, x 1.5 */
            f1->id = flame_id++;
            f1->born = now_ms;
        }
    }
    return dx;
}

/* A flame's flicker (v3's: 18 new draws a second), blended from one draw to the next so it moves, not jumps: its
 * height 0.72-1 (Q15) and lean (units x 256) */
static void flicker(const struct flame *f, int *size, int *lean)
{
    const unsigned t = now_ms * 18, n = t / 1000, u = (t % 1000) * 32 / 1000;   /* u: 0-31 between two draws */
    int a[2], b[2];
    for (int k = 0; k < 2; ++k) {
        seed(f->id * 7919u + n + (unsigned)k);
        a[k] = rnd();
        b[k] = rnd();
    }
    const int r1 = a[0] + ((a[1] - a[0]) * (int)u >> 5), r2 = b[0] + ((b[1] - b[0]) * (int)u >> 5);
    *size = 23593 + (9175 * r1 >> 15);
    *lean = ((r2 - 16384) * U(2.2) >> 15) + U(1.2);
}

static void fire(unsigned a3)
{
    static const int at[4] = { 0, 9830, 21299, 32767 };   /* the outline: young, burning, old, gone */
    const unsigned col[4] = { a3, shade(a3, 200), shade(a3, 77), 0 };
    static const int hot_at[3] = { 0, 6554, 13107 };
    const unsigned halo = shade(a3, 77), hot[3] = { mix(a3, WHITE, 154), a3, 0 };
    for (int i = 0; i < flames; ++i) {
        const struct flame *const f = &flame[i];
        const int age = clampk(((f->x - EMIT_X) << 7) / (f->L >> 8) + (int)((now_ms - f->born) * 32768 / FLAME_LIFE));
        const int life = 32767 - age;
        int size, lean;
        flicker(f, &size, &lean);
        const unsigned grown = now_ms - f->born;           /* a new flame grows in over 60 ms */
        const int base = U(4) + (U(13) * f->lv >> 15);
        int h = ((base * pow07(life) >> 15) * size) >> 15;
        if (grown < 60)
            h = h * (int)grown / 60;
        const int w = U(3.6) + (U(2.6) * life >> 15);
        if (h < U(0.4))
            continue;
        const int gy = GROUND - U(0.6);
        if (age < 19661)                                   /* its glow, fading out by 0.6 */
            tongue(f->x, gy, h, w, lean, U(3), mix(halo, 0, (age << 8) / 19661));
        tongue(f->x, gy, h, w, lean, U(1.15), ramp(col, at, 4, age));
        if (age < 13107 && h > U(2.5)) {                   /* its white-hot core: the accent by 0.2, out by 0.4 */
            const int small = h < U(4) ? ((U(4) - h) << 8) / (U(4) - U(2.5)) : 0;   /* and out as it shrinks to 2.5 */
            tongue(f->x + U(0.2), gy, h / 2, w * 45 / 100, lean * 6 / 10, U(1), mix(ramp(hot, hot_at, 3, age), 0, small));
        }
    }
}

/* ---------------------------------------------------------------- the art, made once */

static void clear(void)
{
    u32 *p = (u32 *)buf_, *e = (u32 *)(buf_ + 256 * rows_);
    while (p < e)
        *p++ = 0;
}

static void make(void)
{
    clear();
    opus_art_set_colour(e4_slots, e4_n, accent(3));
    opus_art_set_colour(e2_slots, e2_n, shade(accent(1), 77));
    opus_art_set_colour(cable_slots, cable_n, shade(accent(0), 154));   /* the cables' glow: the first encoder's */
    opus_art_draw(art);
    layer_used = opus_page_keep(layer, LAYER_WORDS, 0, rows_ - 1, 0, rows_ / 2 - 1);
    ota_page_layer_used = layer_used;
    made_rows = rows_;
    for (int i = 0; i < 4; ++i)
        made_accent[i] = accent(i);
    for (int z = 0; z < Z_COUNT; ++z)
        zones[z].used = -1;
}

int ota_page_init(struct opl_self *self)
{
    u16 *m = (u16 *)opus_alloc(self, 2 * (LAYER_WORDS + 9 * FIELD_WORDS + ROW_WORDS + 5 * TAB_WORDS));
    if (!m)
        return 0;
    layer = m;
    m += LAYER_WORDS;
    for (int i = 0; i < 4; ++i, m += TAB_WORDS)
        zone_at(Z_TRAIL + i, TAB[i][0], TAB[i][1] - TAB_H, TAB[i][0] + TAB[i][2], TAB[i][1] + TAB_H, TAB_WORDS, m);
    /* the fields' edges in the gaps between them: a field's kept pixels never hold its neighbour's */
    static const int edge[4] = { TC_MON - U(2), TC_YEAR - U(3), TC_AMPM - U(2.5), TC_X1 - U(2) };
    for (int r = 0; r < 3; ++r) {
        const int y = r == 0 ? TC_ROW0 : r == 1 ? TC_ROW1 : TC_ROW2;
        for (int f = 0; f < 3; ++f, m += FIELD_WORDS)
            zone_at(Z_ROWS + 3 * r + f, edge[f], y - U(2), edge[f + 1] - 1, y + TC_H + U(2), FIELD_WORDS, m);
    }
    zone_at(Z_METER, GA_CX - GA_R - U(4), GA_CY - GA_R - U(4), GA_CX + GA_R + U(4), MPH_Y + MPH_H + U(3), ROW_WORDS, m);
    m += ROW_WORDS;
    zone_at(Z_READ, READ_X0, READ_Y0, READ_X1, READ_Y1, TAB_WORDS, m);
    for (int c = 0; c < CABLES; ++c) {
        cable_at[CABLE_AT[c]] = 0;
        for (int i = CABLE_AT[c] + 1; i < CABLE_AT[c + 1]; ++i) {
            const int dx = CABLE[i][0] - CABLE[i - 1][0], dy = CABLE[i][1] - CABLE[i - 1][1];
            cable_at[i] = cable_at[i - 1] + (int)isqrt((unsigned)(dx * dx + dy * dy));
        }
        cable_len[c] = cable_at[CABLE_AT[c + 1] - 1];
    }
    for (int k = 0; k < 3; ++k) {
        const int dx = FX_JX - FX_EL[k][0], dy = FX_JY - FX_EL[k][1], L = (int)isqrt((unsigned)(dx * dx + dy * dy));
        rod_u[k][0] = (dx << 12) / L;
        rod_u[k][1] = (dy << 12) / L;
    }
    art = opus_art_text(self, "t1.svg", ota_art, sizeof ota_art - 1);
    return art >= 0;
}

/* ---------------------------------------------------------------- a frame */

int ota_page_frame(const short *set, const short *k, int shift, int held, const struct ota_voice *v, int shape)
{
    if (art < 0 || !opus_art_ready(art))
        return 0;
    int rows;
    u16 *const buf = opus_page_pixels(&rows);
    if (!buf || (rows != 320 && rows != 376 && rows != 400))
        return 0;
    const unsigned t0 = cycles();
    rows_ = rows;
    buf_ = buf;
    if (e4_n < 0)
        e4_n = opus_art_colour_slots(art, ART_E4_RGB, e4_slots, 64);
    if (e2_n < 0)
        e2_n = opus_art_colour_slots(art, ART_E2DIM_RGB, e2_slots, 4);
    if (cable_n < 0)
        cable_n = opus_art_colour_slots(art, ART_CABLE_RGB, cable_slots, 32);
    int same = layer_used >= 0 && made_rows == rows, made = 0;
    for (int i = 0; i < 4 && same; ++i)
        same = made_accent[i] == accent(i);
    if (!same) {
        make();
        made = 1;
    } else
        opus_page_put(layer, layer_used);
    const unsigned a0 = accent(0), a1 = accent(1), a2 = accent(2), a3 = accent(3);

    /* the clock and the settings */
    const unsigned c = cycles();
    int dt = last_cycles ? (int)((c - last_cycles) / 400000u) : 0;
    dt = dt > 250 ? 250 : dt;
    last_cycles = c;
    now_ms += (unsigned)dt;
    const int spd = clampk(k[OTA_SPEED]), power = clampk(k[OTA_POWER]), trail = clampk(k[OTA_TRAIL]);
    const int speed01 = act * (4915 + (spd * 27853 >> 15)) >> 15;   /* v3: 0.15 + 0.85 x the knob, while driving */
    const int env = v ? clampk(v->env) : 0;
    const int mph = ota_mph(set[OTA_SPEED]), drv = mph * 32767 / 88;   /* the needle's top: the set speed */
    const int there = act > 655 && speed01 * 50 > drv * 49;   /* the needle at it (within 2 %): the fire */
    if ((v && (v != lit_voice || env > lit_env)) || (there && !at_speed))
        emit = FLAME_GAP;                  /* a note, or the needle there: the first flame at once */
    at_speed = there;
    lit_voice = v;
    lit_env = env;
    if (held)
        fire_lv = trail;
    else if (fire_lv) {                    /* burning down: 40 ms to 1.4 s with trail, as the ignition (ota_dsp.c) */
        const int tau = 40 + (1360 * trail >> 15), x = (dt << 15) / tau;
        const int f = x - (x * x >> 16) + ((x * x >> 15) * x >> 15) / 6;
        fire_lv -= fire_lv * (f > 32767 ? 32767 : f) >> 15;
        fire_lv = fire_lv < 164 ? 0 : fire_lv;
    }
    step(held, speed01, there ? fire_lv : 0, dt);
    const int year = ota_year(set[OTA_YEAR]);
    if (dest_year < 0)
        dest_year = year;
    if (year != dest_year) {
        if (!moving)
            last_year = dest_year;
        moving = 1;
        dest_year = year;
        moved_ms = (int)now_ms;
    } else if (moving && (int)now_ms - moved_ms > 1000)
        moving = 0;

    /* the zones */
    char key[24];
    static const char *const SHAPE[4] = { "WARM", "HARD", "TUBE", "FOLD" };   /* the overdrive's types (ota_dsp.h) */
    if (shift) {                           /* DRIVE and the type: the zone's key is the tab's text */
        const char *p = "DRIVE ", *q = SHAPE[shape & 3];
        int n = 0;
        while (*p)
            key[n++] = *p++;
        while (*q)
            key[n++] = *q++;
        key[n] = 0;
    } else {
        key[0] = 'T', key[1] = 'R', key[2] = 'A', key[3] = 'I', key[4] = 'L', key[5] = ' ';
        two(key + 6, (clampk(set[OTA_TRAIL]) * 99 + 16383) / 32767);
        key[8] = 0;
    }
    if (!zone_put(Z_TRAIL, key)) {
        tab_name(0, key, a3);
        zone_keep(Z_TRAIL, key);
    }
    static const char *const NAME[3] = { "SPEED", "YEAR", "POWER" }, *const SHIFTED[3] = { "TONE ", "SPREAD ", "FLUX " };
    static const signed char KNOB[3] = { OTA_TONE, OTA_SPREAD, OTA_FLUX }, ENC[3] = { 1, 0, 2 };
    for (int i = 0; i < 3; ++i) {
        int n = 0;
        if (shift) {
            for (const char *p = SHIFTED[i]; *p; ++p)
                key[n++] = *p;
            two(key + n, (clampk(set[KNOB[i]]) * 99 + 16383) / 32767);
            n += 2;
        } else
            for (const char *p = NAME[i]; *p; ++p)
                key[n++] = *p;
        key[n] = 0;
        if (!zone_put(Z_SPEED + i, key)) {
            tab_name(1 + i, key, accent(ENC[i]));
            zone_keep(Z_SPEED + i, key);
        }
    }
    struct opus_clock now;
    opus_clock(&now);
    stamp(dest_year, &now, key);
    row_zones(0, key, a0, shade(a0, 77));
    stamp(v ? ota_arm_year(k, v->drift, 0) : year, &now, key);
    row_zones(1, key, shade(a1, 154), shade(a1, 46));   /* PRES: a darker green */
    stamp(last_year < 0 ? dest_year : last_year, &now, key);
    row_zones(2, key, GREY, 0);
    key[0] = 'M';
    two(key + 1, mph);
    key[3] = 0;
    if (!zone_put(Z_METER, key)) {
        meter(mph, a1, shade(a1, 77));
        zone_keep(Z_METER, key);
    }
    const int gw = ota_gw100(set[OTA_POWER]);
    key[0] = 'G';
    key[1] = (char)('0' + gw / 100);
    two(key + 2, gw);
    key[4] = 0;
    if (!zone_put(Z_READ, key)) {
        readout(gw, a2, shade(a2, 77));
        zone_keep(Z_READ, key);
    }

    /* the road under the car, the speed streaks */
    for (int x = road - U(26); x < U(330); x += U(26)) {
        const int x0 = x < U(2) ? U(2) : x, x1 = x + U(11) > U(318) ? U(318) : x + U(11);
        if (x1 > x0 + 128)
            ln(x0, GROUND + U(3.2), x1, GROUND + U(3.2), U(1), OFFBLUE);
    }
    if (speed01 > 14746 && act > 1638) {
        const int n = (5 * (speed01 - 14746) + 9011) / 18022;
        seed(7);
        for (int i = 0; i < n; ++i) {
            const int y = U(14) + (U(40) >> 4) * rnd() / 2048, len = U(10) + ((U(20) >> 4) * rnd() >> 11) * speed01 / 32768;
            const int kk = 6554 + (rnd() >> 3), x = U(160) + (((streak >> 4) * kk >> 8) + (U(400) >> 4) * rnd() / 2048) % U(170);
            if (x < U(312))
                ln(x, y, x + len > U(318) ? U(318) : x + len, y, U(1), DKBLUE);
        }
    }
    fire(a3);

    /* the cables' current, while a note plays: power's 1-3 pulses on the longest, as many a length on the others (at
     * least one), each 0.035 of the longest (v3's) and cut where its cable goes behind the body */
    if (act > 655 && power > 655) {
        const int n = 1 + (2 * power + 16384) / 32768, len = cable_len[0] * 36 >> 10;
        const unsigned pc = mix(a0, WHITE, 64 + (90 * act >> 15)), halo = shade(a0, 77);   /* the first's, lighter */
        for (int c = 0; c < CABLES; ++c) {
            const int m = (n * cable_len[c] + cable_len[0] / 2) / cable_len[0], nc = m < 1 ? 1 : m;
            for (int i = 0; i < nc; ++i) {
                const int d0 = (cable_d[c] + i * cable_len[c] / nc) % cable_len[c];
                int x0, y0, x1, y1;
                const int e = cable_point(c, d0, &x0, &y0);
                if (e < 0)
                    continue;
                cable_point(c, d0 + len < e ? d0 + len : e, &x1, &y1);
                ln(x0, y0, x1, y1, U(3), halo);
                ln(x0, y0, x1, y1, U(1.4), pc);
            }
        }
    }

    /* the wheels */
    for (int w = 0; w < 2; ++w)
        for (int j = 0; j < SPOKES; ++j) {
            int s, c2;
            sincos(SPOKE[w][j][0] + wheel, &s, &c2);
            ln(WHEEL[w][0] + (c2 * SPOKE[w][j][1] >> 12), WHEEL[w][1] + (s * SPOKE[w][j][1] >> 12),
               WHEEL[w][0] + (c2 * SPOKE[w][j][2] >> 12), WHEEL[w][1] + (s * SPOKE[w][j][2] >> 12), U(1), a0);
        }

    /* the needle: the car's speed, up to the knob's; a tremble flat out */
    {
        const int shown = speed01 < drv ? speed01 : drv;
        int a = meter_angle(shown), s, c2;
        if (there && drv) {
            sincos((int)((now_ms * 46725u) >> 10), &s, &c2);
            a += s * 15 >> 12;
        }
        sincos(a, &s, &c2);
        ln(GA_CX, GA_CY, GA_CX + (c2 * (GA_R - U(3.5)) >> 12), GA_CY - (s * (GA_R - U(3.5)) >> 12), U(1.3), a1);
        dot(GA_CX, GA_CY, U(1.8), LAVENDER);
    }

    /* the flux capacitor: each rod an arm, glowing with power; while a note plays it breathes at the arm's drift */
    {
        int glow[3], sum = 0;
        for (int r = 0; r < 3; ++r) {
            int s = 0, c2;
            if (v)
                sincos((int)(v->drift + (u32)r * THIRD) >> 20, &s, &c2);
            const int half = (2048 + s / 2) << 3;   /* Q15: 0.5 + 0.5 sin */
            glow[r] = power <= 164 ? 0 : power * (18022 + (14746 * (act * half >> 15) >> 15)) >> 15;
            sum += glow[r];
        }
        for (int r = 0; r < 3; ++r) {
            const int ex = FX_EL[r][0], ey = FX_EL[r][1];
            const int x0 = ex + (rod_u[r][0] * ROD_E >> 12), y0 = ey + (rod_u[r][1] * ROD_E >> 12);
            const int x1 = FX_JX - (rod_u[r][0] * ROD_J >> 12), y1 = FX_JY - (rod_u[r][1] * ROD_J >> 12);
            if (glow[r] > 3932)
                ln(x0, y0, x1, y1, U(4.8), rod_halo(glow[r]));
            ln(x0, y0, x1, y1, U(2.1), rod_col(glow[r]));
        }
        const int g = sum / 3;
        if (g > 3932)
            dot(FX_JX, FX_JY, U(3.2) + (U(1.6) * g >> 15), rod_halo(g));
        dot(FX_JX, FX_JY, U(1.7) + (U(0.9) * g >> 15), g > 19661 ? WHITE : rod_col(g));
    }

    const unsigned t = cycles() - t0;
    ota_page_cycles = t;
    if (made)
        ota_page_make_cycles = t;
    else if (t > ota_page_cycles_max)
        ota_page_cycles_max = t;
    ++ota_page_frames;
    ota_page_flames = flames;
    return 1;
}
