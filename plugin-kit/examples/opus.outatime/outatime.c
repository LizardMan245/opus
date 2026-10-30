/* opus.outatime: a time-travel synth engine (2026-10-25: the starter pack's, made to show what Opus 2 can do). The
 * sound, shaped on the device: no jump, the time drift for speed, a strong overdrive for power, the ignition for trail, the level kept
 * whatever the settings (ota_dsp.c). The controls, each lit in its encoder's colour:
 *
 *   blue    year, 1955-2015          shift: spread, the arms' detune
 *   green   speed, 0-88 mph          shift: tone, darker or brighter within the year
 *   white   power, 0-1.21 gigawatts  shift: flux, the ensemble (the arms' three-phase pitch modulation)
 *   orange  trail, the ignition      shift: the overdrive's type (warm, hard, tube, fold)
 *
 * The shift layer is knobs 5-7 (spread, flux, tone), kept with the sound as the four are; a shift turn moves its knob
 * 3 of 99 a detent. Tone is green's and flux white's (2026-10-26, from playing it); the knobs stay
 * where sounds keep them. The OS's envelope (T2) shapes the voices. The page
 * (ota_page.c): the time-machine art (v3), its instruments by meaning (2026-10-26); until its art is
 * ready the controls' names and values (lower case: the font's capitals are icons). Its setting "always
 * driving" (off; the mods list's blue push) keeps the car driving at the set speed with its fire, no key down
 * (2026-10-26). The overdrive's type (warm, hard, tube, fold; 2026-10-27) chooses power's shape: on shift +
 * orange (1.2, 2026-10-30: BarneyBrown's idea, op-forums t32014 #95: the drive type within the synth itself, as
 * something changed often), the sound's own since 1.3 (2026-10-30: kept with the sound and its presets, not
 * device-wide; a setting from 1.1 to 1.2): its engine data's byte, kept with the sound and its
 * presets; warm without one (a new sound). */
#include "opus_plugin.h"
#include "op1fw.h"
#include "ota_dsp.h"
#include "ota_page.h"
#include "ota_tables.h"

#define SHIFT_STEP      3                  /* of 99, a detent */

static const signed char SHIFT_KNOB[3] = { OTA_SPREAD, OTA_TONE, OTA_FLUX };   /* blue, green, white under shift */

static struct opl_self *me;
static struct ota_fx fx;
static volatile short heard[8];            /* the newest voice's knobs (the LFO in), for the sum's chain */
/* For the page: a voice whose key is down (the car drives), the newest note's voice (its fire, the arms' drift), the
 * blocks rendered (a key the OS never released stops counting once its voice stops sounding). */
struct voice {
    struct ota_voice v;
    int held;
};
static volatile int held_n;
static const struct ota_voice *volatile newest;
static volatile unsigned blocks;
static volatile int always;                /* the setting "always driving" (the page) */
/* Where its time goes (the tests read them): every voice's render in one audio block, and the block's chain; the
 * worst of each. */
volatile unsigned ota_render_max, ota_block_max;
static unsigned render_sum;

static inline unsigned cycles(void)
{
    unsigned c;
    __asm__ volatile ("%0 = CYCLES;" : "=d"(c));
    return c;
}

static int k99(int v)                      /* a knob as 0-99, rounded (a turn stores (v x 32767 + 98) / 99) */
{
    return ((v < 0 ? 0 : v) * 99 + 16383) / 32767;
}

/* ---------------------------------------------------------------- the voices */

static void v_init(void *voice, int index)
{
    (void)index;
    ota_voice_init(&((struct voice *)voice)->v);
    ((struct voice *)voice)->held = 0;
}

static void v_note_on(void *voice, const short *knobs)
{
    struct voice *const w = (struct voice *)voice;
    (void)knobs;
    ota_note_on(&w->v);
    if (!w->held)
        ++held_n;
    w->held = 1;
    newest = &w->v;
}

static void v_release(void *voice, const short *knobs)
{
    struct voice *const w = (struct voice *)voice;
    (void)knobs;                           /* the OS's envelope lets it go */
    if (w->held)
        --held_n;
    w->held = 0;
}

static void v_render(void *voice, short *out, const short *par, const unsigned *pitch)
{
    for (int k = 0; k < 8; ++k)
        heard[k] = par[k];
    const unsigned t = cycles();
    ota_render(&((struct voice *)voice)->v, out, par, pitch[0]);
    render_sum += cycles() - t;
}

/* The current sound's overdrive type: its engine data's first byte (opus_engine_data, any thread), warm without one. */
static int sound_shape(void)
{
    unsigned char d;
    return opus_engine_data(&d, 1) == 1 && d < OTA_SHAPES ? d : OTA_WARM;
}

static void v_block(short *buf)
{
    short par[8];
    for (int k = 0; k < 8; ++k)
        par[k] = heard[k];
    fx.shape = sound_shape();              /* the sounding sound's (the block runs while it is the current one) */
    const unsigned t = cycles();
    ota_block(&fx, buf, par);
    const unsigned b = cycles() - t;
    ota_block_max = b > ota_block_max ? b : ota_block_max;
    ota_render_max = render_sum > ota_render_max ? render_sum : ota_render_max;
    render_sum = 0;
    ++blocks;
}

/* ---------------------------------------------------------------- the controls */

