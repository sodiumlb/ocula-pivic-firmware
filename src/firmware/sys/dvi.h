/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _DVI_H_
#define _DVI_H_

#include <stdbool.h>
#include <stdint.h>

// Display type
typedef enum
{
    dvi_vga,   // 640x480 60Hz default
    dvi_480p,  // 720x480 60Hz 
    dvi_576p,  // 720x576 50Hz
} dvi_display_t;

// Canvas size.
typedef enum
{
    dvi_console,
    dvi_240_244,    //Oric standard resolution
} dvi_canvas_t;

// Bit depth & pixels per word
typedef enum
{
    dvi_6_rgb111,   //Oric standard bit depth
    dvi_4_rgb332,
    dvi_2_rgb565,
    dvi_1_rgb888,
} dvi_pixel_format_t;

typedef struct
{
    dvi_pixel_format_t pixel_format;    //Only RGB332 supported for now
    uint8_t scale_x;                    //Only 2 or 3 supported for now
    uint8_t scale_y;                    //Only 2 supported for now
    int8_t offset_x;                   
    int8_t offset_y;
} dvi_mode_t;

//TODO Make DVI mode dynamic
//Framebuffer size works when 8bpp and 2x/3x scaling used
#define DVI_ACTIVE_WIDTH_MAX 480
#define DVI_FB_WIDTH 320
#define DVI_FB_HEIGHT 312
extern volatile uint8_t dvi_framebuf[DVI_FB_HEIGHT][DVI_FB_WIDTH];

void dvi_set_display(dvi_display_t display);
void dvi_set_mode(dvi_mode_t *mode);
bool dvi_xreg_canvas(uint16_t *xregs);
int16_t dvi_canvas_height(void);
void dvi_init(void);
void dvi_task(void);

void dvi_print_status(void);

#endif /* _DVI_H_ */
