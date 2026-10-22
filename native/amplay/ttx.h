/* Teletext for amplay (libzvbi): pages from DVB teletext PES payloads */
#ifndef TTX_H
#define TTX_H

#include <stdint.h>

enum { TTX_PAGE_FULL = 1, TTX_PAGE_MIX, TTX_PAGE_SUBTITLE };

int ttx_init(void);
/* The character sets pages without their own designation are in: the default region of ETS 300
   706 (libzvbi's numbering: 0 West European, Latin; 32 Cyrillic, with Russian and Ukrainian) */
void ttx_region(int region);
/* Another channel: the pages go */
void ttx_reset(void);
/* A PES payload (data_identifier and data units), from the demuxer's thread */
void ttx_feed(const uint8_t *payload, int len);
/* 1 once if the page (BCD, 0x100-0x899) came in since the last call */
int ttx_received(int page);
/* Whether any page came in on this channel yet */
int ttx_any(void);
/* The page as RGBA (malloc'd, w x h) and its red, green, yellow, blue links (0 if none); 0 if the
   page hasn't come in yet */
int ttx_page(int page, int mode, uint8_t **rgba, int *w, int *h, int links[4]);
/* The page number dir pages on, 100-899 round */
int ttx_step(int page, int dir);

#endif
