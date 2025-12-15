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
// Moving from 2x to 1x clock divider for PAL for stability. Using +1 on delay shifts instead. This can be removed
// to get NTSC level of fine tuning, but then the fixed delay values in the palette need to be doubled.
#define CVBS_CMD_PAL_PIXEL(L0,delay0,L1,delay1,L2) \
        (((L2&0x1F) << 27) |  (((delay1-2)&0x1F) << (21+1)) | ((L1&0x1F) << 16) | (((delay0-2)&0x1F) << (10+1)) | ((L0&0x1F) << 5) | CVBS_CMD_PAL_ID_PIXEL)
#define CVBS_CMD_PAL_BURST(L0,L1,DC,delay,count) \
        (((DC&0x1F) << 27) | ((L1&0x1F) << 22) | ((L0&0x1F) << 17) | (((delay-1)&0x1F) << 11) | (((count-1)&0x1F) << 5) | CVBS_CMD_PAL_ID_BURST)

#define CVBS_CMD_PAL_BURST_DELAY(cmd,delay) \
        ((cmd & ~(0x1F<<11)) | ((delay-1)&0x1F)<<11)

#define PAL_HSYNC        CVBS_CMD_PAL_DC_RUN( 0,20)
#define PAL_FRONTPORCH   CVBS_CMD_PAL_DC_RUN( 9, 7)
#define PAL_FRONTPORCH_1 CVBS_CMD_PAL_DC_RUN( 9, 5)        // First part is from last pixel of HC=70 to end of HC=0.
#define PAL_FRONTPORCH_2 CVBS_CMD_PAL_DC_RUN( 9, 2)        // Other two in HC=1 up to HC=1.5
#define PAL_BREEZEWAY    CVBS_CMD_PAL_DC_RUN( 9, 3)        // Actual breezeway this delay + burst command delay
#define PAL_BACKPORCH    CVBS_CMD_PAL_DC_RUN( 9, 4)

//Burst command is special as it is only synced to dotclock at the end, thus delay + 16 periods counts as 17 dotclks
#define PAL_COLBURST_O	 CVBS_CMD_PAL_BURST(12, 6, 9,28,16)
#define PAL_COLBURST_E	 CVBS_CMD_PAL_BURST( 6,12, 9,10,16)

// Vertical blanking and sync.
// TODO: From original cvbs.c code. Doesn't match VIC chip timings.
#define PAL_LONG_SYNC_L  CVBS_CMD_PAL_DC_RUN( 0,133)
#define PAL_LONG_SYNC_H  CVBS_CMD_PAL_DC_RUN( 9,  9)
#define PAL_SHORT_SYNC_L CVBS_CMD_PAL_DC_RUN( 0,  9)
#define PAL_SHORT_SYNC_H CVBS_CMD_PAL_DC_RUN( 9,133)

#define PAL_BLANKING     CVBS_CMD_PAL_DC_RUN( 9,233)

//"Tobias" colours - approximated
#define PAL_BLACK	 CVBS_CMD_PAL_PIXEL( 9,18, 9, 3, 9)
#define PAL_WHITE	 CVBS_CMD_PAL_PIXEL(29,18,29, 3,29)
#define PAL_RED_O	 CVBS_CMD_PAL_PIXEL(10,10,20,18,10)
#define PAL_RED_E	 CVBS_CMD_PAL_PIXEL(20, 8,10,18,20)
#define PAL_CYAN_O	 CVBS_CMD_PAL_PIXEL(30,10,18,18,30)
#define PAL_CYAN_E	 CVBS_CMD_PAL_PIXEL(18, 8,30,18,18)
#define PAL_PURPLE_O     CVBS_CMD_PAL_PIXEL(13, 5,23,18,13)
#define PAL_PURPLE_E     CVBS_CMD_PAL_PIXEL(23,13,13,18,23)
#define PAL_GREEN_O	 CVBS_CMD_PAL_PIXEL(28, 6,16,18,28)
#define PAL_GREEN_E	 CVBS_CMD_PAL_PIXEL(16,12,28,18,16)
#define PAL_BLUE_O	 CVBS_CMD_PAL_PIXEL(18,18,10, 3,10)
#define PAL_BLUE_E	 CVBS_CMD_PAL_PIXEL(18,18,10, 3,10)
#define PAL_YELLOW_O     CVBS_CMD_PAL_PIXEL(20,18,30, 3,30)
#define PAL_YELLOW_E     CVBS_CMD_PAL_PIXEL(20,18,30, 3,30)
#define PAL_ORANGE_O     CVBS_CMD_PAL_PIXEL(13,13,25,18,13)
#define PAL_ORANGE_E     CVBS_CMD_PAL_PIXEL(25, 5,13,18,25)
#define PAL_LORANGE_O    CVBS_CMD_PAL_PIXEL(21,13,27,18,21)
#define PAL_LORANGE_E    CVBS_CMD_PAL_PIXEL(27, 5,21,18,27)
#define PAL_PINK_O	 CVBS_CMD_PAL_PIXEL(20,10,26,18,20)
#define PAL_PINK_E	 CVBS_CMD_PAL_PIXEL(26, 8,20,18,26)
#define PAL_LCYAN_O	 CVBS_CMD_PAL_PIXEL(30,10,24,18,30)
#define PAL_LCYAN_E	 CVBS_CMD_PAL_PIXEL(24, 8,30,18,24)
#define PAL_LPURPLE_O    CVBS_CMD_PAL_PIXEL(21, 5,27,18,21)
#define PAL_LPURPLE_E    CVBS_CMD_PAL_PIXEL(27,13,21,18,27)
#define PAL_LGREEN_O     CVBS_CMD_PAL_PIXEL(29, 6,23,18,29)
#define PAL_LGREEN_E     CVBS_CMD_PAL_PIXEL(23,12,29,18,23)
#define PAL_LBLUE_O	 CVBS_CMD_PAL_PIXEL(24,18,20, 3,20)
#define PAL_LBLUE_E	 CVBS_CMD_PAL_PIXEL(24,18,20, 3,20)
#define PAL_LYELLOW_O    CVBS_CMD_PAL_PIXEL(25,18,31, 3,31)
#define PAL_LYELLOW_E    CVBS_CMD_PAL_PIXEL(25,18,31, 3,31)

