/* NIO Config's high-density screen (Palm OS 5, a double-density colour screen such as the Sony Clie TG50's):
 * drawing in the screen's own 320 x 320 pixels and 16-bit colour, text drawn from the game fonts of SYN-ACK WIRE
 * (fonts/: glyph rectangles, drawn by text.c; gfx_palm.c draws). Colours are 0xRRGGBB. */
#ifndef NIO_HIRES_UI_H
#define NIO_HIRES_UI_H

#include "fonts/palm_fonts.h"

#define SCREEN_W 320
#define SCREEN_H 320

typedef unsigned long rgb_t;

#define C_BG0     0x05060FUL
#define C_BG1     0x0A1424UL
#define C_BG2     0x13233AUL
#define C_EDGE    0x2E4466UL
#define C_EDGEHI  0x5C8FB0UL
#define C_TEXT    0xD0DCEEUL
#define C_DIM     0x6F7E99UL
#define C_WHITE   0xFFFFFFUL
#define C_CYAN    0x33DDFFUL
#define C_AMBER   0xFFB822UL
#define C_RED     0xFF3344UL
#define C_GREEN   0x33FF88UL
#define C_MAG     0xFF33CCUL
#define C_VIOLET  0x8A5CFFUL

void gfx_init(void);
void gfx_done(void);
void gfx_begin(unsigned char whole);
void gfx_end(void);
void gfx_rect(short x, short y, short w, short h, rgb_t c);
void gfx_frame(short x, short y, short w, short h, rgb_t c);
void gfx_line(short x0, short y0, short x1, short y1, rgb_t c);
void gfx_pixel(short x, short y, rgb_t c);
void gfx_color(rgb_t c);
void gfx_fill(short x, short y, short w, short h);
const unsigned char *gfx_font(unsigned char font);
void gfx_text(short x, short y, const char *s, unsigned char n, unsigned char font, rgb_t c);
void gfx_clip(short x, short y, short w, short h);
void gfx_unclip(void);

#endif
