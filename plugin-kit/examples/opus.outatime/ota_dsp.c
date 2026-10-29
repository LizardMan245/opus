/* outatime's sound: lab's design, the owner's choice of 2026-10-25, then shaped by the owner on the device ("discard
 * the 88 mph jump zap effect and make that parameter more of a constant time-travel, morph sounding effect"; "the
 * electric sparks effect just sounds as annoying crackle ... it should be just a strong overdrive effect"; "the volume
 * is super inconsistent when changing settings"; time drift for speed, ignition for trail). Plain C, no SDK: the engine
 * (outatime.c) and the host renders (work/outatime) build the same file; every curve a table (make_tables.py: the
 * Blackfin has no floating point), every stage that would change the level matched there by simulation.
 *
 * A voice (88.2 kHz, the engine service's rate): three oscillators, the flux capacitor's arms.
 *   YEAR: their wave, 1955 a triangle (round, an organ's) -> 1985 a saw (the film's own synths) -> 2015 a sine folded
 *     up to 8 times (glassy): 9 waves at the same rms, an arm crossfading the two nearest its year.
 *   SPEED (0-88 mph): the time drift. The three arms sit at different points in time, a third of a cycle apart on one
 *     slow modulator, travelling around the year: at 0 mph all at the year, toward 88 up to 30 years either way and up
 *     to 1.9 Hz, so the voice keeps morphing between the eras.
 *   SPREAD (shift + blue): the outer arms detuned up to 20 cents from the middle one. FLUX (shift + white): the
 *     ensemble, the arms' pitch moved by a three-phase modulator (the old string machines' chorus), up to 22 cents at
 *     0.4-3 Hz.
 *   TRAIL: the ignition. A state-variable lowpass whose cutoff follows the year (900 Hz in 1955 to 14 kHz in 2015) and
 *     TONE (shift + green); each note ignites up to three octaves above it and burns down with 40 ms to 1.4 s, the
 *     resonance rising with TRAIL; the cutoff's loss of level made up, the resonance's gain taken back at the input.
 * The sum (44.1 kHz, the engine's block): POWER, a strong overdrive at 1 to 40 times in one of four shapes, the setting
 *   "overdrive type" (1.1): warm (tanh), hard (a clip), tube (the negative side clipping at 0.6, even harmonics) and
 *   fold (a triangle wavefolder). Its level is matched live, as a compressor would, so no power and full power sound
 *   as loud: two followers (23 ms) of the sum before the drive and of the drive's output, and a make-up gain from
 *   their ratio a block (1/64 to 2, eased over the block, resting at 1 / the drive in silence), so any power, shape,
 *   note or chord keeps the level the voices came in with. Then a DC blocker (10 Hz) and a lowpass at 7 kHz for the
 *   fizz, both inside the match; after it 4 dB more and a soft limiter easing anything above 0.7 of full scale into
 *   it. */
#include "ota_dsp.h"
#include "ota_tables.h"

#define SPREAD_MAX      195113             /* Q24: 20 cents, 2^(20/1200) - 1 */
#define THIRD           0x55555555u        /* a third of a cycle */
#define FIZZ            21800              /* Q15: 1 - exp(-2 pi 7000 / 44100) */
#define IGNITE          192                /* the ignition's top: three octaves of the filter's index (64 an octave) */
#define FMAX            448                /* the filter's index: 100 Hz .. 12.8 kHz (14 kHz at most) */
#define BAND_MAX        65536              /* its band state kept inside +-2: the products stay in 32 bits */

static const short *bank = &WAVES_BANK[0][0];   /* the era waves: in fast memory once ota_fast had some */

static short scratch_sdram[128];
static short *scratch = scratch_sdram;     /* the arms' sum, a voice block: in fast memory once ota_fast had some */

void ota_fast(short *to)
{
    if (!to)
        return;
    const short *const from = &WAVES_BANK[0][0];
    for (int i = 0; i < OTA_WAVES * OTA_WLEN; ++i)
        to[i] = from[i];
    bank = to;
    scratch = to + OTA_WAVES * OTA_WLEN;
}

int ota_fast_bytes(void)
{
    return (OTA_WAVES * OTA_WLEN + 128) * (int)sizeof(short);   /* the bank, and the arms' sum */
}

static int clamp_knob(int v)
{
    return v < 0 ? 0 : v > 32767 ? 32767 : v;
}

