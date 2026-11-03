/* castle's sound: a drone that sustains for ever. Plain C, fixed point (the Blackfin has no floating point), no
 * library; the engine (castle.c) and the computer's renderer (host/render.c) build this same file.
 *
 * A voice (88.2 kHz): the oscillator and detune of the Lode port (displayname808.lode, after Lode by Fors, MIT licence):
 * Lode's pm saw (two sine operators with frequency-compensated feedback, 14 x (0.5 - f/fs)^4) and a second one
 * detuned from it, into a state-variable lowpass, a soft overdrive and the voice's own level. Lode's code is copied as
 * the port has it: mul15, sine, pow4, pmc_set, saw_eval, the detune's type, semitones, Hz and mix, its smoothing and
 * its control step of 16 samples (the saw's half of it; Lode's pulse, shape and sub are not used).
 *
 *   DETUNE (blue):   Lode's detune: up to 10 % of the knob the second saw is detuned in Hz (up to 6.4 Hz at 5 %,
 *                    slow beating), above it in semitones (up to 12 at the top), a blend between; at 0 the second saw
 *                    fades out.
 *   DRIFT (green):   a slow random walk (a new target every 2.9 s down to 0.18 s) that moves the pitch up to
 *                    12 cents and the filter up to an octave either way.
 *   CRACKLE (white): hiss, sparse crackles (up to about 40 a second) and drop-outs where the level dips for a
 *                    moment: the castle's flicker.
 *   HEAT (orange):   opens the filter (120 Hz to about 4 kHz) and drives it into a soft clip, up to 8 times, the level
 *                    made up so a hot drone is not much louder than a cold one.
 *
 * Sustain: the engine shapes its own level (OPUS_ENGINE_OWN_ENVELOPE). A note fades in over 2 s. In drone mode
 * (ca_drone, the orange push) it then stays: letting the key go changes nothing, and the next note takes a new number
 * (ca_current), so every older voice fades out over 3 s while the new one fades in: one drone at a time, crossfaded.
 * Out of drone mode a voice sounds while its key is down and fades over 3 s when it goes up, chords and all; switching
 * drone mode off lets a held-over drone fade the same way. A voice tells the core it has finished only once it is
 * silent and should not be sounding. */
#include "castle_dsp.h"
#include "castle_tables.h"

volatile unsigned ca_current;
volatile int ca_drone = 1;
volatile unsigned ca_blocks;               /* audio blocks so far (ca_block_end) */
static struct ca_voice *volatile cur_voice, *volatile prev_voice;   /* the newest note's voice, the one before it */
/* The sine as pairs: each step's value and its rise to the next in one word (the rise high, the value low), so a lookup
 * is one load and one 16 x 16 multiply. Made from SINE at the start: the same numbers. */
static int sine_sdram[1024];
static int *sine_pairs = sine_sdram;
/* The block's work buffer (the saws, then each stage's output): beside the pairs in fast memory when there is some. One
 * voice renders at a time, so one buffer serves them all. */
static int work_sdram[CA_BLOCK];
static int *work = work_sdram;

static void pairs(int *to)
{
    for (int i = 0; i < 1024; ++i)
        to[i] = (int)(((unsigned)(SINE[i + 1] - SINE[i]) << 16) | ((unsigned)SINE[i] & 0xFFFFu));
}

static int paired;

void ca_fast(int *to)
{
    pairs(sine_sdram);
    paired = 1;
    if (to) {
        pairs(to);
        sine_pairs = to;
        work = to + 1024;
    }
}

void ca_block_end(void)
{
    ++ca_blocks;
}
#ifdef CA_DEBUG
int ca_dbg_osc[CA_BLOCK];
#endif

#define ATTACK          95                 /* Q24 a sample: 2 s to full */
#define RELEASE         63                 /* Q24 a sample: 3 s to silence */
#define RELEASE_FAST    1268               /* Q24 a sample: 0.15 s, for a voice past the newest two (0.3.3) */
#define AMP_FULL        (1 << 24)
#define DRIFT_PITCH     459                /* Q16: 0.7 %, 12 cents */
#define F_BASE          140                /* Q15: the filter's coefficient at 60 Hz, 2 pi 60 / 88200 */
#define F_MAX           19000              /* Q15: about 9 kHz, inside the filter's stable range */
#define DAMP            24000              /* Q15: the filter's damping (1/Q), a gentle resonance */

