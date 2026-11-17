/* opus.dx's label font (dx_font.c), as shipped in opus.dx 0.3.3 (Opus 2 beta 1), 0.4.0 (beta 2 to 3.2) and 0.4.1 (beta 4 to 4.2); replaced in opus.dx 0.5.0.
 *
 * Derived from op1r by pcppcp (https://github.com/pcppcp/op1r; its sources: https://github.com/pcppcp/op1r-src):
 * synth_sdk/screens/stroke_font.h.
 * Licensed under the GNU General Public License, version 3 (GPL-3.0), as op1r is:
 * https://www.gnu.org/licenses/gpl-3.0.html
 *
 * Only the parts derived from op1r are here, with the types and definitions needed to read them; the
 * comments of the shipped source are left out. */

#include "dx.h"

#define S(a, b, c, d)   (unsigned char)((a) << 4 | (b)), (unsigned char)((c) << 4 | (d))

static const unsigned char G_A[] = { 5, S(0,6,0,2), S(0,2,2,0), S(2,0,4,2), S(4,2,4,6), S(0,3,4,3) };
static const unsigned char G_B[] = { 9, S(0,0,0,6), S(0,0,3,0), S(3,0,4,1), S(4,1,4,2), S(4,2,3,3), S(0,3,3,3),
                                     S(3,3,4,4), S(4,4,4,6), S(4,6,0,6) };
static const unsigned char G_C[] = { 3, S(4,0,0,0), S(0,0,0,6), S(0,6,4,6) };
static const unsigned char G_D[] = { 6, S(0,0,0,6), S(0,0,3,0), S(3,0,4,1), S(4,1,4,5), S(4,5,3,6), S(3,6,0,6) };
static const unsigned char G_E[] = { 4, S(4,0,0,0), S(0,0,0,6), S(0,6,4,6), S(0,3,3,3) };
static const unsigned char G_F[] = { 3, S(4,0,0,0), S(0,0,0,6), S(0,3,3,3) };
static const unsigned char G_G[] = { 5, S(4,0,0,0), S(0,0,0,6), S(0,6,4,6), S(4,6,4,3), S(4,3,2,3) };
static const unsigned char G_H[] = { 3, S(0,0,0,6), S(4,0,4,6), S(0,3,4,3) };
static const unsigned char G_I[] = { 3, S(1,0,3,0), S(2,0,2,6), S(1,6,3,6) };
static const unsigned char G_J[] = { 3, S(4,0,4,6), S(4,6,0,6), S(0,6,0,4) };
static const unsigned char G_K[] = { 3, S(0,0,0,6), S(4,0,0,3), S(0,3,4,6) };
static const unsigned char G_L[] = { 2, S(0,0,0,6), S(0,6,4,6) };
static const unsigned char G_M[] = { 4, S(0,6,0,0), S(0,0,2,3), S(2,3,4,0), S(4,0,4,6) };
static const unsigned char G_N[] = { 3, S(0,6,0,0), S(0,0,4,6), S(4,6,4,0) };
static const unsigned char G_O[] = { 4, S(0,0,4,0), S(4,0,4,6), S(4,6,0,6), S(0,6,0,0) };
static const unsigned char G_P[] = { 4, S(0,6,0,0), S(0,0,4,0), S(4,0,4,3), S(4,3,0,3) };
static const unsigned char G_Q[] = { 5, S(0,0,4,0), S(4,0,4,6), S(4,6,0,6), S(0,6,0,0), S(2,4,4,6) };
static const unsigned char G_R[] = { 5, S(0,6,0,0), S(0,0,4,0), S(4,0,4,3), S(4,3,0,3), S(1,3,4,6) };
static const unsigned char G_S[] = { 5, S(4,0,0,0), S(0,0,0,3), S(0,3,4,3), S(4,3,4,6), S(4,6,0,6) };
static const unsigned char G_T[] = { 2, S(0,0,4,0), S(2,0,2,6) };
static const unsigned char G_U[] = { 3, S(0,0,0,6), S(0,6,4,6), S(4,6,4,0) };
static const unsigned char G_V[] = { 2, S(0,0,2,6), S(2,6,4,0) };
static const unsigned char G_W[] = { 4, S(0,0,1,6), S(1,6,2,3), S(2,3,3,6), S(3,6,4,0) };
static const unsigned char G_X[] = { 2, S(0,0,4,6), S(4,0,0,6) };
static const unsigned char G_Y[] = { 3, S(0,0,2,3), S(4,0,2,3), S(2,3,2,6) };
static const unsigned char G_Z[] = { 3, S(0,0,4,0), S(4,0,0,6), S(0,6,4,6) };
static const unsigned char G_0[] = { 5, S(0,0,4,0), S(4,0,4,6), S(4,6,0,6), S(0,6,0,0), S(4,0,0,6) };
static const unsigned char G_1[] = { 3, S(1,1,2,0), S(2,0,2,6), S(1,6,3,6) };
static const unsigned char G_2[] = { 5, S(0,0,4,0), S(4,0,4,3), S(4,3,0,3), S(0,3,0,6), S(0,6,4,6) };
static const unsigned char G_3[] = { 4, S(0,0,4,0), S(4,0,4,6), S(4,6,0,6), S(1,3,4,3) };
static const unsigned char G_4[] = { 3, S(0,0,0,3), S(0,3,4,3), S(4,0,4,6) };
static const unsigned char G_6[] = { 5, S(4,0,0,0), S(0,0,0,6), S(0,6,4,6), S(4,6,4,3), S(4,3,0,3) };
static const unsigned char G_7[] = { 2, S(0,0,4,0), S(4,0,1,6) };
static const unsigned char G_8[] = { 5, S(0,0,4,0), S(4,0,4,6), S(4,6,0,6), S(0,6,0,0), S(0,3,4,3) };
static const unsigned char G_9[] = { 5, S(4,3,0,3), S(0,3,0,0), S(0,0,4,0), S(4,0,4,6), S(4,6,0,6) };
static const unsigned char G_DASH[] = { 1, S(1,3,3,3) };
static const unsigned char G_LOW[] = { 1, S(0,6,4,6) };
static const unsigned char G_DOT[] = { 1, S(2,6,2,6) };
static const unsigned char G_SLASH[] = { 1, S(4,0,0,6) };
static const unsigned char G_PLUS[] = { 2, S(2,1,2,5), S(0,3,4,3) };
static const unsigned char G_HASH[] = { 4, S(1,0,1,6), S(3,0,3,6), S(0,2,4,2), S(0,4,4,4) };
static const unsigned char G_NONE[] = { 0 };

