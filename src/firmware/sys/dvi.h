/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _DVI_H_
#define _DVI_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Bit depth & pixels per word
typedef enum
{
    dvi_6_rgb111,   //Oric standard bit depth
    dvi_4_rgb332,   //Currently the only supported DVI buffert type
    dvi_2_rgb565,
    dvi_1_rgb888,
} dvi_pixel_format_t;

typedef enum
{
    vneg_hneg,
    vneg_hpos,
    vpos_hneg,
    vpos_hpos,
} dvi_sync_polarity_t;

typedef struct
{
    dvi_pixel_format_t pixel_format;    //Only RGB332 supported for now
    uint8_t scale_x;                    //Only 2,3 or 4 supported for now
    uint8_t scale_y;                    //Only 1 or 2 supported for now
    int16_t offset_x;                   
    int16_t offset_y;
    uint8_t hstx_div;
    uint16_t h_front_porch;
    uint16_t h_sync_width;
    uint16_t h_back_porch;
    uint16_t h_active_pixels;
    uint16_t v_front_porch;
    uint16_t v_sync_width;
    uint16_t v_back_porch;
    uint16_t v_active_lines;
    dvi_sync_polarity_t sync_polarity;
} dvi_modeline_t;

//TODO Make DVI mode dynamic
//Framebuffer size works when 8bpp and 2x/3x scaling used
#define DVI_FB_WIDTH 320
#define DVI_FB_HEIGHT 312
extern volatile uint8_t dvi_framebuf[DVI_FB_HEIGHT][DVI_FB_WIDTH];

//Modes and modelines are defined and set from the primary display systems (e.g. VIC or ULA)
void dvi_set_modeline(dvi_modeline_t *ml);
void dvi_print_modeline(dvi_modeline_t *ml);
void dvi_init(void);
void dvi_task(void);

void dvi_print_status(void);

void dvi_mon_modeline(const char *args, size_t len);


#endif /* _DVI_H_ */