/* The heat's make-up gain (Q12) at heat 0, 4096 .. 32768: measured on the computer's render (host/render.c, a C2 with
 * the other knobs at 20, the Lode saws) so the drone keeps its level, about -15 dBFS, from cold to hot. */
static const short MAKEUP[9] = { 2779, 2525, 2103, 1746, 1507, 1363, 1278, 1227, 1191 };

static int clamp_knob(int v)
{
    return v < 0 ? 0 : v > 32767 ? 32767 : v;
}

static unsigned rnd(unsigned *s)           /* a 32-bit LCG */
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

/* ---------------------------------------------------------------- the Lode port's oscillator (lode_dsp.c, unchanged) */

#define SUBBLK     16                  /* a control step, samples */
#define SM_COEF    2523                /* 1 - (1 - 0.005)^16, Q15: Lode's one-pole smoother over a control step */
#define DM_COEF    52                  /* 1 - (1 - 0.0001)^16, Q15: the detune mix's */
#define OFF_SAW    0xD45A1CACu         /* -0.25 + 0.0795 cycles (mod 1), Q32 */

/* a x b >> 15 for a 32-bit a and a 16-bit b: two 16 x 16 multiplies */
static inline int mul15(int a, int b)
{
    const int al = (short)a;
    const int ah = (a - al) >> 16;
    return (((int)(short)ah * (int)(short)b) << 1) + (((int)(short)al * (int)(short)b) >> 15);
}

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* rise x f: the pair's high half by f (both fit 16 bits: a rise under 202, f under 32,768), one cycle on the Blackfin */
static inline int rise_by(int w, int f)
{
#ifdef __bfin__
    int r;
    __asm__ ("%0 = %1.H * %2.L (IS);" : "=d"(r) : "d"(w), "d"(f));
    return r;
#else
    return (w >> 16) * f;
#endif
}

/* a x b, both known to fit 16 bits (so the same product): the Blackfin's one-cycle 16 x 16 multiply */
static inline int m16(int a, int b)
{
#ifdef __bfin__
    int r;
    __asm__ ("%0 = %1.L * %2.L (IS);" : "=d"(r) : "d"(a), "d"(b));
    return r;
#else
    return a * b;
#endif
}

static inline int sine(unsigned ph)    /* sin(2 pi x), x = ph / 2^32: Q15 (SINE's value and interpolation exactly) */
{
    const int w = sine_pairs[ph >> 22];
    const int f = (int)((ph >> 7) & 0x7FFF);
    return (int)(short)w + (rise_by(w, f) >> 15);
}

static inline int sine_t(const int *tab, unsigned ph)   /* sine() from a table pointer held in a register */
{
    const int w = tab[ph >> 22];
    const int f = (int)((ph >> 7) & 0x7FFF);
    return (int)(short)w + (rise_by(w, f) >> 15);
}

/* One saw's constants for a control step: Lode's pm factor 14 x (0.5 - n)^4 and the feedback factor 13 x (0.5 - n)^4,
 * n = f / fs (the port's struct pmc, its saw fields). */
struct pmc {
    int sh;                            /* the saw's shaper 0.2 x factor, Q15 */
    int fsa, fsb;                      /* the saw's feedbacks 0.4 and 0.3 x fbfac(n), Q16 */
};

static int pow4(int x)                 /* x^4, x and result Q15 */
{
    const int x2 = (x * x) >> 15;
    return (x2 * x2) >> 15;
}

static void pmc_set(struct pmc *c, unsigned step)
{
    const int n = (int)(step >> 17);                                /* Q15 of f / fs */
    const int x4 = pow4(16384 - n);
    const int factor = 14 * x4;                                     /* Q15, up to 28672 */
    const int fb1 = 13 * x4;                                        /* Q15, up to 26624 */
    c->sh = (factor * 6554) >> 15;                                  /* 0.2 */
    c->fsa = (fb1 * 26214) >> 15;                                   /* 0.4 */
    c->fsb = (fb1 * 19661) >> 15;                                   /* 0.3 */
}

