/* The castle page on the computer: castle_page.c drawn into a frame as the OP-1 keeps it (RGB565, turned), with the
 * core's calls it makes stood in for. Writes frames as raw RGB (page_preview.py turns them into a GIF and stills).
 *
 *   cc -O2 -Wall -Wextra -I.. -I../../../sdk page_preview.c ../castle_page.c -o page_preview -lm
 *   ./page_preview ROWS OUT.rgb sweep        each encoder up and down in turn, then all four up (28.5 frames a second)
 *   ./page_preview ROWS OUT.rgb still B G W O MS [DRONE]   one frame: the knobs 0-99 at ms, drone mode 1 or 0
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opus_plugin.h"
#include "castle_page.h"

static int ROWS = 376;
static int DRONE = 1;                      /* drone mode: the flag up */
static unsigned short frame_[400 * 256];
static const unsigned ACCENT[4] = { 0x4D9EFF, 0x00ED95, 0xFFFFFF, 0xFF3A5D };   /* Andrew's screen palette */

unsigned short *opus_page_pixels(int *rows) { *rows = ROWS; return frame_; }
unsigned opus_accent_now(int i) { return i >= 0 && i < 4 ? ACCENT[i] : 0; }
void opus_sincos(int turn4096, int *s, int *c)
{
    const double a = turn4096 * 2 * M_PI / 4096;
    *s = (int)lround(sin(a) * 4096);
    *c = (int)lround(cos(a) * 4096);
}

static void dump(FILE *f)
{
    const int w = ROWS, h = ROWS / 2;
    unsigned char *row = malloc((size_t)w * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned short p = frame_[256 * (ROWS - 1 - x) + y];
            row[3 * x] = (unsigned char)(((p >> 11) & 31) * 255 / 31);
            row[3 * x + 1] = (unsigned char)(((p >> 5) & 63) * 255 / 63);
            row[3 * x + 2] = (unsigned char)((p & 31) * 255 / 31);
        }
        fwrite(row, 3, (size_t)w, f);
    }
    free(row);
}

static int k(int v99) { return (v99 * 32767 + 98) / 99; }

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    ROWS = atoi(argv[1]);
    FILE *f = fopen(argv[2], "wb");
    castle_page_init();
    short kn[8] = {0};
    if (!strcmp(argv[3], "still") && argc >= 9) {
        for (int i = 0; i < 4; ++i) kn[i] = (short)k(atoi(argv[4 + i]));
        const unsigned ms = (unsigned)atoi(argv[8]);
        if (argc >= 10) DRONE = atoi(argv[9]);
        /* the pile: let green work on it for a while first, as the device would */
        for (unsigned t = 0; t + 35 < ms; t += 35) {
            memset(frame_, 0, sizeof frame_);
            castle_page_draw(kn, t, DRONE);
        }
        memset(frame_, 0, sizeof frame_);
        castle_page_draw(kn, ms, DRONE);
        dump(f);
    } else {
        unsigned t = 0;
        int n = 0;
        const double fps = 28.5;
        for (int seg = 0; seg < 8; ++seg) {            /* 1 s idle, four 4 s sweeps, 4 s all up, drone off 2 s, on 2 s */
            const int frames = (int)((seg == 0 ? 1.0 : seg >= 6 ? 2.0 : 4.0) * fps);
            DRONE = seg != 6;
            for (int i = 0; i < frames; ++i) {
                for (int c = 0; c < 4; ++c) kn[c] = 0;
                if (seg >= 1 && seg <= 4) kn[seg - 1] = (short)(32767 * pow(sin(M_PI * i / frames), 0.8));
                if (seg >= 5) for (int c = 0; c < 4; ++c) kn[c] = (short)k(85);
                memset(frame_, 0, sizeof frame_);
                castle_page_draw(kn, t, DRONE);
                if (n % 2 == 0) dump(f);                /* every other frame: a GIF at 14 a second */
                ++n;
                t = (unsigned)(n * 1000 / fps);
            }
        }
    }
    fclose(f);
    return 0;
}
