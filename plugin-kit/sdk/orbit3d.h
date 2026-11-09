/* orbit3d: 3D line art for a plugin's page (the library plugin opus.orbit3d; a plugin names it in its manifest's
 * needs line and includes this header). The plugin authors' kit ships it with its guide, docs/orbit3d.md, and an
 * example, examples/opus.globe.
 *
 * The look is the duotone line drawing of an OP-1 plate: one ink on black, constant strokes (heavy 2, standard 1.5,
 * hairline 1 panel pixels), an orthographic camera, lines behind a solid removed, a dark gap (a halo) cut where a line
 * passes in front of another. No shading in this version (no tone): solids are spheres, drawn as their outlines.
 *
 * Fixed point throughout (the OP-1 has no floating point unit): a world point is in 1/4096 of a unit (O3_ONE), a
 * rotation a 3 x 3 matrix in 1/4096 (O3_ONE = 1.0), an angle in 1/4096 of a turn (opus_sincos's). Inside a page's
 * draw call only: o3_begin, the solids (o3_sphere: what hides lines), then the lines (o3_line, o3_circle, o3_ring_ticks),
 * then o3_end. Every call draws at once; a line is tested against the solids given before it. */
#ifndef ORBIT3D_H
#define ORBIT3D_H

#define O3_ONE          4096

struct o3_vec { int x, y, z; };
struct o3_mat { int m[9]; };               /* row major: x' = m0 x + m1 y + m2 z, ... */

/* Line weights and styles (the plate's): heavy (an outline), standard, hairline; solid or dashed. */
enum { O3_HEAVY, O3_STD, O3_HAIR };
enum { O3_SOLID, O3_DASH, O3_DASH_FINE, O3_DOT };   /* dashes 4/3 and 3/3 pixels, dots 0.6/2.4 */

/* The camera for this frame: it turns the world by yaw (around the vertical), then pitch (tilts toward the viewer);
 * zoom is the drawing's pixels (of the 320 x 160 page) a world unit, x 256; (cx, cy) the world's origin on the page in
 * pixels x 256. ink: the colour (OPUS_RGB or an id); halo: 1 cuts the gaps. */
void o3_begin(int yaw, int pitch, int zoom, int cx, int cy, int ink, int halo);
void o3_end(void);

/* Rotations: a matrix that turns by yaw (around y), pitch (around x) and roll (around z), in that order; products;
 * a point turned by m and moved by at. */
void o3_rotation(struct o3_mat *m, int yaw, int pitch, int roll);
void o3_mul(struct o3_mat *out, const struct o3_mat *a, const struct o3_mat *b);
void o3_apply(struct o3_vec *out, const struct o3_mat *m, const struct o3_vec *p, const struct o3_vec *at);

/* A sphere (centre c, radius r in world units x 4096): it hides the lines behind it from here on, and its outline is
 * drawn (heavy) unless outline is 0. Up to 8 a frame. -> its index (for o3_sphere_fill and a line's skip), or -1. */
int o3_sphere(const struct o3_vec *c, int r, int outline);
/* Sphere i's seen face filled black, then its outline: a moving solid over a kept (still) drawing hides what that has
 * behind it (register it with outline 0, fill it, then draw the lines on it). */
void o3_sphere_fill(int i);

/* A polyline through n world points (closed: back to the first), weight and style; skip: the index of a sphere the
 * line lies on (its own surface: it is not hidden by that sphere's front, only by its back half), or -1. */
void o3_line(const struct o3_vec *p, int n, int closed, int weight, int style, int skip);
/* A circle of radius r around c in the plane of m's x and z axes (m 0: the world's), in n segments (at most 256). */
void o3_circle(const struct o3_mat *m, const struct o3_vec *c, int r, int n, int weight, int style, int skip);
/* n short ticks around a circle (radius r, in m's x-z plane around c, the first at m's -z), long `len` outward
 * (world units x 4096); every `major`th longer by half and of standard weight. */
