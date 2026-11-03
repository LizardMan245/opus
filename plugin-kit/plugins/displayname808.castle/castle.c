/* displayname808.castle: a drone engine that sustains for ever. A note fades in and keeps sounding after the key goes
 * up; the next note crossfades to a new drone (one at a time). The orange push on T1 turns drone mode on and off: off,
 * a note sounds while its key is down (chords too) and fades when it goes up; turning it off lets a drone fade. The sound is
 * castle_dsp.c; its oscillator and detune are the Lode port's (displayname808.lode), sample for sample. The controls,
 * T1's encoders as the OS turns them (knobs 0-3):
 *
 *   blue    detune   the Lode port's detune: a second Lode saw, in Hz (slow beating) up to 5 % of the knob,
 *                    in semitones (up to 12) from 10 %
 *   green   drift    a slow wander of the pitch and the filter
 *   white   crackle  hiss, crackles and drop-outs
 *   orange  heat     the filter opening and a soft overdrive, the level kept
 *
 * The engine shapes its own level (OPUS_ENGINE_OWN_ENVELOPE with finished): the OS's envelope (T2) does not run for
 * its voices, the key's release changes nothing, and a voice is freed only once it has faded out. The page: the dotted
 * castle and its four effects (water off the edges, leaves, flicker and lightning, flames), its flag up in drone
 * mode (castle_page.c). */
#include "opus_plugin.h"
#include "op1fw.h"
#include "castle_dsp.h"
#include "castle_page.h"

#define KEY_ORANGE_PUSH 0x3E

static struct opl_self *me;
static int sine_where;                     /* 2: the sine pairs in fast memory, 1: in the audio pool, 0: in the image */

/* Castle's own audio cost, measured on the device with the cycle counter (0.3.4): every voice's render in a block, summed;
 * a second's average and worst, and how many voices rendered. Shown on T1 while shift is held. */
static volatile unsigned cost_block, voices_block;
static volatile unsigned cost_sum, cost_worst, cost_blocks, voices_sum;
static unsigned shown_avg, shown_worst, shown_voices10;

/* ---------------------------------------------------------------- the voices */

/* Drone mode, kept in the sound's own data (one byte: 1 on, 0 off), so it saves and loads with the sound and its presets;
 * on in a new sound. */
static int sound_drone(void)
{
    unsigned char d;
    return opus_engine_data(&d, 1) == 1 ? d != 0 : 1;
}

static void v_init(void *voice, int index)
{
    ca_voice_init((struct ca_voice *)voice, index);
    ca_drone = sound_drone();              /* every sound change builds the voices again: the sound's mode */
}

static void v_note_on(void *voice, const short *knobs)
{
    (void)knobs;
    ca_note_on((struct ca_voice *)voice);
}

static void v_release(void *voice, const short *knobs)
{
    (void)knobs;
    ca_release((struct ca_voice *)voice);  /* in drone mode it changes nothing; out of it the voice fades */
}

static inline unsigned cycles(void)
{
    unsigned c;
    __asm__ volatile ("%0 = CYCLES;" : "=d"(c));
    return c;
}

static void v_render(void *voice, short *out, const short *par, const unsigned *pitch)
{
    const unsigned t = cycles();
    ca_render((struct ca_voice *)voice, out, par, pitch);
    cost_block += cycles() - t;
    ++voices_block;
}

static void v_block(short *buf)            /* after the voices each block: the block count, and the cost's tally */
{
    (void)buf;
    ca_block_end();
    const unsigned c = cost_block;
    cost_sum += c;
    cost_worst = c > cost_worst ? c : cost_worst;
    voices_sum += voices_block;
    ++cost_blocks;
    cost_block = 0;
    voices_block = 0;
}

/* The voice's struct copy in the same-note takeover may become a memcpy call; no C library, so it is here. */
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n);
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

static int v_finished(void *voice)
{
    return ca_finished((const struct ca_voice *)voice);
}

/* ---------------------------------------------------------------- the controls */

static int e_key(int key, int pressed, int page)
{
    if (page != 0 || key != KEY_ORANGE_PUSH || fw_shift_held())
        return 0;
    if (pressed) {                         /* drone mode on or off, kept in the sound */
        const unsigned char d = (unsigned char)!ca_drone;
        opus_engine_data_set(&d, 1);
        ca_drone = d;
    }
    return 1;
}

/* ---------------------------------------------------------------- the page */