//Truncated 2/3 pixels (to blank) 
#define PAL_TRUNC_BLACK	        CVBS_CMD_PAL_PIXEL( 9,18, 9, 6, 9)
#define PAL_TRUNC_WHITE	        CVBS_CMD_PAL_PIXEL(29,18,29, 6, 9)
#define PAL_TRUNC_RED_O	        CVBS_CMD_PAL_PIXEL(10,10, 5,14, 9)
#define PAL_TRUNC_RED_E	        CVBS_CMD_PAL_PIXEL(20, 8,10,16, 9)
#define PAL_TRUNC_CYAN_O	CVBS_CMD_PAL_PIXEL(30,10,18,14, 9)
#define PAL_TRUNC_CYAN_E	CVBS_CMD_PAL_PIXEL(18, 8,30,16, 9)
#define PAL_TRUNC_PURPLE_O      CVBS_CMD_PAL_PIXEL(13, 5,23,18, 9)
#define PAL_TRUNC_PURPLE_E      CVBS_CMD_PAL_PIXEL(23,13,13,11, 9)
#define PAL_TRUNC_GREEN_O       CVBS_CMD_PAL_PIXEL(28, 6,16,18, 9)
#define PAL_TRUNC_GREEN_E	CVBS_CMD_PAL_PIXEL(16,12,28,12, 9)
#define PAL_TRUNC_BLUE_O	CVBS_CMD_PAL_PIXEL(18,18,10, 6, 9)
#define PAL_TRUNC_BLUE_E	CVBS_CMD_PAL_PIXEL(18,18,10, 6, 9)
#define PAL_TRUNC_YELLOW_O      CVBS_CMD_PAL_PIXEL(20,18,30, 6, 9)
#define PAL_TRUNC_YELLOW_E      CVBS_CMD_PAL_PIXEL(20,18,30, 6, 9)
#define PAL_TRUNC_ORANGE_O      CVBS_CMD_PAL_PIXEL(13,13,25,11, 9)
#define PAL_TRUNC_ORANGE_E      CVBS_CMD_PAL_PIXEL(25, 5,13,18, 9)
#define PAL_TRUNC_LORANGE_O     CVBS_CMD_PAL_PIXEL(21,13,27,11, 9)
#define PAL_TRUNC_LORANGE_E     CVBS_CMD_PAL_PIXEL(27, 5,21,18, 9)
#define PAL_TRUNC_PINK_O	CVBS_CMD_PAL_PIXEL(20,10,26,14, 9)
#define PAL_TRUNC_PINK_E	CVBS_CMD_PAL_PIXEL(26, 8,20,16, 9)
#define PAL_TRUNC_LCYAN_O	CVBS_CMD_PAL_PIXEL(30,10,24,14, 9)
#define PAL_TRUNC_LCYAN_E	CVBS_CMD_PAL_PIXEL(24, 8,30,16, 9)
#define PAL_TRUNC_LPURPLE_O     CVBS_CMD_PAL_PIXEL(21, 5,27,18, 9)
#define PAL_TRUNC_LPURPLE_E     CVBS_CMD_PAL_PIXEL(27,13,21,11, 9)
#define PAL_TRUNC_LGREEN_O      CVBS_CMD_PAL_PIXEL(29, 6,23,18, 9)
#define PAL_TRUNC_LGREEN_E      CVBS_CMD_PAL_PIXEL(23,12,29,12, 9)
#define PAL_TRUNC_LBLUE_O	CVBS_CMD_PAL_PIXEL(24,18,20, 6, 9)
#define PAL_TRUNC_LBLUE_E	CVBS_CMD_PAL_PIXEL(24,18,20, 6, 9)
#define PAL_TRUNC_LYELLOW_O     CVBS_CMD_PAL_PIXEL(25,18,31, 6, 9)
#define PAL_TRUNC_LYELLOW_E     CVBS_CMD_PAL_PIXEL(25,18,31, 6, 9)

#endif /* _CVBS_PAL_H_ */
 