/* T1 with shift: blue, green and white are spread, tone and flux (SHIFT_KNOB), SHIFT_STEP of 99 a detent; orange the
 * overdrive's type, a type a detent, kept in the sound (opus_engine_data_set). The rest the OS's. */
static int e_turn(int encoder, int detents, int page)
{
    if (page != 0 || !fw_shift_held())
        return 0;
    if (encoder < 3 && detents) {
        short k[8];
        opus_engine_live(k);
        const int i = SHIFT_KNOB[encoder];
        int v = k99(k[i]) + SHIFT_STEP * detents;
        v = v < 0 ? 0 : v > 99 ? 99 : v;
        opus_engine_knob_set(i, (v * 32767 + 98) / 99);
    } else if (encoder == 3 && detents) {
        int s = sound_shape() + detents;
        s = s < OTA_WARM ? OTA_WARM : s > OTA_FOLD ? OTA_FOLD : s;
        const unsigned char d = (unsigned char)s;
        opus_engine_data_set(&d, 1);
        fx.shape = s;
    }
    return 1;
}

/* ---------------------------------------------------------------- the page */

/* The art (ota_page.c); until it is ready a column for each encoder: its name in its colour, the value under
 * it, a bar; under shift the shift layer's. Each text slot keeps one place and size (draw.c). A key counts as down
 * while the voices render: blocks seen in the last 300 ms (about 28 frames of the OS's 28.5 a second). */
static void page(void)
{
    static const char *const NAME[4] = { "year", "speed", "power", "trail" };
    static const char *const SHIFTED[4] = { "spread", "tone", "flux", "drive" };
    static const char *const SHAPE[OTA_SHAPES] = { "warm", "hard", "tube", "fold" };
    static unsigned seen, quiet;
    short k[8], set[8];
    opus_engine_live(k);
    opus_engine_knobs(set);
    const int shift = fw_shift_held();
    const unsigned b = blocks;
    quiet = b != seen ? 0 : quiet + 1;
    seen = b;
    const int shape = sound_shape();
    if (ota_page_frame(set, k, shift, (held_n > 0 && quiet < 9) || always, newest, shape))
        return;
    opus_page_text(me, 0, 10, 10, 13, OPUS_COLOR_TEXT, "outatime");
    for (int i = 0; i < 4; ++i) {
        static char value[4][16];
        const int x = 10 + 78 * i, y = 52;
        const int lit = 1, v = shift && i < 3 ? k[SHIFT_KNOB[i]] : k[i];
        if (shift && i == 3)
            fw_snprintf(value[i], sizeof value[i], "%s", SHAPE[shape & 3]);
        else if (!shift && i == 0)
            fw_snprintf(value[i], sizeof value[i], "%i", ota_year(v));
        else if (!shift && i == 1)
            fw_snprintf(value[i], sizeof value[i], "%i mph", ota_mph(v));
        else if (!shift && i == 2) {
            const int g = ota_gw100(v);
            fw_snprintf(value[i], sizeof value[i], "%i.%i%i gw", g / 100, (g / 10) % 10, g % 10);
        } else
            fw_snprintf(value[i], sizeof value[i], "%i", k99(v));
        opus_page_text(me, 1 + i, x, y, 13, lit ? OPUS_COLOR_E(i) : OPUS_COLOR_DIM, shift ? SHIFTED[i] : NAME[i]);
        opus_page_text(me, 5 + i, x, y + 24, 15, lit ? OPUS_COLOR_TEXT : OPUS_COLOR_DIM, lit ? value[i] : "-");
        if (shift && i == 3)
            opus_page_bar(x, y + 56, x + 66, y + 60, (unsigned)(shape & 3), OTA_FOLD, OPUS_COLOR_E(i));
        else
            opus_page_bar(x, y + 56, x + 66, y + 60, (unsigned)(v < 0 ? 0 : v), 32767, lit ? OPUS_COLOR_E(i) : OPUS_COLOR_OFF);
    }
    opus_page_text(me, 9, 10, 132, 9, OPUS_COLOR_DIM,
                   shift ? "release shift: year, speed, power, trail" : "shift: spread, tone, flux, drive");
}

/* ---------------------------------------------------------------- the engine */

static const struct opus_engine OUTATIME = {
    .size = sizeof(struct opus_engine),
    .name = "outatime",
    /* a new sound: 1985, 18 mph, 0.31 gigawatts, trail 35; spread 40, flux 30, tone 50 (0-99) */
    .knobs = { 16549, 6620, 8274, 11584, 0, 13239, 9929, 16549 },
    .voice = sizeof(struct voice),
    .init = v_init,
    .note_on = v_note_on,
    .release = v_release,
    .render = v_render,
    .block = v_block,
    .page = page,
    .turn = e_turn,
};

static void setting(const char *name, int value)
{
    if (name[0] == 'd')                    /* drive: "always driving" */
        always = value;
}

int opl_init(struct opl_self *self)
{
    me = self;
    always = opus_setting(self, "drive");
    opus_on_setting(self, setting);
    ota_fx_init(&fx);
    ota_fast((short *)opus_fast(self, (unsigned)ota_fast_bytes()));   /* the era waves in L1, or read in SDRAM */
    ota_page_init(self);                   /* without its art or memory: the plain page */
    return opus_engine_add(self, &OUTATIME) < 0 ? -1 : 0;
}
