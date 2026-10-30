/* outatime's sound (ota_dsp.c): plain C, for the engine and the host renders. */
#ifndef OTA_DSP_H
#define OTA_DSP_H

/* The knobs (0-32767): 0 YEAR, 1 SPEED, 2 POWER, 3 TRAIL (blue, green, white, orange); under shift 5 SPREAD (blue),
 * 6 FLUX (white), 7 TONE (green). Knob 4 is the core's. */
enum { OTA_YEAR, OTA_SPEED, OTA_POWER, OTA_TRAIL, OTA_MARK, OTA_SPREAD, OTA_FLUX, OTA_TONE };

/* A voice: three oscillators (the flux capacitor's arms), at 88.2 kHz. */
struct ota_voice {
    unsigned ph[3];                        /* the arms' phases (2^32 a cycle) */
    unsigned drift;                        /* the time drift's phase; arm k reads it k thirds of a cycle on */
    unsigned flux;                         /* the ensemble's phase, the same way */
    int env;                               /* the ignition: 1 at the note, burning down, Q15 */
    int low, band;                         /* its filter (a state-variable filter) */
};

/* The voices' sum: the overdrive (64 samples at 44.1 kHz). Its shapes (the sound's type: shift + orange). */
enum { OTA_WARM, OTA_HARD, OTA_TUBE, OTA_FOLD, OTA_SHAPES };
struct ota_fx {
    int shape;                             /* OTA_WARM .. OTA_FOLD: the current sound's (outatime.c, each block) */
    int in, out;                           /* the level followers: the sum before the drive, the drive's (Q23) */
    int gain;                              /* the level match now (Q12) */
    int hp_x, hp_y;                        /* the DC blocker after the drive (the tube's asymmetry) */
    int lp;                                /* the fizz's lowpass after the drive */
};

void ota_voice_init(struct ota_voice *v);
void ota_note_on(struct ota_voice *v);
void ota_render(struct ota_voice *v, short *out, const short *par, unsigned pitch);
void ota_fx_init(struct ota_fx *f);
void ota_block(struct ota_fx *f, short *buf, const short *par);
/* The era waves (OTA_WAVES x OTA_WLEN samples) copied to fast memory (L1, opus_fast), or none: then read in SDRAM. */
void ota_fast(short *bank);
int ota_fast_bytes(void);

/* For the page: the knobs as the page shows them. */
int ota_year(int knob);                    /* 1955-2015 */
int ota_mph(int knob);                     /* 0-88 */
int ota_gw100(int knob);                   /* gigawatts x 100: 0-121 */
int ota_arm_year(const short *par, unsigned drift, int k);   /* arm k (0-2) now, by its voice's drift */

#endif