static const unsigned char *glyph(int c)
{
    static const unsigned char *const LETTER[26] = { G_A, G_B, G_C, G_D, G_E, G_F, G_G, G_H, G_I, G_J, G_K, G_L, G_M,
                                                     G_N, G_O, G_P, G_Q, G_R, G_S, G_T, G_U, G_V, G_W, G_X, G_Y, G_Z };
    static const unsigned char *const DIGIT[10] = { G_0, G_1, G_2, G_3, G_4, G_S, G_6, G_7, G_8, G_9 };
    if (c >= 'a' && c <= 'z')
        return LETTER[c - 'a'];
    if (c >= 'A' && c <= 'Z')
        return LETTER[c - 'A'];
    if (c >= '0' && c <= '9')
        return DIGIT[c - '0'];
    switch (c) {
    case '-': return G_DASH;
    case '_': return G_LOW;
    case '.': return G_DOT;
    case '/': return G_SLASH;
    case '+': return G_PLUS;
    case '#': return G_HASH;
    default: return G_NONE;
    }
}

int dx_label_width(const char *s, int scale)
{
    int n = 0;
    while (s[n])
        ++n;
    return n ? (6 * n - 2) * scale : 0;
}

void dx_label(int x, int y, int scale, int colour, const char *s)
{
    for (; *s; ++s, x += 6 * scale) {
        const unsigned char *const g = glyph(*s);
        for (int i = 0; i < g[0]; ++i) {
            const int a = g[1 + 2 * i], b = g[2 + 2 * i];
            opus_page_line(x + (a >> 4) * scale, y + (a & 15) * scale, x + (b >> 4) * scale, y + (b & 15) * scale,
                           colour);
        }
    }
}

void dx_label_right(int x, int y, int scale, int colour, const char *s)
{
    dx_label(x - dx_label_width(s, scale), y, scale, colour, s);
}
