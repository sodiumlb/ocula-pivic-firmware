/*
 * Copyright (c) 2025 Sodiumlightbaby
 * Copyright (c) 2025 dreamseal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CVBS_PAL_H_
#define _CVBS_PAL_H_ 
#include "cvbs.pio.h"
// DAC driving logic is using square wave
//L0, L1 and L2 levels to be asserted on the DAC
//Set L0, L1 and L2 to same level to get DC, or use DC_RUN command
//Delay is used to shift the phase a number of sys cycles between each output to the DAC
//Commands for the PIO program (each word 30 bits):
//    dc_run: Repeate L0 for count periods
//            [29:10]=repeat, [9:5]=L0, [4:0]=dc_run_id
//
//     pixel: CVBS pixel data payload for pixel_run commands
//            [31]=truncate, [30:25]=delay1, [24:18]=L1, [17:12]=delay0, [11:5] L0, [4:0]=pixel_id
//     burst: Colour burst special. Alternative to doing pixel_run of burst pixels
//            [31:25]=DC, [24:18]=L1, [17:11]=L0, [10:5]=pre_delay(phase), [4:0]=burst_id

#define CVBS_CMD_PAL_ID_DC_RUN cvbs_pal_offset_cvbs_cmd_dc_run
#define CVBS_CMD_PAL_ID_PIXEL cvbs_pal_offset_cvbs_cmd_pixel
#define CVBS_CMD_PAL_ID_BURST cvbs_pal_offset_cvbs_cmd_burst

#define CVBS_CMD_PAL_DC_RUN(L0,count) \
        (((count-2) << 12) | ((L0&0x7F) << 5)| CVBS_CMD_PAL_ID_DC_RUN)
// Moving from 2x to 1x clock divider for PAL for stability. Using +1 on delay shifts instead. This can be removed
// to get NTSC level of fine tuning, but then the fixed delay values in the palette need to be doubled.
// THis version ignores L2, reuses L0 as the last level and interprets delay1 of 0 (when encoded) as keep L1 output
#define CVBS_CMD_PAL_PIXEL(L0,delay0,L1,delay1,truncate) \
        (((truncate&0x1)<<31) | (((delay1-6)&0x3F) << (25)) | ((L1&0x7F) << 18) | (((delay0-3)&0x3F) << (12)) | ((L0&0x7F) << 5) | CVBS_CMD_PAL_ID_PIXEL)
#define CVBS_CMD_PAL_BURST(L0,L1,DC,delay,count_unused) \
        (((DC&0x7F) << 25) | ((L1&0x7F) << 18) | ((L0&0x7F) << 11) | (((delay-1)&0x3F) << 5) | CVBS_CMD_PAL_ID_BURST)

#define PAL_HSYNC        CVBS_CMD_PAL_DC_RUN( 0,20)
#define PAL_FRONTPORCH   CVBS_CMD_PAL_DC_RUN(36, 7)
#define PAL_FRONTPORCH_1 CVBS_CMD_PAL_DC_RUN(36, 5)        // First part is from last pixel of HC=70 to end of HC=0.
#define PAL_FRONTPORCH_2 CVBS_CMD_PAL_DC_RUN(36, 2)        // Other two in HC=1 up to HC=1.5
#define PAL_BREEZEWAY    CVBS_CMD_PAL_DC_RUN(36, 3)        // Actual breezeway this delay + burst command delay
#define PAL_BACKPORCH    CVBS_CMD_PAL_DC_RUN(36, 4)


// Vertical blanking and sync.
// TODO: From original cvbs.c code. Doesn't match VIC chip timings.
#define PAL_LONG_SYNC_L_CVBS  CVBS_CMD_PAL_DC_RUN( 0,133)
#define PAL_LONG_SYNC_H_CVBS  CVBS_CMD_PAL_DC_RUN( 9,  9)
#define PAL_SHORT_SYNC_L_CVBS CVBS_CMD_PAL_DC_RUN( 0,  9)
#define PAL_SHORT_SYNC_H_CVBS CVBS_CMD_PAL_DC_RUN( 9,133)
#define PAL_LONG_SYNC_L  CVBS_CMD_PAL_DC_RUN( 0,133)
#define PAL_LONG_SYNC_H  CVBS_CMD_PAL_DC_RUN(36,  9)
#define PAL_SHORT_SYNC_L CVBS_CMD_PAL_DC_RUN( 0,  9)
#define PAL_SHORT_SYNC_H CVBS_CMD_PAL_DC_RUN(36,133)

#define PAL_BLANKING     CVBS_CMD_PAL_DC_RUN(36,234)


static const cvbs_palette_t palette_default_pal = {
        .version = 1,
        .burst = { 28,  9,  3, 0 },
        .colours = {
                 { 36,  9,  0, 0 },     //Black
                 { 36, 29,  0, 0 },     //White
                 { 20, 15, -5, 0 },     //Red
                 { 20, 24, +6, 0 },     //Cyan
                 { 10, 18, -5, 0 },     //Purple
                 { 12, 22, +6, 0 },     //Green
                 { 36, 14, +4, 0 },     //Blue
                 { 36, 25, -5, 0 },     //Yellow
                 { 26, 19, -6, 0 },     //Orange
                 { 26, 24, -3, 0 },     //LOrange
                 { 20, 23, -3, 0 },     //Pink
                 { 20, 27, +3, 0 },     //LCyan
                 { 10, 24, -3, 0 },     //LPurple
                 { 12, 26, +3, 0 },     //LGreen
                 { 36, 22, +2, 0 },     //LBlue
                 { 36, 28, -3, 0 }      //LYellow
        }
};


#endif /* _CVBS_PAL_H_ */
 
