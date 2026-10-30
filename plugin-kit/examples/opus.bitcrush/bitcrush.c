/* opus.bitcrush: Opus 1's bitcrush (its fx.c, build 90), its sound unchanged, as an effect plugin on
 * the core's effect service (effects.c). */
#include "opus_plugin.h"
#include "op1fw.h"

typedef unsigned int u32;

static int clamp15(int v) { return v < 0 ? 0 : v > 32767 ? 32767 : v; }

/* The sample-and-hold's phase step per rate step i (16.16): 2^((i - 31) / 6), 44.1 kHz at 31. */
static const u32 RATE_INC[32] = {
    1825, 2048, 2299, 2580, 2896, 3251, 3649, 4096, 4598, 5161, 5793, 6502, 7298, 8192, 9195, 10321,
    11585, 13004, 14596, 16384, 18390, 20643, 23170, 26008, 29193, 32768, 36781, 41285, 46341, 52016, 58386, 65536,
};

struct chan {
    u32 ph;                                /* the hold's phase, 16.16: a new sample when it wraps */
    int held, lp;                          /* the held sample, the low-pass's state */
};

struct state {
    struct chan ch[2];
};

/* The hold, the tone and the mix, each sample; open (the tone's low-pass open) and full (the mix all wet) fixed for the
 * block (2.0.1, 2026-10-28: tested every sample, both mispredicted at the defaults, they cost most of a sample; the
 * same output, a block 3,774 -> 1,639 cycles). */
static inline __attribute__((always_inline))
void crush_n(struct chan *c, short *buf, int n, u32 inc, int step, int half, int a, int mix, int open, int full)
{
    u32 ph = c->ph;
    int held = c->held, lp = c->lp;
    for (int i = 0; i < n; ++i) {
        const int dry = buf[i];
        ph += inc;
        if (ph >= 65536) {
            ph -= 65536;
            const int q = (dry + half) & ~(step - 1);
            held = q > 32767 ? 32767 : q;
        }
        lp = open ? held : lp + (((held - lp) * a) >> 15);
        int y = full ? lp : dry + (((lp - dry) * mix) >> 15);
        y = y > 32767 ? 32767 : y;
        y = y < -32768 ? -32768 : y;
        buf[i] = (short)y;
    }
    c->ph = ph;
    c->held = held;
    c->lp = lp;
}

/* One channel, n samples in place. Rounding to the step, not truncation. */
static void crush(struct chan *c, short *buf, int n, const short *par)
{
    const int p0 = clamp15(par[0]), p1 = clamp15(par[1]), p2 = clamp15(par[2]), mix = clamp15(par[3]);
    const u32 inc = RATE_INC[p0 >> 10];
    const int bits = 1 + (p1 >> 11), step = 1 << (16 - bits), half = step >> 1;
    const int a = 32767 - ((p2 * 31129) >> 15);   /* tone: the low-pass's coefficient, open at 32767 */
    if (a >= 32767) {
        if (mix >= 32767)
            crush_n(c, buf, n, inc, step, half, a, mix, 1, 1);
        else
            crush_n(c, buf, n, inc, step, half, a, mix, 1, 0);
    } else if (mix >= 32767)
        crush_n(c, buf, n, inc, step, half, a, mix, 0, 1);
    else
        crush_n(c, buf, n, inc, step, half, a, mix, 0, 0);
}

/* One instance's needs: its state alone. */
static void need(int stereo, struct opus_need *n)
{
    (void)stereo;
    n->state = sizeof(struct state);
}

/* Called on every factory call, in thread and IVG11 context: only resets the state. */
static void init(struct opus_fx *fx)
{
    struct state *const o = (struct state *)fx->state;
    for (int k = 0; k < 2; ++k) {
        o->ch[k].ph = 65535;               /* the first sample is taken at once */
        o->ch[k].held = 0;
        o->ch[k].lp = 0;
    }
}

static void mono(struct opus_fx *fx, short *buf, const short *par)
{
    crush(&((struct state *)fx->state)->ch[0], buf, 64, par);
}

