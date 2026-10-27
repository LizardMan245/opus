/* outatime's T1 over the owner's time-machine art (ota_page.c). */
#ifndef OTA_PAGE_H
#define OTA_PAGE_H

#include "opus_plugin.h"
#include "ota_dsp.h"

/* From opl_init: the art written to the plugin's folder, the kept pictures' memory; 0 when either failed (the plain
 * page then). */
int ota_page_init(struct opl_self *self);
/* The display thread, inside T1's draw: set the knobs as stored, k as the sound hears them (the LFO in), shift held,
 * held a key down or the setting "always driving" on (the car drives), v the newest note's voice (its ignition and time drift) or 0. 0 when the art is
 * not there (the plain page draws instead). */
int ota_page_frame(const short *set, const short *k, int shift, int held, const struct ota_voice *v);

#endif