void o3_ring_ticks(const struct o3_mat *m, const struct o3_vec *c, int r, int n, int major, int len);

/* A small mark at a world point, unless a sphere hides it: 0 a square, 1 a triangle, 2 a diamond, 3 a ringed dot. */
void o3_glyph(const struct o3_vec *p, int shape);
/* Where a world point lands on the page (pixels x 256) and whether it is seen (1) or behind a sphere (0). */
int o3_project(const struct o3_vec *p, int *x, int *y);

/* The segments drawn since o3_begin and the ones hidden (for a page's cost and the tests). */
int o3_drawn(int *hidden);

/* 0.2 (2026-11-03, SIM's page): several inks, solids drawn as faces, an overlay in 2D, text.
 * o3_ink: the ink from here on (OPUS_RGB) at alpha 0-256, blended over what each pixel has (o3_begin goes back to the
 * plate's one ink). o3_to_view: a world point in the camera's space (x right, y up, z toward the eye); o3_to_page: that
 * point on the page (pixels x 256). o3_fill: a convex polygon (up to 8 points on the page, pixels x 256) filled black
 * (a solid's face toward the eye: what is behind it goes). o3_seg: a polyline on the page, dashed on/off (panel pixels
 * x 256; on 0: solid). o3_dot: a square of the ink (size: the drawing's pixels x 256). o3_text: text in a 5 x 7 pixel
 * font (capitals and digits, . and -), each dot `dot` panel pixels, its top left at (x, y) -> its width;
 * o3_text_width: the width of `chars` characters. */
void o3_ink(int colour, int alpha);
#define O3_OVER         0x10000            /* o3_ink's alpha | O3_OVER: written over what is there, not blended (a
                                              covered pixel without a read of the panel's memory: the cheap way over a
                                              black ground; 0.2.3: a line's soft edge each channel the brighter of it
                                              and what is there; 0.2.5: below full alpha every pixel so, a fading
                                              ink only brightening) */
void o3_to_view(const struct o3_vec *p, struct o3_vec *v);
void o3_to_page(const struct o3_vec *v, int *x, int *y);
void o3_fill(const int *xy, int n);
void o3_seg(const int *xy, int n, int closed, int weight, int on, int off);
void o3_dot(int x, int y, int size);
int o3_text(int x, int y, int dot, const char *s);
int o3_text_width(int dot, int chars);
/* o3_lines: n separate segments (x0 y0 x1 y1 each, page pixels x 256), solid, in one go. o3_rect: an upright
 * rectangle's outline of whole panel pixels, `thick` of them, dashed on/off (panel pixels; off 0 solid): cheap, unsmoothed.
 * o3_wave: a scope, n samples across w from x, centred on y, full scale reaching h (page pixels x 256). o3_fill takes up
 * to 32 points. */
void o3_lines(const int *xy, int n, int weight);
void o3_rect(int x0, int y0, int x1, int y1, int on, int off, int thick);
void o3_wave(const short *v, int n, int x, int y, int w, int h);
/* o3_band: a scope's history, n spans (each a lowest and a highest sample) across w from x, centred on y, full scale
 * reaching h: each panel column the spans it covers. */
void o3_band(const short *lo, const short *hi, int n, int x, int y, int w, int h);

/* o3_ellipse (0.2.4): an upright ellipse's outline, centre (cx, cy) and radii rx, ry (the drawing's pixels x 256), w
 * wide (panel pixels x 256), smoothed, in the ink, kept inside clip (x0 y0 x1 y1, the drawing's pixels x 256) or not
 * (0): a level circle seen from a camera with no yaw (a ring on a floor, inside the floor) for a fraction of the
 * lines' cost. */
void o3_ellipse(int cx, int cy, int rx, int ry, int w, const int *clip);

/* o3_fill_alpha (0.2.5): o3_fill at an alpha, 0-256: what is under the face darkened by it (256 black, as o3_fill),
 * so a fading solid's face fades with it. Reads each pixel. */
void o3_fill_alpha(const int *xy, int n, int alpha);

#endif
