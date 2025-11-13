/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CVBS_NTSC_H_
#define _CVBS_NTSC_H_ 

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

#define CVBS_CMD_ID_DC_RUN cvbs_ntsc_offset_cvbs_cmd_dc_run
#define CVBS_CMD_ID_PIXEL cvbs_ntsc_offset_cvbs_cmd_pixel
#define CVBS_CMD_ID_BURST cvbs_ntsc_offset_cvbs_cmd_burst

#define CVBS_CMD_DC_RUN(L0,count) \
        (((count-2) << 10) | ((L0&0x1F) << 5)| CVBS_CMD_ID_DC_RUN)
#define CVBS_CMD_PIXEL(L0,delay0,L1,delay1,L2) \
        (((L2&0x1F) << 27) |  (((delay1-3)&0x3F) << 21) | ((L1&0x1F) << 16) | (((delay0-3)&0x3F) << 10) | ((L0&0x1F) << 5) | CVBS_CMD_ID_PIXEL)
#define CVBS_CMD_BURST(L0,L1,DC,delay,count) \
        (((DC&0x1F) << 26) | ((L1&0x1F) << 21) | ((L0&0x1F) << 16) | (((delay-1)&0x1F) << 11) | (((count-1)&0x3F) << 5) | CVBS_CMD_ID_BURST)

#define CVBS_CMD_BURST_DELAY(cmd,delay) \
        ((cmd & ~(0x3F<<10)) | ((delay-1)&0x3F)<<10)

//Front porch split into two, to allow for change to vertical blanking after first part, if required.
#define NTSC_FRONTPORCH_1 CVBS_CMD_DC_RUN( 9,12)
#define NTSC_FRONTPORCH_2 CVBS_CMD_DC_RUN( 9,4)

#define NTSC_FRONTPORCH  CVBS_CMD_DC_RUN( 9,16)
#define NTSC_HSYNC       CVBS_CMD_DC_RUN( 0,20)
#define NTSC_BREEZEWAY   CVBS_CMD_DC_RUN( 9, 2)
#define NTSC_BACKPORCH   CVBS_CMD_DC_RUN( 9, 2)
#define NTSC_LONG_SYNC_L  CVBS_CMD_DC_RUN( 0,110)
#define NTSC_LONG_SYNC_H  CVBS_CMD_DC_RUN( 9, 20)
#define NTSC_SHORT_SYNC_L CVBS_CMD_DC_RUN( 0, 20)
#define NTSC_SHORT_SYNC_H CVBS_CMD_DC_RUN( 9,110)
#define NTSC_BLANKING     CVBS_CMD_DC_RUN( 9,200)
//Two NTSC burst methodes available CMD_BURST or PIXEL_RUN
//CMD_BURST is more experimental since it runs carrier cycles instead of dot clock cycles
//17 carrier cycles => 19,5 dot clock cycles, syncs up as 20 dot clock cycles
#define NTSC_COLBURST_O     CVBS_CMD_BURST(13, 5, 9, 5,17)
#define NTSC_COLBURST_E     CVBS_CMD_BURST( 5,13, 9, 5,17)

//VIC-20 NTSC dot clock/colour carrier ratio is 4/3.5
//We solve this by using 8 precomputed carrier to pixel
//mappings per color bearing pixel.
//Run versions 0..7 0..7 etc

#define NTSC_BLACK     CVBS_CMD_PIXEL( 9,44, 9,3, 9)
#define NTSC_WHITE     CVBS_CMD_PIXEL(28,44,28,3,28)