static int knob_u(const unsigned *t, int v)
{
    v = clamp_knob(v);
    const int i = v >> 7, f = v & 127;
    return (int)t[i] + ((((int)t[i + 1] - (int)t[i]) * f) >> 7);   /* every table's steps x 127 fit 32 bits */
}

static int knob_h(const unsigned short *t, int v)
{
    v = clamp_knob(v);
    const int i = v >> 7, f = v & 127;
    return (int)t[i] + ((((int)t[i + 1] - (int)t[i]) * f) >> 7);
}

static inline __attribute__((always_inline)) int tanh_q15(int x)
{
    const int neg = x < 0;
    const unsigned a = (unsigned)(neg ? -x : x);
    int y;
    if (a >= 512u * 512u)
        y = 32767;
    else {
        const unsigned i = a >> 9, f = a & 511;
        y = (int)TANH[i] + ((((int)TANH[i + 1] - (int)TANH[i]) * (int)f) >> 9);
    }
    return neg ? -y : y;
}

int ota_year(int knob) { return 1955 + (clamp_knob(knob) * 60 + 16383) / 32767; }
int ota_mph(int knob) { return (clamp_knob(knob) * 88 + 16383) / 32767; }
int ota_gw100(int knob) { return (clamp_knob(knob) * 121 + 16383) / 32767; }

/* Arm k's year as the voice's last render left its time drift (ota_render's own sum, without the era's wave). */
int ota_arm_year(const short *par, unsigned drift, int k)
{
    const int depth = knob_h(DRIFT_DEPTH, clamp_knob(par[OTA_SPEED]));
    return ota_year(clamp_knob(par[OTA_YEAR]) + ((depth * SINE[(drift + (unsigned)k * THIRD) >> 22]) >> 15));
}

void ota_voice_init(struct ota_voice *v)
{
    for (int k = 0; k < 3; ++k)
        v->ph[k] = (unsigned)k * THIRD;    /* the arms apart from the start: no phasey onset */
    v->drift = v->flux = 0;
    v->env = v->low = v->band = 0;
}

void ota_note_on(struct ota_voice *v)
{
    v->env = 32767;                        /* ignited */
}

/* An arm's era: its wave (a row of the bank) and the share of the next, for a year (Q15) */
static void era(int year, const short **a, const short **b, int *w)
{
    year = clamp_knob(year);
    const int pos = year * (OTA_WAVES - 1), i = pos >> 15;
    const int r = i >= OTA_WAVES - 1 ? OTA_WAVES - 2 : i;
    *a = bank + r * OTA_WLEN;
    *b = *a + OTA_WLEN;
    *w = i >= OTA_WAVES - 1 ? 32767 : pos & 32767;
}