/* Shift held on T1: Castle's measured cost over the last second, over the scene (the block: 580,525 cycles). */
static void cost_text(void)
{
    static char l1[48], l2[48];
    const unsigned avg_pm = (shown_avg * 1000u + 290262u) / 580525u, worst_pm = (shown_worst * 1000u + 290262u) / 580525u;
    fw_snprintf(l1, sizeof l1, "castle %i.%i worst %i.%i of 100", (int)(avg_pm / 10), (int)(avg_pm % 10),
                (int)(worst_pm / 10), (int)(worst_pm % 10));
    fw_snprintf(l2, sizeof l2, "%i cycles  voices %i.%i  sine %s", (int)shown_avg, (int)(shown_voices10 / 10),
                (int)(shown_voices10 % 10), sine_where == 2 ? "fast" : sine_where == 1 ? "audio pool" : "image");
    opus_page_text(me, 10, 8, 4, 11, OPUS_COLOR_TEXT, l1);
    opus_page_text(me, 11, 8, 18, 11, OPUS_COLOR_DIM, l2);
}

/* The castle (castle_page.c), its time in ms from the processor's cycles (400 a microsecond, as outatime's page);
 * without a pixel frame the controls' names and values. */
static void page(void)
{
    static unsigned last, now_ms;
    const unsigned c = cycles();
    unsigned dt = last ? (c - last) / 400000u : 0;
    dt = dt > 250 ? 250 : dt;
    last = c;
    now_ms += dt;
    static unsigned window_ms;
    window_ms += dt;
    if (window_ms >= 1000 && cost_blocks) {                        /* a second's tally, then a new one */
        const unsigned n = cost_blocks;
        shown_avg = cost_sum / n;
        shown_worst = cost_worst;
        shown_voices10 = voices_sum * 10u / n;
        cost_sum = cost_worst = voices_sum = cost_blocks = 0;
        window_ms = 0;
    }
    short k[8];
    opus_engine_live(k);
    if (castle_page_draw(k, now_ms, ca_drone)) {
        if (fw_shift_held())
            cost_text();
        return;
    }
    static const char *const NAME[4] = { "detune", "drift", "crackle", "heat" };
    static char value[4][8];
    opus_page_text(me, 0, 10, 10, 13, OPUS_COLOR_TEXT, "castle");
    for (int i = 0; i < 4; ++i) {
        const int x = 10 + 78 * i, y = 52, v = k[i] < 0 ? 0 : k[i];
        fw_snprintf(value[i], sizeof value[i], "%i", (v * 99 + 16383) / 32767);
        opus_page_text(me, 1 + i, x, y, 13, OPUS_COLOR_E(i), NAME[i]);
        opus_page_text(me, 5 + i, x, y + 24, 15, OPUS_COLOR_TEXT, value[i]);
        opus_page_bar(x, y + 56, x + 66, y + 60, (unsigned)v, 32767, OPUS_COLOR_E(i));
    }
}

/* ---------------------------------------------------------------- the engine */

static const struct opus_engine CASTLE = {
    .size = sizeof(struct opus_engine),
    .name = "castle",
    /* a new sound: detune 3 (Lode's Hz detune, about 4 Hz of beating), drift 25, crackle 0, heat 25 (of 99); knob 4 is
     * the core's */
    .knobs = { 994, 8274, 0, 8274, 0, 0, 0, 0 },
    .voice = sizeof(struct ca_voice),
    .init = v_init,
    .note_on = v_note_on,
    .release = v_release,
    .render = v_render,
    .block = v_block,
    .page = page,
    .turn = 0,                             /* the OS turns knobs 0-3 on T1 */
    .key = e_key,
    .flags = OPUS_ENGINE_OWN_ENVELOPE,
    .finished = v_finished,
};

int opl_init(struct opl_self *self)
{
    me = self;
    castle_page_init();
    int *pairs = (int *)opus_fast(self, CA_FAST_BYTES);           /* the sine pairs and the work buffer in L1 */
    sine_where = pairs ? 2 : 0;
    if (!pairs) {                          /* else the audio pool: the display's frames do not slow its reads */
        pairs = (int *)opus_alloc_audio(self, CA_FAST_BYTES);
        sine_where = pairs ? 1 : 0;
    }
    ca_fast(pairs);
    return opus_engine_add(self, &CASTLE) < 0 ? -1 : 0;
}
