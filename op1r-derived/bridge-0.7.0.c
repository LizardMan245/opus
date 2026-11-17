/* The op1r bridge's stroke font and helpers, as shipped in Opus 2 beta 1 (opus.bridge 0.7.0); replaced in beta 4.2 (opus.bridge 0.11.0).
 *
 * Derived from op1r by pcppcp (https://github.com/pcppcp/op1r; its sources: https://github.com/pcppcp/op1r-src):
 * synth_sdk/screens/stroke_font.h, synth_sdk/seq/seq_text.h, synth_sdk/op1_plugin.h.
 * Licensed under the GNU General Public License, version 3 (GPL-3.0), as op1r is:
 * https://www.gnu.org/licenses/gpl-3.0.html
 *
 * Only the parts derived from op1r are here, with the types and definitions needed to read them; the
 * comments of the shipped source are left out. */

typedef unsigned int u32;

typedef unsigned char u8;

#define RASTER_LINE     ((void (*)(void *, const int *, const int *, int, const u8 *))0x01A24A4C)

static void sq_line(void *r, int x0, int y0, int x1, int y1, int w, const u8 *c)
{
    const int a[2] = { x0, y0 }, b[2] = { x1, y1 };
    RASTER_LINE(r, a, b, w, c);
}

static const struct { char c; u8 narrow; const char *s; } FONT[] = {
    { 'A', 0, "06020220204242460444" }, { 'B', 0, "0006003030414142423303333344444545363606" },
    { 'C', 0, "400000060646" }, { 'D', 0, "000600303041414545363606" }, { 'E', 0, "4000000606460333" },
    { 'F', 0, "400000060333" }, { 'G', 0, "40000006064646434323" }, { 'H', 0, "000640460343" },
    { 'I', 0, "202610301636" }, { 'J', 0, "404646060604" }, { 'K', 0, "000640030346" }, { 'L', 0, "00060646" },
    { 'M', 0, "0600002323404046" }, { 'N', 0, "060000464640" }, { 'O', 0, "0040404646060600" },
    { 'P', 0, "0600004040434303" }, { 'Q', 0, "00404046460606002446" }, { 'R', 0, "06000040404343031346" },
    { 'S', 0, "40000003034343464606" }, { 'T', 0, "00402026" }, { 'U', 0, "000606464640" }, { 'V', 0, "00262640" },
    { 'W', 0, "0016162323363640" }, { 'X', 0, "00464006" }, { 'Y', 0, "002340232326" }, { 'Z', 0, "004040060646" },
    { '0', 0, "00404046460606004006" }, { '1', 1, "11202026" }, { '2', 0, "00404043430303060646" },
    { '3', 0, "0040404646061343" }, { '4', 0, "000303434046" }, { '5', 0, "40000003034343464606" },
    { '6', 0, "40000006064646434303" }, { '7', 0, "00404026" }, { '8', 0, "00404046460606000343" },
    { '9', 0, "43030300004040464606" }, { '#', 0, "1016303602420444" }, { '/', 0, "4006" },
    { '+', 0, "21250343" }, { '-', 0, "0343" }, { '%', 0, "400600113546" }, { '.', 1, "0506" },
    { ':', 1, "01020405" },
};

#define ADVANCE         6

#define ADVANCE_NARROW  3

static int sq_text(void *r, int x, int y, int s, const char *t, int w, const u8 *c)
{
    for (; *t; ++t) {
        char ch = *t;
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
        int adv = ADVANCE;
        for (unsigned k = 0; k < sizeof FONT / sizeof FONT[0]; ++k) {
            if (FONT[k].c != ch)
                continue;
            for (const char *p = FONT[k].s; p[0] && p[1] && p[2] && p[3]; p += 4)
                sq_line(r, x + (p[0] - '0') * s, y + (p[1] - '0') * s, x + (p[2] - '0') * s, y + (p[3] - '0') * s, w,
                        c);
            if (FONT[k].narrow)
                adv = ADVANCE_NARROW;
            break;
        }
        x += adv * s;
    }
    return x;
}

static char *sq_cat(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    *p = 0;
    return p;
}

static char *sq_num(char *p, u32 v, int digits)
{
    char d[10];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 10);
    while (n < digits && n < 10)
        d[n++] = '0';
    while (n)
        *p++ = d[--n];
    *p = 0;
    return p;
}

static void sq_node_rotate(void *node, int cx, int cy, int s15, int c15)
{
    const int c = c15 >> 5, s = s15 >> 5;
    const int m[9] = { c, s, 0, -s, c, 0, cx - ((cx * c - cy * s) >> 10), cy - ((cx * s + cy * c) >> 10), 1024 };
    fw_node_set_matrix(node, m);
}