void ota_render(struct ota_voice *v, short *out, const short *par, unsigned pitch)
{
    const int year = clamp_knob(par[OTA_YEAR]), speed = clamp_knob(par[OTA_SPEED]), trail = clamp_knob(par[OTA_TRAIL]);
    /* the arms' steps: the note, the spread, the ensemble (32-bit products: libgcc's 64-bit ones cost thousands) */
    const int spread = (clamp_knob(par[OTA_SPREAD]) * (SPREAD_MAX >> 3)) >> 12;   /* Q24 */
    const int fdepth = knob_u(FLUX_DEPTH, par[OTA_FLUX]);                      /* Q24, up to 214,000 */
    unsigned st[3];
    for (int k = 0; k < 3; ++k) {
        const int m = SINE[(v->flux + (unsigned)k * THIRD) >> 22];
        const int d = ((k - 1) * spread + (((fdepth >> 3) * m) >> 12)) >> 8;   /* Q16 over 1 */
        st[k] = pitch + (unsigned)(((int)(pitch >> 12) * d) >> 4);
    }
    v->flux += (unsigned)knob_u(FLUX_STEP, par[OTA_FLUX]);
    /* the time drift: each arm's year, a third of a cycle apart around the knob's */
    const int depth = knob_h(DRIFT_DEPTH, speed);   /* Q15 of the year knob */
    const short *wa[3], *wb[3];
    int ww[3];
    for (int k = 0; k < 3; ++k)
        era(year + ((depth * SINE[(v->drift + (unsigned)k * THIRD) >> 22]) >> 15), &wa[k], &wb[k], &ww[k]);
    v->drift += (unsigned)knob_u(DRIFT_STEP, speed);
    /* the ignition: the cutoff from the year and the tone, up with the burning note; resonance with the trail */
    int ci = 203 + ((year * 256) >> 15) + ((clamp_knob(par[OTA_TONE]) - 16384) >> 7);   /* 900 Hz .. 14 kHz */
    ci = ci < 0 ? 0 : ci > FMAX ? FMAX : ci;
    const int comp = SVF_COMP[ci >> 3];    /* Q12: the static cutoff's level made up */
    int fi = ci + ((((v->env * IGNITE) >> 15) * trail) >> 15);
    fi = fi > FMAX ? FMAX : fi;
    const int f = SVF_F[fi];               /* Q14 */
    const int damp = 23101 - ((trail * 15000) >> 15);   /* Q14: 1.41 (none) .. 0.5 (resonant) */
    const int res_back = 32768 - ((trail * 13107) >> 15);   /* Q15: the resonance's gain taken back */
    const int out_gain = (((comp * 21845) >> 15) * res_back) >> 15;   /* Q12: made up x 4/3 (the quarter) x a half
                                                                        (a saw's peak), the resonance taken back */
    v->env = (v->env * knob_h(BURN, trail)) >> 15;
    /* The arms one pass each, a quarter of each into sum (the sum of three fits a short), then the filter's pass over
     * them into out: one loop doing it all held more values than the Blackfin has registers (93 instructions a sample,
     * 9 of them the stack's); a pass each holds a few. sum is in fast memory (L1) when the engine gave some: out, the
     * engine service's buffer, is written once a sample. */
    short *const sum = scratch;
    for (int k = 0; k < 3; ++k) {          /* the first arm sets sum, the others add (1.1.3: a loop each, not a test a
                                              sample; the next era's wave read from the same pointer: era's b is a's
                                              next wave) */
        unsigned p = v->ph[k];
        const unsigned s = st[k];
        const short *const a = wa[k];
        const int w = ww[k];
        if (!k)
            for (int i = 0; i < 128; ++i) {
                p += s;
                const short *const q = a + (p >> 24);
                sum[i] = (short)((q[0] + (((short)(q[OTA_WLEN] - q[0]) * (short)w) >> 15)) >> 1);   /* half-scale
                                                                                  waves: 16 x 16, one cycle */
            }
        else
            for (int i = 0; i < 128; ++i) {
                p += s;
                const short *const q = a + (p >> 24);
                sum[i] = (short)(sum[i] + ((q[0] + (((short)(q[OTA_WLEN] - q[0]) * (short)w) >> 15)) >> 1));
            }
        v->ph[k] = p;
    }
    int low = v->low, band = v->band;
    for (int i = 0; i < 128; ++i) {
        const int x = sum[i];              /* a quarter of the sum */
        low += (f * band) >> 14;
        low = low > BAND_MAX ? BAND_MAX : low;
        low = low < -BAND_MAX ? -BAND_MAX : low;
        const int high = x - low - ((damp * band) >> 14);
        band += (f * (high >> 2)) >> 12;   /* high up to about 5.6: a quarter of it keeps the product in 32 bits */
        band = band > BAND_MAX ? BAND_MAX : band;
        band = band < -BAND_MAX ? -BAND_MAX : band;
        int y = (low * out_gain) >> 12;
        y = y > 32767 ? 32767 : y;
        y = y < -32768 ? -32768 : y;
        out[i] = (short)y;
    }
    v->low = low;
    v->band = band;
}

void ota_fx_init(struct ota_fx *f)
{
    f->in = f->out = 0;
    f->gain = 4096;
    f->hp_x = f->hp_y = 0;
    f->lp = 0;
}

/* The drive's shape of v (Q15, up to 40 times full scale) -> Q15 within full scale. */
static inline __attribute__((always_inline)) int shape(int v, int s)
{
    switch (s) {
    case OTA_HARD:
        return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    case OTA_TUBE: {                       /* the negative side: tanh(5/3 v) x 0.6, clipping at 0.6 and sooner */
        if (v >= 0)
            return tanh_q15(v);
        const int c = v < -262144 ? -262144 : v;          /* past -8 the tanh is -1 anyway */
        return (tanh_q15(((c >> 3) * 54613) >> 12) * 19661) >> 15;
    }
    case OTA_FOLD: {                       /* a triangle folding back at full scale: itself within it */
        const int m = (v + 32768) & 131071;
        const int y = m < 65536 ? m - 32768 : 98304 - m;
        return y > 32767 ? 32767 : y;
    }
    default:
        return tanh_q15(v);
    }
}