/* Lode's saw: two operators on p: the first's output x 0.2 x factor shapes the second. */
static inline __attribute__((always_inline)) int saw_eval(unsigned p, int *fb, const struct pmc *c)
{
    const int a = sine(p + OFF_SAW + (unsigned)fb[0]);
    fb[0] = m16(a, c->fsa) + (fb[0] >> 1);                  /* |a| <= 32,767; fsa, sh, fsb under 21,300 */
    const int sh = m16(a, c->sh) >> 2;
    const int b = sine(p + OFF_SAW + (unsigned)fb[1] + ((unsigned)sh << 4));
    fb[1] = m16(b, c->fsb) + (fb[1] >> 1);
    return b;
}

/* 2^(x / 4096) x 4096 for x in 0 .. 8 x 4096: the octave's shift and a quadratic for its fraction (within 0.3 %). */
static int exp2_q12(int x)
{
    const int i = x >> 12, f = x & 4095;
    const int frac = 4096 + ((f * (2689 + ((f * 1407) >> 12))) >> 12);   /* 1 + 0.6565 f + 0.3435 f^2 */
    return frac << i;
}

/* a x b, a signed and b unsigned (both 16 bits): the Blackfin's mixed-mode multiply, MAC1's (an odd register) */
static inline int mul_su(int a, int b)
{
#ifdef __bfin__
    int r;
    __asm__ ("%0 = %1.L * %2.L (M, IS);" : "=W"(r) : "d"(a), "d"(b));
    return r;
#else
    return a * b;
#endif
}

/* A soft clip, Q15 in and out: x - 4/27 x^3 up to 1.5, then flat at 1 (the curve's top). Without a branch: the input
 * held to +-1.5 (MIN, MAX), the curve, the result held to +-32,767, and below -1.5 a conditional move, since the curve's
 * floor there gives -32,766 where the flat part is -32,767 (above 1.5 the held input gives 32,767 by itself). The same
 * value as the branching form for every input (checked for all of -524,288 .. 524,288, past what the filter can give). */
static inline int soft_clip(int x)
{
    int xc = x > 49152 ? 49152 : x;
    xc = xc < -49152 ? -49152 : xc;
    const int h = xc >> 1;                 /* Q14, |h| <= 24576 */
    const int h3 = mul_su(h, (int)((unsigned)m16(h, h) >> 14)) >> 14;   /* h^2 >> 14 is up to 36,864: unsigned */
    int y = (h - ((h3 * 4855) >> 15)) << 1;   /* 4/27 as Q15: no divide (the Blackfin has none in hardware) */
    y = y > 32767 ? 32767 : y;
    y = y < -32767 ? -32767 : y;
    return x < -49152 ? -32767 : y;
}

/* One saw over a control step (Lode's saw_eval, sample for sample): its phase stepped by st (SUBBLK steps and a 0),
 * its two feedbacks, into o (mode 0), added to it (1) or added at the detune's mix dm (2). One loop per saw keeps every
 * value of the loop in a register. On the Blackfin the loop is castle_saw.S's (the same numbers); the C is the
 * computer's (host/render.c) and the reference the assembly is checked against. */
struct ca_saw {                        /* castle_saw.S reads it by these offsets */
    unsigned q;                        /* 0: the phase, the saw's offset in it */
    int f0, f1;                        /* 4, 8: the operators' feedbacks */
    int c1;                            /* 12: fsa (low half), sh (high) */
    int c2;                            /* 16: 0x070F (EXTRACT's place 7, length 15: the fraction) | fsb << 16 */
    int dm;                            /* 20: the detune's mix, under 32,768 (mode 2) */
    const unsigned *st;                /* 24: SUBBLK + 1 steps, the last 0 */
    int *out;                          /* 28 */
    const int *tab;                    /* 32: the sine pairs */
};
#if defined __bfin__ && !defined CA_SAW_C
void ca_saw_set(struct ca_saw *), ca_saw_add(struct ca_saw *), ca_saw_mix(struct ca_saw *);
#endif