#define NTSC_RED_0     CVBS_CMD_PIXEL(21,28, 7,44,21)
#define NTSC_RED_4     CVBS_CMD_PIXEL( 7,28,21,44, 7)
#define NTSC_RED_1     CVBS_CMD_PIXEL(21,39, 7, 3, 7)
#define NTSC_RED_5     CVBS_CMD_PIXEL( 7,39,21, 3,21)
#define NTSC_RED_2     CVBS_CMD_PIXEL( 7, 6,21,44, 7)
#define NTSC_RED_6     CVBS_CMD_PIXEL(21, 6, 7,44,21)
#define NTSC_RED_3     CVBS_CMD_PIXEL( 7,17,21,44, 7)
#define NTSC_RED_7     CVBS_CMD_PIXEL(21,17, 7,44,21)
#define NTSC_CYAN_0    CVBS_CMD_PIXEL(16,28,30,44,16)
#define NTSC_CYAN_4    CVBS_CMD_PIXEL(30,28,16,44,30)
#define NTSC_CYAN_1    CVBS_CMD_PIXEL(16,39,30, 3,30)
#define NTSC_CYAN_5    CVBS_CMD_PIXEL(30,39,16, 3,16)
#define NTSC_CYAN_2    CVBS_CMD_PIXEL(39, 6,16,44,30)
#define NTSC_CYAN_6    CVBS_CMD_PIXEL(16, 6,30,44,16)
#define NTSC_CYAN_3    CVBS_CMD_PIXEL(30,17,16,44,30)
#define NTSC_CYAN_7    CVBS_CMD_PIXEL(16,17,30,44,16)
#define NTSC_PURPLE_0  CVBS_CMD_PIXEL(23,32, 9,44,23)
#define NTSC_PURPLE_4  CVBS_CMD_PIXEL( 9,32,23,44, 9)
#define NTSC_PURPLE_1  CVBS_CMD_PIXEL(23,43, 9, 3, 9)
#define NTSC_PURPLE_5  CVBS_CMD_PIXEL( 9,43,23, 3,23)
#define NTSC_PURPLE_2  CVBS_CMD_PIXEL( 9,10,23,44, 9)
#define NTSC_PURPLE_6  CVBS_CMD_PIXEL(23,10, 9,44,23)
#define NTSC_PURPLE_3  CVBS_CMD_PIXEL( 9,21,23,44, 9)
#define NTSC_PURPLE_7  CVBS_CMD_PIXEL(28,21, 9,44,23)
#define NTSC_GREEN_0   CVBS_CMD_PIXEL(14,32,28,44,14)
#define NTSC_GREEN_4   CVBS_CMD_PIXEL(28,32,14,44,28)
#define NTSC_GREEN_1   CVBS_CMD_PIXEL(14,43,28, 3,28)
#define NTSC_GREEN_5   CVBS_CMD_PIXEL(28,43,14, 3,14)
#define NTSC_GREEN_2   CVBS_CMD_PIXEL(28,10,14,44,28)
#define NTSC_GREEN_6   CVBS_CMD_PIXEL(14,10,28,44,14)
#define NTSC_GREEN_3   CVBS_CMD_PIXEL(28,21,14,44,28)
#define NTSC_GREEN_7   CVBS_CMD_PIXEL(14,21,28,44,14)
#define NTSC_BLUE_0    CVBS_CMD_PIXEL( 6,16,20,44, 6)
#define NTSC_BLUE_4    CVBS_CMD_PIXEL(20,16, 6,44,20)
#define NTSC_BLUE_1    CVBS_CMD_PIXEL( 6,27,20,44, 6)
#define NTSC_BLUE_5    CVBS_CMD_PIXEL(20,27, 6,44,20)
#define NTSC_BLUE_2    CVBS_CMD_PIXEL( 6,38,20, 3,20)
#define NTSC_BLUE_6    CVBS_CMD_PIXEL(20,38, 6, 3, 6)
#define NTSC_BLUE_3    CVBS_CMD_PIXEL(20, 5, 6,44,20)
#define NTSC_BLUE_7    CVBS_CMD_PIXEL( 6, 5,20,44, 6)
#define NTSC_YELLOW_0  CVBS_CMD_PIXEL(31,16,17,44,31)
#define NTSC_YELLOW_4  CVBS_CMD_PIXEL(17,16,31,44,17)
#define NTSC_YELLOW_1  CVBS_CMD_PIXEL(31,27,17,44,31)
#define NTSC_YELLOW_5  CVBS_CMD_PIXEL(17,27,31,44,17)
#define NTSC_YELLOW_2  CVBS_CMD_PIXEL(31,38,17, 3,17)
#define NTSC_YELLOW_6  CVBS_CMD_PIXEL(17,38,31, 3,31)
#define NTSC_YELLOW_3  CVBS_CMD_PIXEL(17, 5,31,44,17)
#define NTSC_YELLOW_7  CVBS_CMD_PIXEL(31, 5,17,44,31)
#define NTSC_ORANGE_0  CVBS_CMD_PIXEL(24,18,10,44,24)
#define NTSC_ORANGE_4  CVBS_CMD_PIXEL(10,18,24,44,10)
#define NTSC_ORANGE_1  CVBS_CMD_PIXEL(24,29,10,44,24)
#define NTSC_ORANGE_5  CVBS_CMD_PIXEL(10,29,24,44,10)
#define NTSC_ORANGE_2  CVBS_CMD_PIXEL(24,40,10, 3,10)
#define NTSC_ORANGE_6  CVBS_CMD_PIXEL(10,40,24, 3,24)
#define NTSC_ORANGE_3  CVBS_CMD_PIXEL(10, 7,24,44,10)
#define NTSC_ORANGE_7  CVBS_CMD_PIXEL(24, 7,10,44,24)
#define NTSC_LORANGE_0 CVBS_CMD_PIXEL(27,18,19,44,27)
#define NTSC_LORANGE_4 CVBS_CMD_PIXEL(19,18,27,44,19)
#define NTSC_LORANGE_1 CVBS_CMD_PIXEL(27,29,19,44,27)
#define NTSC_LORANGE_5 CVBS_CMD_PIXEL(19,29,27,44,19)
#define NTSC_LORANGE_2 CVBS_CMD_PIXEL(27,40,19, 3,19)
#define NTSC_LORANGE_6 CVBS_CMD_PIXEL(19,40,27, 3,27)
#define NTSC_LORANGE_3 CVBS_CMD_PIXEL(19, 7,27,44,19)
#define NTSC_LORANGE_7 CVBS_CMD_PIXEL(27, 7,19,44,27)
#define NTSC_PINK_0    CVBS_CMD_PIXEL(25,28,17,44,25)
#define NTSC_PINK_4    CVBS_CMD_PIXEL(17,28,25,44,17)
#define NTSC_PINK_1    CVBS_CMD_PIXEL(25,39,17, 3,17)
#define NTSC_PINK_5    CVBS_CMD_PIXEL(17,39,25, 3,25)
#define NTSC_PINK_2    CVBS_CMD_PIXEL(17, 6,25,44,17)
#define NTSC_PINK_6    CVBS_CMD_PIXEL(25, 6,17,44,25)
#define NTSC_PINK_3    CVBS_CMD_PIXEL(17,17,25,44,17)
#define NTSC_PINK_7    CVBS_CMD_PIXEL(25,17,17,44,25)
#define NTSC_LCYAN_0   CVBS_CMD_PIXEL(22,28,30,44,22)
#define NTSC_LCYAN_4   CVBS_CMD_PIXEL(30,28,22,44,30)
#define NTSC_LCYAN_1   CVBS_CMD_PIXEL(22,39,30, 3,30)
#define NTSC_LCYAN_5   CVBS_CMD_PIXEL(30,39,22, 3,22)
#define NTSC_LCYAN_2   CVBS_CMD_PIXEL(30, 6,22,44,30)
#define NTSC_LCYAN_6   CVBS_CMD_PIXEL(22, 6,30,44,22)
#define NTSC_LCYAN_3   CVBS_CMD_PIXEL(30,17,22,44,30)
#define NTSC_LCYAN_7   CVBS_CMD_PIXEL(22,17,30,44,22)
#define NTSC_LPURPLE_0 CVBS_CMD_PIXEL(27,32,19,44,27)
#define NTSC_LPURPLE_4 CVBS_CMD_PIXEL(19,32,27,44,19)
#define NTSC_LPURPLE_1 CVBS_CMD_PIXEL(27,43,19, 3,19)
#define NTSC_LPURPLE_5 CVBS_CMD_PIXEL(19,43,27, 3,27)
#define NTSC_LPURPLE_2 CVBS_CMD_PIXEL(19,10,27,44,19)
#define NTSC_LPURPLE_6 CVBS_CMD_PIXEL(27,10,19,44,27)
#define NTSC_LPURPLE_3 CVBS_CMD_PIXEL(19,21,27,44,19)
#define NTSC_LPURPLE_7 CVBS_CMD_PIXEL(27,21,19,44,27)
#define NTSC_LGREEN_0  CVBS_CMD_PIXEL(21,32,29,44,21)
#define NTSC_LGREEN_4  CVBS_CMD_PIXEL(29,32,21,44,29)
#define NTSC_LGREEN_1  CVBS_CMD_PIXEL(21,43,29, 3,29)
#define NTSC_LGREEN_5  CVBS_CMD_PIXEL(29,43,21, 3,21)
#define NTSC_LGREEN_2  CVBS_CMD_PIXEL(29,10,21,44,29)
#define NTSC_LGREEN_6  CVBS_CMD_PIXEL(21,10,29,44,21)
#define NTSC_LGREEN_3  CVBS_CMD_PIXEL(29,21,21,44,29)
#define NTSC_LGREEN_7  CVBS_CMD_PIXEL(21,21,29,44,21)
#define NTSC_LBLUE_0   CVBS_CMD_PIXEL(16,16,24,44,16)
#define NTSC_LBLUE_4   CVBS_CMD_PIXEL(24,16,16,44,24)
#define NTSC_LBLUE_1   CVBS_CMD_PIXEL(16,27,24,44,16)
#define NTSC_LBLUE_5   CVBS_CMD_PIXEL(24,27,16,44,24)
#define NTSC_LBLUE_2   CVBS_CMD_PIXEL(16,38,24, 3,24)
#define NTSC_LBLUE_6   CVBS_CMD_PIXEL(24,38,16, 3,16)
#define NTSC_LBLUE_3   CVBS_CMD_PIXEL(24, 5,16,44,24)
#define NTSC_LBLUE_7   CVBS_CMD_PIXEL(15, 5,24,44,16)
#define NTSC_LYELLOW_0 CVBS_CMD_PIXEL(31,16,23,44,31)
#define NTSC_LYELLOW_4 CVBS_CMD_PIXEL(23,16,31,44,23)
#define NTSC_LYELLOW_1 CVBS_CMD_PIXEL(31,27,23,44,31)
#define NTSC_LYELLOW_5 CVBS_CMD_PIXEL(23,27,31,44,23)
#define NTSC_LYELLOW_2 CVBS_CMD_PIXEL(31,38,23, 3,23)
#define NTSC_LYELLOW_6 CVBS_CMD_PIXEL(23,38,31, 3,31)
#define NTSC_LYELLOW_3 CVBS_CMD_PIXEL(23, 5,31,44,23)
#define NTSC_LYELLOW_7 CVBS_CMD_PIXEL(31, 5,23,44,31)

#endif /* _CVBS_NTSC_H_ */
 