#define FOLLOW          10                 /* the followers' time: 1024 samples, 23 ms */
#define SILENT          8388               /* Q23: -60 dB, below it the match rests at its floor */
#define MATCH_MIN       64                 /* the make-up gain's range, Q12: 1/64 .. 2 */
#define MATCH_MAX       8192
/* Q12: a driven wave's lower rms at the same average level, made up at full drive (x 1.21; a fold gets less square:
 * x 1.05), each measured in the host renders (work/outatime/host, the 20_ renders at power 0, 50 and 99). */
static const short SQUARE[OTA_SHAPES] = { 860, 860, 860, 210 };
#define DC_LEAK         47                 /* Q15: 2 pi 10 / 44100, the DC blocker's pole 1 - this */
#define OUT_GAIN        6492               /* Q12: +4 dB after the match: the engine's level among the others */
#define KNEE            22938              /* the soft limiter above 0.7 of full scale: tanh to full scale */
#define KNEE_SCALE      3414               /* Q10: 32768 / (32767 - KNEE), the tanh's argument a step over the knee */

static inline __attribute__((always_inline))
int limit(int z)                          /* below the knee as it is; above, eased into full scale (never clipped) */
{
    const int a = z < 0 ? -z : z;
    if (__builtin_expect(a <= KNEE, 1))
        return z;
    const int d = a - KNEE > 0x60000 ? 0x60000 : a - KNEE;   /* tanh's table ends at 8 anyway */
    const int y = KNEE + (((32767 - KNEE) * tanh_q15((d * KNEE_SCALE) >> 10)) >> 15);
    return z < 0 ? -y : y;
}

/* The drive over a block in shape s (a constant in each copy), the gain ramped from g0 to g1. */
static inline __attribute__((always_inline))
void drive_loop(struct ota_fx *f, short *buf, int drive, int g0, int g1, int s)
{
    int in = f->in, out = f->out, hp_x = f->hp_x, hp_y = f->hp_y, lp = f->lp;
    for (int i = 0; i < 64; ++i) {
        const int x = buf[i];
        in += (((x < 0 ? -x : x) << 8) - in) >> FOLLOW;
        const int y = shape((x * drive) >> 8, s);
        const int h = y - hp_x + hp_y - ((DC_LEAK * hp_y) >> 15);
        hp_x = y;
        hp_y = h;
        lp += (FIZZ * (h - lp)) >> 15;     /* the drive's output as heard (the fizz's lowpass in), then matched */
        out += (((lp < 0 ? -lp : lp) << 8) - out) >> FOLLOW;
        int z = limit((((lp * (g0 + (((g1 - g0) * i) >> 6))) >> 12) * OUT_GAIN) >> 12);
        z = z > 32767 ? 32767 : z;
        z = z < -32768 ? -32768 : z;
        buf[i] = (short)z;
    }
    f->in = in;
    f->out = out;
    f->hp_x = hp_x;
    f->hp_y = hp_y;
    f->lp = lp;
}

void ota_block(struct ota_fx *f, short *buf, const short *par)
{
    const int drive = knob_h(DRIVE_GAIN, par[OTA_POWER]);
    const int s = f->shape >= 0 && f->shape < OTA_SHAPES ? f->shape : OTA_WARM;
    int target, g0 = f->gain;
    if (f->in > SILENT) {                  /* the level in over the drive's level out, from the blocks before */
        const int o = f->out >> 8;
        target = ((f->in >> 8) << 12) / (o > 0 ? o : 1);
        target = target > MATCH_MAX ? MATCH_MAX : target;
        target = (target * (4096 + SQUARE[s] * (drive - 256) / drive)) >> 12;
        target = target < MATCH_MIN ? MATCH_MIN : target > MATCH_MAX ? MATCH_MAX : target;
    } else {                               /* silence: rest at 1 / the drive, so a note after it starts soft */
        target = (4096 * 256) / drive;
        target = target < MATCH_MIN ? MATCH_MIN : target;
        g0 = target;
    }
    const int g1 = g0 + (target < g0 ? (target - g0) >> 1 : (target - g0) >> 2);   /* down in 2 blocks, up in 4 */
    switch (s) {                           /* one loop per shape (1.1.3: the shape's choice made once, not a sample) */
    case OTA_HARD: drive_loop(f, buf, drive, g0, g1, OTA_HARD); break;
    case OTA_TUBE: drive_loop(f, buf, drive, g0, g1, OTA_TUBE); break;
    case OTA_FOLD: drive_loop(f, buf, drive, g0, g1, OTA_FOLD); break;
    default: drive_loop(f, buf, drive, g0, g1, OTA_WARM); break;
    }
    f->gain = g1;
}