static unsigned saw_run(unsigned p, const unsigned *st, int *fbp, const struct pmc *c, int *o, const int *tab,
                        int mode, int dm)
{
#if defined __bfin__ && !defined CA_SAW_C
    struct ca_saw s = { p + OFF_SAW, fbp[0], fbp[1], (c->fsa & 0xFFFF) | (c->sh << 16), 0x070F | (c->fsb << 16),
                        dm, st, o, tab };
    if (mode == 0)
        ca_saw_set(&s);
    else if (mode == 1)
        ca_saw_add(&s);
    else
        ca_saw_mix(&s);
    fbp[0] = s.f0;
    fbp[1] = s.f1;
    return s.q - OFF_SAW;
#else
    int f0 = fbp[0], f1 = fbp[1];
    const int fsa = c->fsa, shc = c->sh, fsb = c->fsb;
    unsigned q = p + OFF_SAW;
    for (int k = 0; k < SUBBLK; ++k) {
        q += st[k];
        const int a = sine_t(tab, q + (unsigned)f0);
        f0 = m16(a, fsa) + (f0 >> 1);
        const int sh = m16(a, shc) >> 2;
        const int b = sine_t(tab, q + (unsigned)f1 + ((unsigned)sh << 4));
        f1 = m16(b, fsb) + (f1 >> 1);
        if (mode == 0)
            o[k] = b;
        else if (mode == 1)
            o[k] += b;
        else
            o[k] += m16(b, dm) >> 15;
    }
    fbp[0] = f0;
    fbp[1] = f1;
    return q - OFF_SAW;
#endif
}

void ca_voice_init(struct ca_voice *v, int index)
{
    if (!paired) {                         /* without ca_fast (the computer's renders): the pairs in SDRAM */
        pairs(sine_sdram);
        paired = 1;
    }
    unsigned char *p = (unsigned char *)v;
    for (unsigned i = 0; i < sizeof *v; ++i)   /* by hand: no memset (no C library) */
        p[i] = 0;
    v->seed = 0x9E3779B9u * (unsigned)(index + 1);
    v->id = ca_current - 1;                /* not the current drone: silent, finished */
    if (cur_voice == v)
        cur_voice = 0;
    if (prev_voice == v)
        prev_voice = 0;
    v->gate = v->gate_to = 32767;
}

void ca_note_on(struct ca_voice *v)
{
    if (v->amp == 0) {                     /* a voice starting from silence: its oscillators anywhere */
#ifndef CA_TEST_PHASE0
        v->ph[0] = rnd(&v->seed);
        v->ph[1] = v->ph[0];
#endif
        for (int k = 0; k < 4; ++k)
            v->fb[k] = 0;
        v->low = v->band = 0;
        v->primed = 0;
    }
    /* A voice already sounding (the OS's mono play retriggers it) keeps its level: it glides on, no click. */
    v->id = ++ca_current;
    v->held = 1;
    v->dead = 0;
    v->fresh = 1;                          /* its first block looks for the same note sounding in another voice */
    if (cur_voice != v)
        prev_voice = cur_voice;
    cur_voice = v;
}

void ca_release(struct ca_voice *v)
{
    v->held = 0;                           /* drone mode: nothing changes; off: the voice fades */
}

/* Whether the voice should be sounding: in drone mode the newest note's voice, held or not (the others fade); out of
 * it every voice whose key is down. */
static inline int sounding(const struct ca_voice *v)
{
    return ca_drone ? v->id == ca_current : v->held;
}

int ca_finished(const struct ca_voice *v)
{
    return v->amp == 0 && (v->dead || !sounding(v));
}

/* The same note again: the new voice takes over the sounding one's whole state (its phases, filter, level, drift), so
 * the note goes on as it was, with no second voice fading out beside it: one voice's work, not two for 3 s. The old
 * voice is silenced and freed. Done in the new voice's first block; if the old one has already rendered this block, the
 * new one stays silent for it (the old one's sound is in the block), else the old one goes silent (the new one's is). */
static int same_note(unsigned a, unsigned b)
{
    const unsigned d = a > b ? a - b : b - a;
    return d <= (a >> 10);                 /* within 0.1 % (under 2 cents) */
}

