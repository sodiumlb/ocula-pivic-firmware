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
//            [29:25]=L2, [24:20]=delay1, [19:15]=L1, [14:10]=delay0, [9:5] L0, [4:0]=pixel_id
//     burst: Colour burst special. Alternative to doing pixel_run of burst pixels
//            [29:25]=L1, [24:20]=L0, [19:15]=pre_delay(phase), [14:5]=repeat(burst periods), [4:0]=burst_id

#define CVBS_CMD_PAL_ID_DC_RUN cvbs_pal_offset_cvbs_cmd_dc_run
#define CVBS_CMD_PAL_ID_PIXEL cvbs_pal_offset_cvbs_cmd_pixel
#define CVBS_CMD_PAL_ID_BURST cvbs_pal_offset_cvbs_cmd_burst

#define CVBS_CMD_PAL_DC_RUN(L0,count) \
        (((count-2) << 10) | ((L0&0x1F) << 5)| CVBS_CMD_PAL_ID_DC_RUN)
#define CVBS_CMD_PAL_PIXEL(L0,delay0,L1,delay1,L2) \
        (((L2&0x1F) << 25) |  (((delay1-2)&0x1F) << 20) | ((L1&0x1F) << 15) | (((delay0-2)&0x1F) << 10) | ((L0&0x1F) << 5) | CVBS_CMD_PAL_ID_PIXEL)
#define CVBS_CMD_PAL_BURST(L0,L1,DC,delay,count) \
        (((DC&0x1F) << 25) | ((L1&0x1F) << 20) | ((L0&0x1F) << 15) | (((delay-1)&0x1F) << 10) | (((count-1)&0x1F) << 5) | CVBS_CMD_PAL_ID_BURST)

#define CVBS_CMD_PAL_BURST_DELAY(cmd,delay) \
        ((cmd & ~(0x1F<<10)) | ((delay-1)&0x1F)<<10)

#define PAL_HSYNC        CVBS_CMD_PAL_DC_RUN( 0,20)
#define PAL_FRONTPORCH   CVBS_CMD_PAL_DC_RUN(18, 8)
#define PAL_FRONTPORCH_1 CVBS_CMD_PAL_DC_RUN(18, 2)    // First two in second half of HC=70
#define PAL_FRONTPORCH_2 CVBS_CMD_PAL_DC_RUN(18, 6)    // Other six in HC=0 up to HC=1.5
#define PAL_BREEZEWAY    CVBS_CMD_PAL_DC_RUN(18, 3)    // Actual breezeway this delay + burst command delay
#define PAL_BACKPORCH    CVBS_CMD_PAL_DC_RUN(18, 4)

#define PAL_COLBURST_O	 CVBS_CMD_PAL_BURST( 6,12,18,14,16)
#define PAL_COLBURST_E	 CVBS_CMD_PAL_BURST(12, 6,18, 5,16)

// Vertical blanking and sync.
// TODO: From original cvbs.c code. Doesn't match VIC chip timings.
#define PAL_LONG_SYNC_L  CVBS_CMD_PAL_DC_RUN( 0,133)
#define PAL_LONG_SYNC_H  CVBS_CMD_PAL_DC_RUN(18,  9)
#define PAL_SHORT_SYNC_L CVBS_CMD_PAL_DC_RUN( 0,  9)
#define PAL_SHORT_SYNC_H CVBS_CMD_PAL_DC_RUN(18,133)

#define PAL_BLANKING     CVBS_CMD_PAL_DC_RUN(18,232)

//"Tobias" colours - approximated
#define PAL_BLACK	 CVBS_CMD_PAL_PIXEL(18,18,18, 3,18)
#define PAL_WHITE	 CVBS_CMD_PAL_PIXEL(23,18,23, 3,23)
#define PAL_RED_O	 CVBS_CMD_PAL_PIXEL(10,10, 5,18,10)
#define PAL_RED_E	 CVBS_CMD_PAL_PIXEL( 5, 8,10,18, 5)
#define PAL_CYAN_O	 CVBS_CMD_PAL_PIXEL(15,10, 9,18,15)
#define PAL_CYAN_E	 CVBS_CMD_PAL_PIXEL( 9, 8,15,18, 9)
#define PAL_PURPLE_O     CVBS_CMD_PAL_PIXEL(22, 5,29,18,22)
#define PAL_PURPLE_E     CVBS_CMD_PAL_PIXEL(29,13,22,18,29)
#define PAL_GREEN_O	 CVBS_CMD_PAL_PIXEL( 7, 6, 1,18, 7)
#define PAL_GREEN_E	 CVBS_CMD_PAL_PIXEL( 1,12, 7,18, 1)
#define PAL_BLUE_O	 CVBS_CMD_PAL_PIXEL( 9,18,10, 3,10)
#define PAL_BLUE_E	 CVBS_CMD_PAL_PIXEL( 9,18,10, 3,10)
#define PAL_YELLOW_O     CVBS_CMD_PAL_PIXEL( 5,18,15, 3,15)
#define PAL_YELLOW_E     CVBS_CMD_PAL_PIXEL( 5,18,15, 3,15)
#define PAL_ORANGE_O     CVBS_CMD_PAL_PIXEL(22,13,19,18,22)
#define PAL_ORANGE_E     CVBS_CMD_PAL_PIXEL(19, 5,22,18,19)
#define PAL_LORANGE_O    CVBS_CMD_PAL_PIXEL(21,13,27,18,21)
#define PAL_LORANGE_E    CVBS_CMD_PAL_PIXEL(27, 5,21,18,27)
#define PAL_PINK_O	 CVBS_CMD_PAL_PIXEL( 5,10,11,18, 5)
#define PAL_PINK_E	 CVBS_CMD_PAL_PIXEL(11, 8, 5,18,11)
#define PAL_LCYAN_O	 CVBS_CMD_PAL_PIXEL(15,10, 3,18,15)
#define PAL_LCYAN_E	 CVBS_CMD_PAL_PIXEL( 3, 8,15,18, 3)
#define PAL_LPURPLE_O    CVBS_CMD_PAL_PIXEL(21, 5,27,18,21)
#define PAL_LPURPLE_E    CVBS_CMD_PAL_PIXEL(27,13,21,18,27)
#define PAL_LGREEN_O     CVBS_CMD_PAL_PIXEL(23, 6,29,18,23)
#define PAL_LGREEN_E     CVBS_CMD_PAL_PIXEL(29,12,23,18,29)
#define PAL_LBLUE_O	 CVBS_CMD_PAL_PIXEL( 3,18, 5, 3, 5)
#define PAL_LBLUE_E	 CVBS_CMD_PAL_PIXEL( 3,18, 5, 3, 5)
#define PAL_LYELLOW_O    CVBS_CMD_PAL_PIXEL(19,18,31, 3,31)
#define PAL_LYELLOW_E    CVBS_CMD_PAL_PIXEL(19,18,31, 3,31)

#endif /* _CVBS_PAL_H_ */
 
