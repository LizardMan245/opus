/* castle's T1 page (castle_page.c). */
#ifndef CASTLE_PAGE_H
#define CASTLE_PAGE_H

void castle_page_init(void);                                  /* opl_init: the sine table */
int castle_page_draw(const short *knobs, unsigned now_ms, int drone);   /* the frame; 0 without a pixel frame */

#endif
