/* castle on the computer: plays castle_dsp.c the way the OS's engine service does (six voices, 128 samples at 88.2 kHz
 * a block, the sum taken down to 44.1 kHz) and writes a WAV. Not part of the plugin.
 *
 *   cc -O2 -Wall -Wextra -Werror -I.. render.c ../castle_dsp.c -o render -lm
 *   ./render demo demo.wav       knobs swept one by one, a second note crossfading, the orange push
 *   ./render sustain sus.wav     one note, the key let go at 0.3 s, ten minutes; prints the level every 10 s
 *   ./render knob K V out.wav    knob K (0-3) held at V (0-99) for 20 s, the others at 20
 *   ./render modes out.wav       drone mode off: a note held 3 s, a chord held 3 s; then on: a note let go, sustaining
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "castle_dsp.h"

#define VOICES 6

static struct ca_voice voice[VOICES];
static int busy[VOICES];                   /* the OS's view: rendering until finished says so */
static unsigned age[VOICES], clock_n;
static double voice_hz[VOICES];          /* each voice's own note, as the OS gives it */

static unsigned step_of(double hz) { return (unsigned)(hz * 4294967296.0 / CA_RATE); }

static int note_on(double hz)
{
    int pick = -1;
    for (int i = 0; i < VOICES; ++i)       /* a free voice, else the oldest */
        if (!busy[i]) { pick = i; break; }
    if (pick < 0) {
        pick = 0;
        for (int i = 1; i < VOICES; ++i)
            if (age[i] < age[pick]) pick = i;
    }
    ca_note_on(&voice[pick]);
    voice_hz[pick] = hz;
    busy[pick] = 1;
    age[pick] = ++clock_n;
    return pick;
}

static FILE *wav_open(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x44\xac\0\0\x88\x58\x01\0\x02\0\x10\0data\0\0\0\0", 1, 44, f);
    return f;
}

static void wav_close(FILE *f, unsigned samples)
{
    unsigned d = samples * 2, r = d + 36;
    fseek(f, 4, SEEK_SET); fwrite(&r, 4, 1, f);
    fseek(f, 40, SEEK_SET); fwrite(&d, 4, 1, f);
    fclose(f);
}

static short par[8];
static unsigned pitch[CA_BLOCK];
static double sum_sq;
static unsigned n_sq, written, clipped, finished_frees;

/* One audio block: every busy voice, the sum, down to 44.1 kHz. */
static void block(FILE *f)
{
    int mix[CA_BLOCK] = {0};
    for (int v = 0; v < VOICES; ++v) {
        if (!busy[v]) continue;
        if (ca_finished(&voice[v])) { busy[v] = 0; ++finished_frees; continue; }
        const unsigned st = step_of(voice_hz[v]);
        for (int i = 0; i < CA_BLOCK; ++i) pitch[i] = st;
        short out[CA_BLOCK];
        ca_render(&voice[v], out, par, pitch);
        for (int i = 0; i < CA_BLOCK; ++i) mix[i] += out[i];
    }
    short half[CA_BLOCK / 2];
    for (int i = 0; i < CA_BLOCK / 2; ++i) {
        int s = (mix[2 * i] + mix[2 * i + 1]) / 2;
        if (s > 32767 || s < -32768) ++clipped;
        s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
        half[i] = (short)s;
        sum_sq += (double)s * s;
        ++n_sq;
    }
    ca_block_end();
    fwrite(half, 2, CA_BLOCK / 2, f);
    written += CA_BLOCK / 2;
}

static double rms_db(void)
{
    double r = n_sq ? sqrt(sum_sq / n_sq) / 32768.0 : 0;
    sum_sq = 0; n_sq = 0;
    return r > 0 ? 20 * log10(r) : -999;
}

