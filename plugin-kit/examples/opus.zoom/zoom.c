/* opus.zoom: better zoom: shift + the white encoder on the tape screen zooms in and out, clockwise in. In stock
 * that turn sets the tape speed in percent; the zoom replaces it there (plain white still sets the speed in
 * semitone steps).
 *
 * The tape view's zoom is a float, pixels a second: the target [0x018C1838] and the shown value
 * [0x018C183C], which the view's draw eases toward the target each frame (a doubling settles in about 0.4 s). Stock
 * only sets the target in its fit (0x01A5B0CC, 160 / the span to show, 40 when there is nothing to fit, never below
 * 5), which the draw runs when a loop edit asks for it; so a zoom set here holds until the next loop edit fits the
 * view again. Each detent multiplies the target by the square root of 2 (two detents double it), from stock's floor
 * 5 (about 75 s across the panel; below it the second ticks run together into a bar) to 640 (about 0.6 s across).
 *
 * The manifest's line `uses white_turn shift tape over "zoom the tape view"` does the choosing: built for core 2.67,
 * the plugin's knob handler gets only the turns that line matches (the white encoder, shift held, the tape's
 * screens), so the handler checks none of that itself; `over` says the turn is the OP-1's own there and the plugin
 * replaces it on purpose (the mods list's controls page shows what it replaces). */
#include "opus_plugin.h"

#define ZOOM_TARGET     (*(volatile float *)0x018C1838)        /* pixels a second the view eases to */
#define ZOOM_MIN        5.0f
#define ZOOM_MAX        640.0f
#define UNITS_PER_STEP  4                                      /* one encoder detent, as the menu (CtlKnob scale 4) */
#define ROOT_2          1.41421356f

/* KeyHandler, before the OS: shift + the white encoder on the tape screen, and nothing else (the `uses` line). */
static int zoom_knob(int index, short *delta)
{
    static int acc;
    (void)index;
    acc += *delta >> 10;
    int n = acc / UNITS_PER_STEP;
    acc -= n * UNITS_PER_STEP;
    float z = ZOOM_TARGET;
    if (!(z >= ZOOM_MIN))                  /* not a number, or below the floor */
        z = ZOOM_MIN;
    for (; n > 0; --n)
        z *= ROOT_2;
    for (; n < 0; ++n)
        z /= ROOT_2;
    ZOOM_TARGET = z < ZOOM_MIN ? ZOOM_MIN : z > ZOOM_MAX ? ZOOM_MAX : z;
    return 1;                              /* taken: not the OS's percent tape speed */
}

int opl_init(struct opl_self *self)
{
    opus_on_knob(self, zoom_knob);
    return 0;
}
