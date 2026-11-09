# orbit3d: 3D line art for a plugin's page

**orbit3d** (`opus.orbit3d`, version 0.2.5, in Opus 2's starter pack) is a library plugin: other plugins call it to
draw 3D line art on their pages in the look of an OP-1 plate. One ink on black, constant line weights, an orthographic
camera, the lines behind a solid removed, and a dark gap (a halo) cut where a line passes in front of another. Since
0.2 it also has several inks blended over each other, filled faces, a 2D overlay (lines, dots, rectangles, ellipses, a
scope) and a 5 x 7 pixel font. SIM's page is drawn with it.

It draws its own smoothed pixels straight into the frame: a short line costs it a few hundred CPU cycles, where the
core's strokes (`opus_page_stroke_line`) cost about 6,000. Hundreds of lines a frame stay affordable.

The example `examples/opus.globe` uses most of it: a turning globe with its latitude and longitude lines, a ring of
ticks around it and a moon that hides what passes behind it.

## Contents

- [Using it](#using-it): the manifest line, the header, where it draws, the order of a frame
- [Numbers](#numbers): world units, angles, places on the page
- [The camera](#the-camera): `o3_begin`, `o3_end`
- [Turning things](#turning-things): `o3_rotation`, `o3_mul`, `o3_apply`
- [Solids](#solids): `o3_sphere`, `o3_sphere_fill`
- [Lines](#lines): `o3_line`, `o3_circle`, `o3_ring_ticks`, weights and styles
- [Marks and places](#marks-and-places): `o3_glyph`, `o3_project`, `o3_to_view`, `o3_to_page`, `o3_drawn`
- [Inks](#inks): `o3_ink`, `O3_OVER`
- [The 2D overlay](#the-2d-overlay): `o3_seg`, `o3_lines`, `o3_dot`, `o3_rect`, `o3_ellipse`, `o3_fill`,
  `o3_fill_alpha`, `o3_wave`, `o3_band`, `o3_text`, `o3_text_width`
- [Cost and limits](#cost-and-limits)
- [Versions](#versions)
- [The example: opus.globe](#the-example-opusglobe)

## Using it

1. **The manifest** names the library and its lowest version:

   ```
   needs     opus.orbit3d 0.2.5
   ```

   On the OP-1, `orbit3d.opmod` must be in the mods folder (the starter pack has it). Without it, or with an older
   one, the core refuses your plugin and the mods list says "needs opus.orbit3d 0.2.5" ([manifest.md](manifest.md),
   `needs`).
2. **The code** includes its header from the kit's `sdk/` folder:

   ```c
   #include "orbit3d.h"
   ```

   The build lists the `o3_*` names your plugin calls as imports; the core links them to orbit3d when it loads.
3. **Draw only inside a page's draw call**: an engine's, an effect's or an LFO type's page, a drum engine's page, a
   quick page ([api.md](api.md), "Pages", "Quick pages"). orbit3d draws into the frame through `opus_page_pixels`,
   which answers only there; elsewhere its calls draw nothing.
4. **A frame, in this order.** Every call draws at once; a line is tested against the solids given before it:

   ```c
   o3_begin(yaw, pitch, zoom, cx, cy, ink, halo);   /* the camera, the ink */
   o3_sphere(...);                                  /* the solids: what hides lines (up to 8) */
   o3_line(...); o3_circle(...); ...                /* the 3D lines */
   o3_ink(...); o3_seg(...); o3_text(...); ...      /* the 2D overlay */
   o3_end();
   ```

## Numbers

The OP-1 has no floating point unit, so orbit3d is fixed point throughout:

| what | unit | example |
|---|---|---|
| a world point | 1/4096 of a unit (`O3_ONE` = 4096 is 1.0) | `{ 0, O3_ONE / 2, 0 }` is half a unit up |
| a rotation | a 3 x 3 matrix in 1/4096, row major (`x' = m[0] x + m[1] y + m[2] z`, ...) | `o3_rotation` makes them |
| an angle | 1/4096 of a turn (1024 a quarter turn), as `opus_sincos` | 512 is 45 degrees |
| a place on the page | the drawing's pixels (of 320 x 160) x 256 | `160 << 8, 80 << 8` is the middle |
| the zoom | the drawing's pixels a world unit, x 256 | `40 << 8`: a unit is 40 pixels |
| a line's width, a dash | the panel's own pixels, the same on every OP-1 screen | |

The camera's space: x right, y up, z toward the viewer. The page's y runs down. Keep what you draw within y 2 to 146 of
the drawing (the OP-1's window hides the rows below).

## The camera

```c
void o3_begin(int yaw, int pitch, int zoom, int cx, int cy, int ink, int halo);
void o3_end(void);
```

`o3_begin` starts a frame's drawing: the camera turns the world by `yaw` (around the vertical), then tilts it by
`pitch` (toward the viewer); `zoom` is the drawing's pixels a world unit x 256; `(cx, cy)` is where the world's origin
lands on the page (the drawing's pixels x 256). `ink` is the colour: `OPUS_RGB(0xRRGGBB)` for orbit3d's own fast lines
(`OPUS_RGB(opus_accent_now(0))` follows the user's colours), or a colour id, which goes through the core's slower
strokes. `halo` 1 cuts the dark gaps under lines in front, 0 leaves them out. It also forgets the last frame's solids
and goes back to one ink (the plate's: the brightest of what a pixel had is kept, so no line darkens another).
`o3_end` ends the frame.

To turn the whole scene, change `yaw` and `pitch` each frame; to move one object, turn its points yourself
([Turning things](#turning-things)).

## Turning things

```c
struct o3_vec { int x, y, z; };
struct o3_mat { int m[9]; };
void o3_rotation(struct o3_mat *m, int yaw, int pitch, int roll);
void o3_mul(struct o3_mat *out, const struct o3_mat *a, const struct o3_mat *b);
void o3_apply(struct o3_vec *out, const struct o3_mat *m, const struct o3_vec *p, const struct o3_vec *at);
```

`o3_rotation` makes a matrix that turns by `yaw` (around y), then `pitch` (around x), then `roll` (around z).
`o3_mul` multiplies two (`out = a b`: `b`'s turn first, then `a`'s). `o3_apply` turns the point `p` by `m` and moves it
by `at` (0: not moved). Make the matrices that do not change once, in `opl_init`: orbit3d is loaded first, so its
calls work there.

## Solids

```c
int o3_sphere(const struct o3_vec *c, int r, int outline);
void o3_sphere_fill(int i);
```

`o3_sphere`: a sphere (centre `c`, radius `r` in world units x 4096) that hides the lines behind it from here on; its
outline is drawn (heavy) unless `outline` is 0. Up to 8 a frame. It returns the sphere's index (for `o3_sphere_fill`
and a line's `skip`), or -1 when 8 are given already.

`o3_sphere_fill(i)` fills sphere `i`'s seen face with black and then draws its outline: a moving solid over a drawing
kept from earlier frames hides what that drawing has behind it (register it with `outline` 0, fill it, then draw its
lines).

## Lines

```c
void o3_line(const struct o3_vec *p, int n, int closed, int weight, int style, int skip);
void o3_circle(const struct o3_mat *m, const struct o3_vec *c, int r, int n, int weight, int style, int skip);
void o3_ring_ticks(const struct o3_mat *m, const struct o3_vec *c, int r, int n, int major, int len);
```

`o3_line`: a polyline through `n` world points (up to 256; `closed` 1 joins the last to the first). `skip` is the
index of a sphere the line lies on, or -1: a line on a sphere's own surface is not hidden by that sphere's front, only
by its back half, so a latitude line on a globe shows its near half.

`o3_circle`: a circle of radius `r` around `c`, in the plane of `m`'s x and z axes (`m` 0: the world's level plane), in
`n` segments (up to 256). Stand it up or lean it with a matrix: `o3_rotation(&m, 0, -1024, 0)` puts it upright.

`o3_ring_ticks`: `n` short ticks around a circle (radius `r` in `m`'s x-z plane around `c`, the first at `m`'s -z),
each `len` long outward (world units x 4096); every `major`th one is half as long again and of standard weight.

| weight | | style | |
|---|---|---|---|
| `O3_HEAVY` | 2 panel pixels (outlines) | `O3_SOLID` | solid |
| `O3_STD` | 1.5 | `O3_DASH` | dashes 4 on, 3 off (panel pixels) |
| `O3_HAIR` | 1 (no halo) | `O3_DASH_FINE` | 3 on, 3 off |
| | | `O3_DOT` | dots, 0.6 on, 2.4 off |

## Marks and places

```c
void o3_glyph(const struct o3_vec *p, int shape);
int o3_project(const struct o3_vec *p, int *x, int *y);
void o3_to_view(const struct o3_vec *p, struct o3_vec *v);
void o3_to_page(const struct o3_vec *v, int *x, int *y);
int o3_drawn(int *hidden);
```

`o3_glyph`: a small mark at a world point, unless a sphere hides it: 0 a square, 1 a triangle, 2 a diamond, 3 a ringed
dot, about 3 of the drawing's pixels across. `o3_project`: where a world point lands on the page (the drawing's
pixels x 256) and whether it is seen (1) or behind a sphere (0): put a label next to a 3D point with it.
`o3_to_view` turns a world point into the camera's space; `o3_to_page` puts such a point on the page. `o3_drawn`: the
segments drawn since `o3_begin`, and the hidden ones into `*hidden` (for a page's cost and for tests).

## Inks

```c
void o3_ink(int colour, int alpha);
#define O3_OVER 0x10000
```

From `o3_ink` on, lines, dots, text and the overlay are drawn in `colour` (`OPUS_RGB` only) at `alpha` 0 to 256,
blended over what each pixel has: a second colour, a fading line. With `alpha | O3_OVER` the ink is written over what is
there without reading it first (cheaper: right over a black ground); below full alpha it only brightens a pixel, so a
fading line never darkens what it crosses. `o3_begin` goes back to the plate's one ink.

## The 2D overlay

All in the drawing's pixels x 256, in the current ink:

```c
void o3_seg(const int *xy, int n, int closed, int weight, int on, int off);
void o3_lines(const int *xy, int n, int weight);
void o3_dot(int x, int y, int size);
void o3_rect(int x0, int y0, int x1, int y1, int on, int off, int thick);
void o3_ellipse(int cx, int cy, int rx, int ry, int w, const int *clip);
void o3_fill(const int *xy, int n);
void o3_fill_alpha(const int *xy, int n, int alpha);
void o3_wave(const short *v, int n, int x, int y, int w, int h);
void o3_band(const short *lo, const short *hi, int n, int x, int y, int w, int h);
int o3_text(int x, int y, int dot, const char *s);
int o3_text_width(int dot, int chars);
```

- `o3_seg`: a polyline through `n` points (`xy`: x0 y0 x1 y1 ...), dashed `on`/`off` (panel pixels x 256; `on` 0
  solid), a weight as the 3D lines'. `o3_lines`: `n` separate segments (x0 y0 x1 y1 each), solid, in one go.
- `o3_dot`: a square `size` across (at least a panel pixel), centred on (x, y).
- `o3_rect`: an upright rectangle's outline of whole pixels, `thick` panel pixels, dashed `on`/`off` (panel pixels;
  `off` 0 solid). Cheap, not smoothed.
- `o3_ellipse`: an upright ellipse's outline, centre (cx, cy), radii rx, ry, `w` wide (panel pixels x 256), smoothed,
  kept inside `clip` (x0 y0 x1 y1) or not (0). A level circle seen by a camera with no yaw (a ring on a floor) for a
  fraction of what the same circle costs as lines.
- `o3_fill`: a convex polygon of 3 to 32 points filled black (a solid's face toward the viewer: what is behind it goes).
  `o3_fill_alpha`: the same at an alpha, 0 to 256 (256 black): a fading solid's face fades with its lines.
- `o3_wave`: a scope, `n` samples (-32767 to 32767) across the width `w` from x, centred on y, full scale reaching
  `h`. `o3_band`: a scope's history, `n` spans (each a lowest and a highest sample).
- `o3_text`: text in orbit3d's 5 x 7 pixel font (capital letters, digits, `.` and `-`), each dot `dot` panel pixels
  square (crisp on every screen), its top left at (x, y); returns its width. `o3_text_width`: the width of `chars`
  characters. For lowercase text in the OP-1's own font use `opus_page_text` ([api.md](api.md), "Pages").

## Cost and limits

- **Use `OPUS_RGB` inks.** orbit3d's own lines cost a few hundred cycles a short one. An ink given as a colour id goes
  through the core's strokes instead (about 6,000 a line). `OPUS_RGB(opus_accent_now(i))` keeps the user's colours.
- **What a frame costs** depends on the pixels it covers: the globe example draws about 700 segments a frame; SIM's
  page under a busy pattern measured 4.35 M cycles a frame at its worst. Draw what does not move once and keep it
  (`opus_page_still`, [api.md](api.md), "Pages"); fade with `O3_OVER` over a black ground (no pixel read); draw a level
  ring with `o3_ellipse` (24 lines for a ring had cost SIM 1.3 M cycles a frame). Developer mode's figures and the
  monitor show your page's drawing cost on the OP-1.
- **Limits:** 8 spheres a frame; 256 points a polyline and segments a circle; 3 to 32 points a filled face; the font
  has capitals, digits, `.` and `-` only.
- **Threads:** the display thread only, inside a page's draw call (the core calls it there).

## Versions

`needs opus.orbit3d 0.2.5` gives everything on this page.

| version | what came |
|---|---|
| 0.1 | the plate: the camera, spheres, lines, circles, ticks, glyphs, the halo |
| 0.2 | several inks (`o3_ink`), filled faces, the 2D overlay, the pixel font (SIM's page) |
| 0.2.2 | lines smoothed as the OS smooths its own; level and upright hairlines on whole pixels |
| 0.2.3 | `O3_OVER`'s edges never darker than what is there |
| 0.2.4 | `o3_ellipse` |
| 0.2.5 | a fading ink only brightens; `o3_fill_alpha` |

## The example: opus.globe

`examples/opus.globe` is a whole plugin in one file, `globe.c`. FN + orange push opens a quick page with a globe: its
outline and latitude and longitude lines (the far side's removed), a dashed ring of ticks leaning around it and a moon
going round on the ring, which hides what passes behind it. The camera turns a little every frame; blue turns it
faster, slower or backwards, green tilts it, white zooms. FN + orange push again, or any other key, closes it. Its ink
is the blue encoder's colour, so it follows the user's theme.

```
python plugin_tool.py build examples/opus.globe
```

Then copy `build/globe.opmod` and the starter pack's `orbit3d.opmod` into the OP-1's mods folder (disk mode), restart,
and hold FN while pushing the orange encoder.