static int k(int v99) { return (v99 * 32767 + 98) / 99; }
static const unsigned BPS = CA_RATE / CA_BLOCK;   /* blocks a second, 689 */

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: render demo|sustain|knob ...\n"); return 2; }
    for (int v = 0; v < VOICES; ++v) ca_voice_init(&voice[v], v);
    const char *mode = argv[1];

    if (!strcmp(mode, "sustain")) {
        FILE *f = wav_open(argv[2]);
        for (int i = 0; i < 4; ++i) par[i] = (short)k(35);
        const int v0 = note_on(110.0);      /* the key goes up at 0.3 s: in drone mode nothing happens */
        const unsigned total = 600 * BPS;
        for (unsigned b = 0; b < total; ++b) {
            if (b == 3 * BPS / 10)
                ca_release(&voice[v0]);
            block(f);
            if ((b + 1) % (10 * BPS) == 0)
                printf("t=%4us  level %.2f dBFS\n", (b + 1) / BPS, rms_db());
        }
        wav_close(f, written);
    } else if (!strcmp(mode, "melody")) {
        /* a new note every 0.6 s for 8 s (let go after 0.2 s); how many voices render. ./render melody out.wav [drone 0|1] */
        if (argc >= 4) ca_drone = atoi(argv[3]);
        FILE *f = wav_open(argv[2]);
        for (int i = 0; i < 4; ++i) par[i] = (short)k(20);
        static const double NOTE[] = { 65.41, 73.42, 82.41, 98.0, 110.0, 98.0, 82.41, 73.42, 65.41, 98.0, 110.0, 130.8, 98.0, 82.41 };
        int a = -1, peak = 0;
        double busy_sum = 0;
        for (unsigned bl = 0; bl < 12 * BPS; ++bl) {
            const unsigned step = 6 * BPS / 10;
            if (bl < 8 * BPS && bl % step == 0) a = note_on(NOTE[(bl / step) % 14]);
            if (bl < 8 * BPS && bl % step == step / 3) ca_release(&voice[a]);
            block(f);
            int busy_n = 0;
            for (int v = 0; v < VOICES; ++v) busy_n += busy[v];
            peak = busy_n > peak ? busy_n : peak;
            if (bl < 8 * BPS) busy_sum += busy_n;
            if ((bl + 1) % BPS == 0) printf("t=%4.1fs voices rendering %d  level %.2f dBFS\n", (bl + 1.0) / BPS, busy_n, rms_db());
        }
        printf("while playing: %.1f voices on average, %d at most\n", busy_sum / (8 * BPS), peak);
        wav_close(f, written);
    } else if (!strcmp(mode, "repress")) {
        /* drone mode: C, let go; at 5 s C again, held 3 s; at 12 s G (a real crossfade) */
        FILE *f = wav_open(argv[2]);
        for (int i = 0; i < 4; ++i) par[i] = (short)k(20);
        int a = note_on(65.41);
        for (unsigned bl = 0; bl < 18 * BPS; ++bl) {
            if (bl == BPS / 4) ca_release(&voice[a]);
            if (bl == 5 * BPS) a = note_on(65.41);
            if (bl == 8 * BPS) ca_release(&voice[a]);
            if (bl == 12 * BPS) a = note_on(98.0);
            if (bl == 12 * BPS + BPS / 4) ca_release(&voice[a]);
            block(f);
            if ((bl + 1) % (BPS / 2) == 0) {
                int busy_n = 0;
                for (int v = 0; v < VOICES; ++v) busy_n += busy[v];
                printf("t=%5.1fs voices rendering %d  level %.2f dBFS\n", (bl + 1.0) / BPS, busy_n, rms_db());
            }
        }
        wav_close(f, written);
    } else if (!strcmp(mode, "modes")) {
        FILE *f = wav_open(argv[2]);
        for (int i = 0; i < 4; ++i) par[i] = (short)k(20);
        ca_drone = 0;
        int a = -1, b = -1, c = -1;
        for (unsigned bl = 0; bl < 24 * BPS; ++bl) {
            const double t = (double)bl / BPS;
            if (bl == 0) a = note_on(65.41);                        /* drone off: C held 3 s */
            if (bl == 3 * BPS) ca_release(&voice[a]);
            if (bl == 7 * BPS) { b = note_on(65.41); c = note_on(98.0); }   /* a chord, held 3 s */
            if (bl == 10 * BPS) { ca_release(&voice[b]); ca_release(&voice[c]); }
            if (bl == 14 * BPS) { ca_drone = 1; a = note_on(82.41); }   /* drone on: E, let go at once */
            if (bl == 14 * BPS + 20) ca_release(&voice[a]);
            block(f);
            if ((bl + 1) % BPS == 0) {
                int busy_n = 0;
                for (int v = 0; v < VOICES; ++v) busy_n += busy[v];
                printf("t=%4.1fs level %7.2f dBFS  voices busy %d\n", t + 1.0 / BPS, rms_db(), busy_n);
            }
        }
        wav_close(f, written);
    } else if (!strcmp(mode, "knob") && argc >= 5) {
        const int kn = atoi(argv[2]), val = atoi(argv[3]);
        FILE *f = wav_open(argv[4]);
        for (int i = 0; i < 4; ++i) par[i] = (short)k(20);
        par[kn] = (short)k(val);
        note_on(65.41);
        for (unsigned b = 0; b < 20 * BPS; ++b) block(f);
        rms_db();
        wav_close(f, written);
    } else {                                /* the demo */
        FILE *f = wav_open(argv[2]);
        const double C2 = 65.41, G2 = 98.0;
        for (int i = 0; i < 4; ++i) par[i] = (short)k(10);
        int vc = note_on(C2);
        const unsigned total = 64 * BPS;
        for (unsigned b = 0; b < total; ++b) {
            const double t = (double)b / BPS;
            for (int c = 0; c < 4; ++c) {   /* knob c sweeps up and back over 8 s from 6 + 9c s */
                const double t0 = 6 + 9 * c;
                double v = 10;
                if (t >= t0 && t < t0 + 8) v = 10 + 85 * sin(M_PI * (t - t0) / 8);
                par[c] = (short)k((int)v);
            }
            if (b == 44 * BPS) vc = note_on(G2);          /* a new drone: the old one fades as it comes in */
            if (b == 3 * BPS / 10 || b == 44 * BPS + 3 * BPS / 10)
                ca_release(&voice[vc]);                   /* each key let go after 0.3 s */
            if (b == 56 * BPS) ca_drone = 0;              /* the orange push: drone mode off, the drone fades */
            block(f);
            if ((b + 1) % BPS == 0) printf("t=%2us  level %.2f dBFS\n", (b + 1) / BPS, rms_db());
        }
        wav_close(f, written);
    }
    fprintf(stderr, "samples %u, clipped %u, voices freed %u\n", written, clipped, finished_frees);
    return 0;
}