static void stereo(struct opus_fx *fx, short *l, short *r, const short *par)
{
    struct state *const o = (struct state *)fx->state;
    crush(&o->ch[0], l, 64, par);
    crush(&o->ch[1], r, 64, par);
}

/* One period of a sine, 128 points (Q15, 0.8 of full scale): the picture's test signal, a tone of 344 Hz at
 * 44.1 kHz, so the hold's steps show as they would. */
static const short SINE[OPUS_FX_WAVE] = {
    0, 1286, 2569, 3846, 5114, 6369, 7609, 8831, 10032, 11208, 12357, 13476, 14563, 15615, 16630, 17604,
    18536, 19423, 20263, 21055, 21796, 22484, 23118, 23697, 24218, 24681, 25085, 25428, 25710, 25930, 26087, 26182,
    26214, 26182, 26087, 25930, 25710, 25428, 25085, 24681, 24218, 23697, 23118, 22484, 21796, 21055, 20263, 19423,
    18536, 17604, 16630, 15615, 14563, 13476, 12357, 11208, 10032, 8831, 7609, 6369, 5114, 3846, 2569, 1286,
    0, -1286, -2569, -3846, -5114, -6369, -7609, -8831, -10032, -11208, -12357, -13476, -14563, -15615, -16630, -17604,
    -18536, -19423, -20263, -21055, -21796, -22484, -23118, -23697, -24218, -24681, -25085, -25428, -25710, -25930, -26087, -26182,
    -26214, -26182, -26087, -25930, -25710, -25428, -25085, -24681, -24218, -23697, -23118, -22484, -21796, -21055, -20263, -19423,
    -18536, -17604, -16630, -15615, -14563, -13476, -12357, -11208, -10032, -8831, -7609, -6369, -5114, -3846, -2569, -1286,
};

/* Its page: rate in kHz, bits, tone and mix 0-99, and the effect applied to the test sine. */
static void view(const short *par, struct opus_fx_view *v)
{
    static const char *const label[4] = { "rate", "bits", "tone", "mix" };
    static char value[4][16];
    static short wave[OPUS_FX_WAVE];
    const u32 hz = (44100u * RATE_INC[clamp15(par[0]) >> 10] + 32768) >> 16;
    if (hz >= 10000)
        fw_snprintf(value[0], 16, "%u khz", (hz + 500) / 1000);
    else
        fw_snprintf(value[0], 16, "%u.%u khz", hz / 1000, (hz % 1000 + 50) / 100 > 9 ? 9 : (hz % 1000 + 50) / 100);
    fw_snprintf(value[1], 16, "%i", 1 + (clamp15(par[1]) >> 11));
    fw_snprintf(value[2], 16, "%i", clamp15(par[2]) * 99 / 32767);
    fw_snprintf(value[3], 16, "%i", clamp15(par[3]) * 99 / 32767);
    struct chan c = { 65535, 0, 0 };
    for (int i = 0; i < OPUS_FX_WAVE; ++i)
        wave[i] = SINE[i];
    crush(&c, wave, OPUS_FX_WAVE, par);
    for (int i = 0; i < 4; ++i) {
        v->label[i] = label[i];
        v->value[i] = value[i];
    }
    v->wave[0] = wave;
    v->waves = 1;
}

/* Bitcrush: one rate step and one bit per detent (1024 x 8, 2048 x 8); tone and mix 4 % a detent (Opus 1's
 * fx_limits row); params 4-7 unused, step 0. */
static const struct opus_effect BITCRUSH = {
    .size = sizeof(struct opus_effect),
    .name = "bitcrush",
    .defaults = { 19968, 15360, 0, 32767, 0, 0, 0, 0 },
    .limits = { { 0, 32767, 8192 }, { 0, 32767, 16384 }, { 0, 32767, 10485 }, { 0, 32767, 10485 },
                { 0, 32767, 0 }, { 0, 32767, 0 }, { 0, 32767, 0 }, { 0, 32767, 0 } },
    .need = need,
    .init = init,
    .mono = mono,
    .stereo = stereo,
    .view = view,
};

int opl_init(struct opl_self *self)
{
    return opus_effect_register(self, &BITCRUSH) < 0 ? -1 : 0;
}
