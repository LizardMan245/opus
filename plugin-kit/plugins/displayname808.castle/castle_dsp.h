/* castle's sound (castle_dsp.c): plain C, no SDK, so the engine (castle.c) and a renderer on the computer
 * (host/render.c) build the same file. */
#ifndef CASTLE_DSP_H
#define CASTLE_DSP_H

/* The knobs (0-32767), T1's encoders: 0 DETUNE (blue), 1 DRIFT (green), 2 CRACKLE (white), 3 HEAT (orange).
 * Knob 4 is the core's (the engine's mark). */
enum { CA_DETUNE, CA_DRIFT, CA_CRACKLE, CA_HEAT, CA_MARK };
#ifdef CA_DEBUG
extern int ca_dbg_osc[128];             /* the last block's oscillator mix, for host/lodecheck.c */
#endif

#define CA_RATE         88200              /* a voice's sample rate (the engine service's) */
#define CA_BLOCK        128                /* samples a voice renders a block */

/* The drone that is sounding now: every note takes a new number. A voice whose number is not the current one fades out;
 * the current one never does in drone mode (it sustains until a new note, or drone mode goes off). */
extern volatile unsigned ca_current;
extern volatile int ca_drone;              /* drone mode: 1 (notes latch, one at a time) or 0 (notes sound while held) */

struct ca_voice {
    unsigned ph[2];                        /* the Lode saws' phases (2^32 a cycle): the note, its detuned twin */
    int fb[4];                             /* their operators' feedback states (Lode's saw: two each) */
    int sm_det;                            /* Lode's smoothed detune knob (Q16) */
    int dmix;                              /* the detuned saw's level (Q23), eased in and out as Lode does */
    int primed;                            /* the smoother has its first value */
    unsigned id;                           /* the drone this voice plays (ca_current at its note) */
    int held;                              /* its key is down */
    int amp;                               /* its level, Q24 (1 << 24 full) */
    int low, band;                         /* its filter (a state-variable lowpass), Q14 */
    unsigned seed;                         /* its noise */
    int drift, drift_to, drift_wait;       /* the drift: a slow random walk, Q15 */
    int gate, gate_to, gate_wait;          /* the crackle's drop-outs: the level they let through, Q15 */
    int click;                             /* a crackle's impulse, decaying */
    int hiss;                              /* the hiss's lowpass state */
    unsigned last_step;                    /* its pitch step at its last block's end (the same-note check) */
    unsigned last_block;                   /* the block it last rendered (ca_blocks) */
    int fresh;                             /* a new note: its first block not yet rendered */
    int dead;                              /* taken over by a newer voice playing the same note: silent until freed */
};

void ca_voice_init(struct ca_voice *v, int index);
void ca_note_on(struct ca_voice *v);
void ca_render(struct ca_voice *v, short *out, const short *par, const unsigned *pitch);
int ca_finished(const struct ca_voice *v);
void ca_release(struct ca_voice *v);       /* its key went up */
void ca_block_end(void);                   /* each audio block's end (the engine's block handler) */
void ca_fast(int *mem);                    /* CA_FAST_BYTES: the sine pairs and the block's work buffer, or 0 */
#define CA_FAST_BYTES   ((1024 + CA_BLOCK) * 4)   /* 4,608: fast memory, else the audio pool */

#endif
