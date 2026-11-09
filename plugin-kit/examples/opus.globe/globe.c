/* opus.globe: the kit's example of the orbit3d library (docs/orbit3d.md). A plugin that uses orbit3d names it in its
 * manifest (`needs opus.orbit3d 0.2.5`), includes sdk/orbit3d.h, and draws inside a page's draw call:
 *
 *     o3_begin (the camera and the ink), the solids (o3_sphere: what hides lines), the lines, the 2D overlay, o3_end.
 *
 * FN + orange push opens a quick page (opus_on_quick) with a globe: its outline, latitude and longitude lines (the ones
 * on its far side removed by orbit3d), a tilted ring of ticks around it and a moon going round on the ring, which
 * hides what passes behind it. The camera turns a little every frame; blue turns it faster, slower or backwards, green
 * tilts it, white zooms. The ink is the blue encoder's colour as the user picked it, so the page follows their theme.
 *
 * Everything is fixed point: world points in 1/4096 of a unit (O3_ONE), angles in 1/4096 of a turn, places on the page
 * and the zoom in the drawing's pixels (of 320 x 160) x 256. */
#include "opus_plugin.h"
#include "op1fw.h"
#include "orbit3d.h"

#define KEY_ORANGE_PUSH 0x3E               /* the orange encoder's push */
#define PAGE            0                  /* this plugin's one quick page */
#define MERIDIANS       6
#define RING_R          (O3_ONE * 8 / 5)   /* the ring's radius: 1.6 units */
#define MOON_R          (O3_ONE / 6)

static struct opl_self *me;
static int yaw;                            /* the camera's turn now */
static int spin = 12;                      /* its turn a frame (blue) */
static int tilt = 300;                     /* its pitch, toward the viewer (green) */
static int zoom = 44 << 8;                 /* the drawing's pixels a world unit, x 256 (white) */
static int moon;                           /* the moon's angle on the ring */
static int pushed;                         /* the push's key-down was ours: its key-up too */
static struct o3_mat meridian[MERIDIANS];  /* each longitude circle's plane, made once */
static struct o3_mat ring;                 /* the ring's plane: tilted a little */

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int isqrt(int v)                    /* the whole square root (no floating point on the OP-1) */
{
    int r = 0, b = 1 << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

static void draw(int page)
{
    (void)page;
    yaw = (yaw + spin) & 4095;
    moon = (moon + 24) & 4095;
    const struct o3_vec origin = { 0, 0, 0 };

    /* The camera: it turns the world by yaw, tilts it by pitch; the globe's centre in the middle of the page. */
    o3_begin(yaw, tilt, zoom, 160 << 8, 72 << 8, OPUS_RGB(opus_accent_now(0)), 1);

    /* The solids first: every line after them is tested against them. The moon's place on the ring. */
    int s, c;
    opus_sincos(moon - 2048, &s, &c);
    const struct o3_vec on_ring = { RING_R * c / 4096, 0, RING_R * s / 4096 };
    struct o3_vec moon_at;
    o3_apply(&moon_at, &ring, &on_ring, &origin);
    const int globe = o3_sphere(&origin, O3_ONE, 1);
    o3_sphere(&moon_at, MOON_R, 1);

    /* Latitude lines: level circles at heights -0.6 to 0.6, each on the globe's own surface (skip: globe), so its
     * near half shows and its far half is hidden by the globe. */
    for (int k = -2; k <= 2; ++k) {
        const int y = k * O3_ONE * 3 / 10;
        const struct o3_vec at = { 0, y, 0 };
        o3_circle(0, &at, isqrt(O3_ONE * O3_ONE - y * y), 64, k ? O3_HAIR : O3_STD, O3_SOLID, globe);
    }
    /* Longitude lines: circles through both poles, in planes turned around the vertical. */
    for (int i = 0; i < MERIDIANS; ++i)
        o3_circle(&meridian[i], &origin, O3_ONE, 64, O3_HAIR, O3_SOLID, globe);

    /* The ring around it: dashed, with ticks, every fourth one longer. The globe and the moon hide its far side. */
    o3_circle(&ring, &origin, RING_R, 96, O3_STD, O3_DASH, -1);
    o3_ring_ticks(&ring, &origin, RING_R, 48, 4, O3_ONE / 10);

    /* The 2D overlay: orbit3d's own 5 x 7 font (capitals and digits), then how many segments this frame drew. */
    int hidden;
    const int shown = o3_drawn(&hidden);
    o3_text(8 << 8, 8 << 8, 2, "ORBIT3D");
    char line[24];
    fw_snprintf(line, sizeof line, "%d LINES", shown);
    o3_text(8 << 8, 132 << 8, 1, line);
    o3_end();
}

static int knob(int page, int encoder, int steps)
{
    (void)page;
    if (encoder == 1)
        spin = clamp(spin + steps * 4, -64, 64);
    else if (encoder == 2)
        tilt = clamp(tilt + steps * 32, -900, 900);
    else if (encoder == 3)
        zoom = clamp(zoom + steps * 256, 24 << 8, 60 << 8);
    return 1;                              /* the turns are the page's while it shows */
}

static int on_key(int key, int pressed)
{
    if (key != KEY_ORANGE_PUSH)
        return 0;
    if (!pressed) {                        /* the key-up of a push we took */
        const int ours = pushed;
        pushed = 0;
        return ours;
    }
    if (!opus_fn_held())
        return 0;
    pushed = 1;
    opus_quick_show(me, opus_quick_page(me) == PAGE ? -1 : PAGE);
    return 1;
}

int opl_init(struct opl_self *self)
{
    me = self;
    /* A longitude circle lies in a plane holding the vertical: a level circle's plane stood up (a quarter turn
     * around x), then turned around the vertical (y) to its longitude. */
    struct o3_mat stand, turn;
    o3_rotation(&stand, 0, -1024, 0);
    for (int i = 0; i < MERIDIANS; ++i) {
        o3_rotation(&turn, i * 2048 / MERIDIANS, 0, 0);
        o3_mul(&meridian[i], &turn, &stand);
    }
    o3_rotation(&ring, 0, 0, 220);         /* rolled a little: the ring leans */
    opus_on_key(self, on_key);
    opus_on_quick(self, draw, knob);
    return 0;
}
