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

void dvi_set_display(dvi_display_t display);
bool dvi_xreg_canvas(uint16_t *xregs);
int16_t dvi_canvas_height(void);
void dvi_init(void);
void dvi_task(void);

void dvi_print_status(void);

#endif /* _DVI_H_ */