static int take_over(struct ca_voice *v, const unsigned *pitch)
{
    struct ca_voice *const p = prev_voice;
    if (!p || p == v || p->dead || p->amp == 0 || !same_note(p->last_step, pitch[0]))
        return 0;
    const int rendered = p->last_block == ca_blocks;
    const unsigned id = v->id;
    const int held = v->held;
    *v = *p;                               /* a struct copy: the compiler may call memcpy (defined in castle.c) */
    v->id = id;
    v->held = held;
    v->fresh = 0;
    v->dead = 0;
    p->dead = 1;
    p->amp = 0;
    prev_voice = 0;
    return rendered;                       /* 1: this block is the old voice's already */
}

void ca_render(struct ca_voice *v, short *out, const short *par, const unsigned *pitch)
{
    if (v->fresh) {
        v->fresh = 0;
        if (take_over(v, pitch))
            v->dead = 2;                   /* silent this one block, then itself again */
    }
    if (v->dead) {                         /* taken over: silent until freed (2: one block, then sounding again) */
        for (int i = 0; i < CA_BLOCK; ++i)
            out[i] = 0;
        if (v->dead == 2)
            v->dead = 0;
        else
            v->amp = 0;
        v->last_step = pitch[CA_BLOCK - 1];
        v->last_block = ca_blocks;
        return;
    }
    const int detune = clamp_knob(par[CA_DETUNE]), drift_k = clamp_knob(par[CA_DRIFT]);
    const int crackle = clamp_knob(par[CA_CRACKLE]), heat = clamp_knob(par[CA_HEAT]);

    /* The drift's walk, once a block: a new target now and then, eased toward. */
    v->drift_wait -= 1 + (drift_k >> 11);  /* 1 .. 16 a block */
    if (v->drift_wait <= 0) {
        v->drift_wait = 2000;              /* a new target every 2.9 s at the knob's bottom, 0.18 s at its top */
        v->drift_to = (int)(rnd(&v->seed) >> 17) - 16384;   /* -16384 .. 16383 */
        v->drift_to <<= 1;
    }
    v->drift += (v->drift_to - v->drift) >> 6;

    /* The crackle's drop-outs, once a block: now and then the level dips to as low as a fifth, for a few ms. */
    if (--v->gate_wait <= 0) {
        const unsigned r = rnd(&v->seed);
        const int chance = (crackle * crackle) >> 17;   /* 0 .. 8191 of 65536 a step */
        if ((int)(r & 0xFFFF) < chance)
            v->gate_to = 32767 - (((int)(((r >> 16) * 26000u) >> 16) * crackle) >> 15);   /* no divide: 0 .. 25,999 x crackle */
        else
            v->gate_to = 32767;
        v->gate_wait = 6 + (int)((r >> 24) & 15);   /* 9-30 ms a step */
    }

    /* Per block: the detune and drift as Q16 offsets, the filter's coefficient, the drive and its make-up. */
    const int pd = (v->drift * ((drift_k * DRIFT_PITCH) >> 15)) >> 15;
    int oct = 4096 + ((heat * 5) >> 3);                           /* 1 + 5 x heat octaves, Q12 */
    oct += (v->drift * (drift_k >> 3)) >> 15;                      /* the drift's +- 1 octave */
    oct = oct < 0 ? 0 : oct > 7 * 4096 ? 7 * 4096 : oct;
    int f = (F_BASE * exp2_q12(oct)) >> 12;
    f = f > F_MAX ? F_MAX : f;
    const int drive = 4096 + ((((heat * heat) >> 15) * 7) >> 3);   /* 1 .. 8, Q12 */
    const int mi = heat >> 12, mf = heat & 4095;                    /* the make-up, from MAKEUP's nine points */
    const int makeup = MAKEUP[mi] + (((MAKEUP[mi + 1] - MAKEUP[mi]) * mf) >> 12);

    /* The hiss and the crackles' density. */
    const int hiss_amt = (crackle * 2600) >> 15;                   /* Q15: up to 8 % */
    const unsigned crack_chance = (unsigned)((crackle * crackle) >> 15) * 15u;   /* of 2^24 a sample: ~40/s at the top */

    unsigned p0 = v->ph[0], pd_ = v->ph[1];
    int low = v->low, band = v->band, amp = v->amp, gate = v->gate, click = v->click, hiss = v->hiss;
    unsigned seed = v->seed;
    const int target = sounding(v) ? AMP_FULL : 0;
    /* At most two voices' work fading (0.3.3): in drone mode the drone being crossfaded from fades over 3 s, and any
     * older one still fading (fast playing) goes in 0.15 s; out of it the newest three notes' tails keep their 3 s and an
     * older one goes in 0.15 s. Each fading voice costs as much as a sounding one. */
    const unsigned age = ca_current - v->id;                       /* 0: the newest note */
    const int rel = (ca_drone ? age > 1 : age > 2 && !v->held) ? RELEASE_FAST : RELEASE;

    /* Lode's detune knob and its mix: the smoother starts at the knob on a fresh note (as the port's note_on does). */
    const int t_det = detune << 1;                                 /* Q16 */
    const int dmix_t = detune > 0 ? (1 << 23) : 0;
    if (!v->primed) {
        v->sm_det = t_det;
        v->dmix = dmix_t;
        v->primed = 1;
    }

    int *const osc = work;                                        /* the saws, then each stage's output */
    for (int sb = 0; sb < CA_BLOCK; sb += SUBBLK) {
        /* A control step (Lode's): the smoothed detune, its type (Hz below 5 %, semitones from 10 %), the ratio and the
         * Hz step, the oscillators' constants. */
        v->sm_det += ((t_det - v->sm_det) * SM_COEF) >> 15;
        v->dmix += ((dmix_t - v->dmix) >> 5) * DM_COEF >> 10;      /* (diff x 52) >> 15, kept inside 32 bits */
        if (!dmix_t && v->dmix < 256)
            v->dmix = 0;
        const int dm = v->dmix >> 8;                                /* Q15 */
        const int d = v->sm_det;
        const int dtype = (clampi(d, 3277, 6554) - 3277) * 10;                     /* Q15 (0 up to 5 %, 1 from 10 %) */
        const int sem1k = clampi((((12288 * d) >> 16) * dtype) >> 15, 0, 12287);   /* semitones x 1024 */
        const int rd0 = DETRATIO[sem1k >> 4], rd1 = DETRATIO[(sem1k >> 4) + 1];
        const int rd = clampi(rd0 + (((rd1 - rd0) * (sem1k & 15)) >> 4), 0, 32767);   /* 2^(s / 12) - 1, Q15 */
        const int hz = mul15((d * 24349) >> 8, 32767 - clampi(dtype, 0, 32767));   /* Hz detune as a step, x (1 - type) */
        unsigned st0 = pitch[sb];
        st0 += (unsigned)((int)(st0 >> 16) * pd);                  /* castle's drift on the note */
        const unsigned std0 = st0 + (unsigned)mul15((int)st0, rd) + (unsigned)hz;
        struct pmc cm, cd;
        pmc_set(&cm, st0);
        pmc_set(&cd, std0);
        const int use_det = dm > 0;

        /* The step's pitch steps, the note's and its twin's: the drift and detune again only for a new step. */
        unsigned stm[SUBBLK + 1], sdt[SUBBLK + 1];   /* the 17th 0: castle_saw.S reads one ahead */
        unsigned raw = ~pitch[sb], st = 0, stdt = 0;
        for (int k = 0; k < SUBBLK; ++k) {
            if (pitch[sb + k] != raw) {
                raw = pitch[sb + k];
                st = raw + (unsigned)((int)(raw >> 16) * pd);
                stdt = st + (unsigned)mul15((int)st, rd) + (unsigned)hz;
            }
            stm[k] = st;
            sdt[k] = stdt;
        }
        stm[SUBBLK] = sdt[SUBBLK] = 0;

        /* Lode's saw and its detuned twin (the port's osc mix with shape at saw, no sub), Q15 each, one loop each */
        const int *const tab = sine_pairs;
        p0 = saw_run(p0, stm, v->fb, &cm, osc + sb, tab, 0, 0);
        if (!use_det)
            pd_ = p0;                                               /* in step with the note while it is silent */
        else if (dm == 32768)                                       /* at full mix the saw itself */
            pd_ = saw_run(pd_, sdt, v->fb + 2, &cd, osc + sb, tab, 1, 0);
        else
            pd_ = saw_run(pd_, sdt, v->fb + 2, &cd, osc + sb, tab, 2, dm);
#ifdef CA_DEBUG
        for (int k = 0; k < SUBBLK; ++k)
            ca_dbg_osc[sb + k] = osc[sb + k];
#endif
    }

    /* The rest a stage at a time over the block (the filter's, the heat's and the level's settings hold for the whole
     * block): each stage's loop keeps its few values in registers. The same arithmetic, sample for sample. */
    for (int i = 0; i < CA_BLOCK; ++i) {                           /* the filter (Chamberlin), Q14 states */
        const int x = osc[i] >> 1;                                  /* +- 32,767 */
        low += (f * band) >> 15;
        int high = x - low - ((DAMP * band) >> 15);
        high = high > 65535 ? 65535 : high < -65535 ? -65535 : high;   /* the products below stay in 32 bits */
        band += (f * high) >> 15;
        band = band > 65535 ? 65535 : band < -65535 ? -65535 : band;
        low = low > 65535 ? 65535 : low < -65535 ? -65535 : low;
        osc[i] = low;
    }
    for (int i = 0; i < CA_BLOCK; ++i) {                           /* the heat: drive into the soft clip, the make-up */
        int y = (osc[i] * drive) >> 12;                             /* Q15-ish */
        y = soft_clip(y);
        osc[i] = m16(y, makeup) >> 12;                              /* |y| <= 32,767, makeup <= 4,096 */
    }
    if (crackle) {                                                  /* the crackle: hiss, clicks, the drop-outs' gate */
        const int gate_to = v->gate_to;
        for (int i = 0; i < CA_BLOCK; ++i) {
            int y = osc[i];
            const unsigned r = rnd(&seed);
            hiss += ((((int)(r >> 16) - 32768) - hiss) * 9000) >> 15;   /* a soft hiss, not a fizz */
            y += m16(hiss, hiss_amt) >> 15;          /* the hiss stays within 16 bits (it eases toward 16-bit noise) */
            if ((r & 0xFFFFFF) < crack_chance) {     /* rare: the branch is cheaper than a conditional move */
                const int a = 6000 + (int)((r >> 8) & 0x3FFF);   /* 6,000 .. 22,383 */
                click = (r & 0x80000000u) ? -a : a;
            }
            y += click;
            click -= click >> 3;
            gate += (gate_to - gate) >> 7;
            osc[i] = (y * gate) >> 15;
        }
    }
    /* The voice's level: in over 2 s, out over 3 s when it is no longer the current drone; full and staying (amp 1 << 24
     * and its target), y x 32,768 >> 15 = y: no ramp. Then the headroom for the OS's mix of voices. */
    if (amp == AMP_FULL && target == AMP_FULL) {
        for (int i = 0; i < CA_BLOCK; ++i) {
            int y = osc[i];
            y = y > 40000 ? 40000 : y < -40000 ? -40000 : y;
            y = (y * 22000) >> 15;
            out[i] = (short)y;                                      /* |y| <= 26,856 */
        }
    } else {
        for (int i = 0; i < CA_BLOCK; ++i) {
            int y = osc[i];
            y = y > 40000 ? 40000 : y < -40000 ? -40000 : y;
            amp += amp < target ? ATTACK : amp > target ? -rel : 0;
            amp = amp < 0 ? 0 : amp > AMP_FULL ? AMP_FULL : amp;
            y = (y * (amp >> 9)) >> 15;                             /* amp as Q15; 40,000 x 32,768 fits 32 bits */
            y = (y * 22000) >> 15;
            out[i] = (short)y;
        }
    }
    if (amp < 0)
        amp = 0;
    v->ph[0] = p0, v->ph[1] = pd_;
    v->low = low, v->band = band, v->amp = amp, v->gate = gate, v->click = click, v->hiss = hiss;
    v->seed = seed;
    v->last_step = pitch[CA_BLOCK - 1];
    v->last_block = ca_blocks;
}
