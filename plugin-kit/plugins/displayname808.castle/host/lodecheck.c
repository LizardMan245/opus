/* castle's oscillator against the Lode port's (lode_dsp.c with a hook after its osc mix): the same samples, bit for bit?
 * Built on the computer only:
 *   cc -O2 -DCA_DEBUG -DCA_TEST_PHASE0 -I.. -I LODE_DIR lodecheck.c ../castle_dsp.c LODE_HOOKED.c -o lodecheck -lm */
#include <math.h>
#include <stdio.h>
#include "castle_dsp.h"
#include "lode_dsp.h"

extern int lode_dbg_osc[128];

static unsigned step_of(double note) { return (unsigned)llround(440.0 * pow(2.0, (note - 69) / 12.0) * 4294967296.0 / 88200.0); }

static int run(double note, int det0, int det1, int blocks)
{
    struct ca_voice c;
    struct lode_voice l;
    ca_voice_init(&c, 0);
    lode_voice_init(&l);
    short ck[8] = {0}, lk[8] = {0};
    ck[CA_DETUNE] = (short)det0;
    lk[LODE_DETUNE] = (short)det0;
    lk[LODE_CUT] = 32767;
    ca_note_on(&c);
    lode_note_on(&l, lk);
    unsigned pitch[128];
    const unsigned st = step_of(note);
    for (int i = 0; i < 128; ++i) pitch[i] = st;
    short out[128];
    int diffs = 0;
    for (int b = 0; b < blocks; ++b) {
        if (b == blocks / 2) { ck[CA_DETUNE] = (short)det1; lk[LODE_DETUNE] = (short)det1; }   /* a turn mid-way */
        ca_render(&c, out, ck, pitch);
        lode_render(&l, out, lk, pitch);
        for (int i = 0; i < 128; ++i) diffs += ca_dbg_osc[i] != lode_dbg_osc[i];
    }
    printf("note %5.1f detune %5d -> %5d: %d of %d samples differ\n", note, det0, det1, diffs, blocks * 128);
    return diffs;
}

int main(void)
{
    int bad = 0;
    const int dets[] = { 0, 300, 1638, 3000, 6554, 9929, 20000, 32767 };
    const double notes[] = { 24, 36, 48, 60, 84 };
    for (unsigned n = 0; n < sizeof notes / sizeof *notes; ++n)
        for (unsigned d = 0; d < sizeof dets / sizeof *dets; ++d)
            bad += run(notes[n], dets[d], dets[(d + 3) % 8], 2000);
    printf(bad ? "MISMATCH\n" : "IDENTICAL: castle's oscillator is the Lode port's\n");
    return bad != 0;
}
