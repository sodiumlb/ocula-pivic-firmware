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
//Commands for the PIO program (each word 25 bits):
//    dc_run: Repeate L0 for count periods
//            [24:10]=repeat, [9:5]=L0, [4:0]=dc_run_id
//
// pixel_run: Output count pixels (exact count pixeldata to follow)
//            [24: 9]=repeat, [4:0]=pixel_run_id 
//
//      data: CVBS pixel data payload for pixel_run commands
//            [24:20]=L2, [19:15]=delay1, [14:10]=L1, [9:5]=delay0, [4:0] L0
//     burst: Colour burst special. Alternative to doing pixel_run of burst pixels
//            [24:20]=L1, [19:15]=L0, [14:10]=pre_delay(phase), [9:5]=repeat(burst periods), [4:0]=burst_id

#define CVBS_CMD_ID_DC_RUN cvbs_ntsc_offset_cvbs_cmd_dc_run
#define CVBS_CMD_ID_PIXEL_RUN cvbs_ntsc_offset_cvbs_cmd_pixel_run
#define CVBS_CMD_ID_BURST cvbs_ntsc_offset_cvbs_cmd_burst

#define CVBS_CMD_DC_RUN(L0,count) \
        (((count-1) << 10) | ((L0&0x1F) << 5)| CVBS_CMD_ID_DC_RUN)
#define CVBS_CMD_PIXEL_RUN(count) \
        (((count-1) << 5) | CVBS_CMD_ID_PIXEL_RUN)
#define CVBS_CMD_DATA(L0,delay0,L1,delay1,L2) \
        (((L2&0x1F) << 20) |  (((delay1-3)&0x1F) << 15) | ((L1&0x1F) << 10) | (((delay0-3)&0x1F) << 5) | (L0&0x1F))
#define CVBS_CMD_BURST(L0,L1,delay,count) \
        (((L1&0x1F) << 20) | ((L0&0x1F) << 15) | (((delay-1)&0x1F) << 10) | ((count&0x1F) << 5) | CVBS_CMD_ID_BURST)

#define NTSC_FRONTPORCH  CVBS_CMD_DC_RUN(18,16)
#define NTSC_HSYNC       CVBS_CMD_DC_RUN( 0,20)
#define NTSC_BREEZEWAY   CVBS_CMD_DC_RUN(18, 6)
#define NTSC_BACKPORCH   CVBS_CMD_DC_RUN(18, 7)
#define NTSC_LONG_SYNC_L  CVBS_CMD_DC_RUN( 0,110)
#define NTSC_LONG_SYNC_H  CVBS_CMD_DC_RUN(18, 20)
#define NTSC_SHORT_SYNC_L CVBS_CMD_DC_RUN( 0, 20)
#define NTSC_SHORT_SYNC_H CVBS_CMD_DC_RUN(18,110)
#define NTSC_BLANKING     CVBS_CMD_DC_RUN(18,200)
//Two NTSC burst methodes available CMD_BURST or PIXEL_RUN
//CMD_BURST is more experimental since it runs carrier cycles instead of dot clock cycles
//9 carrier cycles => 10.3 dot clock cycles, syncs up as 11 dot clock cycles
#define NTSC_BURST_O     CVBS_CMD_BURST(6,12,1,9)
#define NTSC_BURST_E     CVBS_CMD_BURST(12,6,1,9)

//VIC-20 NTSC dot clock/colour carrier ratio is 4/3.5
//We solve this by using 8 precomputed carrier to pixel
//mappings per color bearing pixel.
//Run versions 0..7 0..7 etc

#define NTSC_BLACK CVBS_CMD_DATA(26,22,26,3,26)
#define NTSC_WHITE CVBS_CMD_DATA(23,22,23,3,23)

#define NTSC_RED_0 CVBS_CMD_DATA(22,16,25,3,25)
#define NTSC_RED_4 CVBS_CMD_DATA(25,16,22,3,22)
#define NTSC_RED_1 CVBS_CMD_DATA(22,22,25,3,25)
#define NTSC_RED_5 CVBS_CMD_DATA(25,22,22,3,22)
#define NTSC_RED_2 CVBS_CMD_DATA(25,5,22,22,25)
#define NTSC_RED_6 CVBS_CMD_DATA(22,5,25,22,22)
#define NTSC_RED_3 CVBS_CMD_DATA(25,11,22,22,25)
#define NTSC_RED_7 CVBS_CMD_DATA(22,11,25,22,22)
#define NTSC_CYAN_0 CVBS_CMD_DATA(7,16,13,3,13)
#define NTSC_CYAN_4 CVBS_CMD_DATA(13,16,7,3,7)
#define NTSC_CYAN_1 CVBS_CMD_DATA(7,22,13,3,13)
#define NTSC_CYAN_5 CVBS_CMD_DATA(13,22,7,3,7)
#define NTSC_CYAN_2 CVBS_CMD_DATA(13,5,7,22,13)
#define NTSC_CYAN_6 CVBS_CMD_DATA(7,5,13,22,7)
#define NTSC_CYAN_3 CVBS_CMD_DATA(13,11,7,22,13)
#define NTSC_CYAN_7 CVBS_CMD_DATA(7,11,13,22,7)
#define NTSC_PURPLE_0 CVBS_CMD_DATA(30,11,21,22,30)
#define NTSC_PURPLE_4 CVBS_CMD_DATA(21,11,30,22,21)
#define NTSC_PURPLE_1 CVBS_CMD_DATA(30,17,21,3,21)
#define NTSC_PURPLE_5 CVBS_CMD_DATA(21,17,30,3,30)
#define NTSC_PURPLE_2 CVBS_CMD_DATA(30,22,21,3,21)
#define NTSC_PURPLE_6 CVBS_CMD_DATA(21,22,30,3,30)
#define NTSC_PURPLE_3 CVBS_CMD_DATA(21,6,30,22,21)
#define NTSC_PURPLE_7 CVBS_CMD_DATA(30,6,21,22,30)
#define NTSC_GREEN_0 CVBS_CMD_DATA(19,11,25,22,19)
#define NTSC_GREEN_4 CVBS_CMD_DATA(25,11,19,22,25)
#define NTSC_GREEN_1 CVBS_CMD_DATA(19,17,25,3,25)
#define NTSC_GREEN_5 CVBS_CMD_DATA(25,17,19,3,19)
#define NTSC_GREEN_2 CVBS_CMD_DATA(19,22,25,3,25)
#define NTSC_GREEN_6 CVBS_CMD_DATA(25,22,19,3,19)
#define NTSC_GREEN_3 CVBS_CMD_DATA(25,6,19,22,25)
#define NTSC_GREEN_7 CVBS_CMD_DATA(19,6,25,22,19)
#define NTSC_BLUE_0 CVBS_CMD_DATA(6,4,9,22,6)
#define NTSC_BLUE_4 CVBS_CMD_DATA(9,4,6,22,9)
#define NTSC_BLUE_1 CVBS_CMD_DATA(6,9,9,22,6)
#define NTSC_BLUE_5 CVBS_CMD_DATA(9,9,6,22,9)
#define NTSC_BLUE_2 CVBS_CMD_DATA(6,14,9,22,6)
#define NTSC_BLUE_6 CVBS_CMD_DATA(9,14,6,22,9)
#define NTSC_BLUE_3 CVBS_CMD_DATA(6,20,9,3,9)
#define NTSC_BLUE_7 CVBS_CMD_DATA(9,20,6,3,6)
#define NTSC_YELLOW_0 CVBS_CMD_DATA(23,4,29,22,23)
#define NTSC_YELLOW_4 CVBS_CMD_DATA(29,4,23,22,29)
#define NTSC_YELLOW_1 CVBS_CMD_DATA(23,9,29,22,23)
#define NTSC_YELLOW_5 CVBS_CMD_DATA(29,9,23,22,29)
#define NTSC_YELLOW_2 CVBS_CMD_DATA(23,14,29,22,23)
#define NTSC_YELLOW_6 CVBS_CMD_DATA(29,14,23,22,29)
#define NTSC_YELLOW_3 CVBS_CMD_DATA(23,20,29,3,29)
#define NTSC_YELLOW_7 CVBS_CMD_DATA(29,20,23,3,23)
#define NTSC_ORANGE_0 CVBS_CMD_DATA(1,19,13,3,13)
#define NTSC_ORANGE_4 CVBS_CMD_DATA(13,19,1,3,1)
#define NTSC_ORANGE_1 CVBS_CMD_DATA(13,4,1,22,13)
#define NTSC_ORANGE_5 CVBS_CMD_DATA(1,4,13,22,1)
#define NTSC_ORANGE_2 CVBS_CMD_DATA(13,8,1,22,13)
#define NTSC_ORANGE_6 CVBS_CMD_DATA(1,8,13,22,1)
#define NTSC_ORANGE_3 CVBS_CMD_DATA(13,14,1,22,13)
#define NTSC_ORANGE_7 CVBS_CMD_DATA(1,14,13,22,1)
#define NTSC_LORANGE_0 CVBS_CMD_DATA(21,15,27,22,21)
#define NTSC_LORANGE_4 CVBS_CMD_DATA(27,15,21,22,27)
#define NTSC_LORANGE_1 CVBS_CMD_DATA(21,21,27,3,27)
#define NTSC_LORANGE_5 CVBS_CMD_DATA(27,21,21,3,21)
#define NTSC_LORANGE_2 CVBS_CMD_DATA(27,4,21,22,27)
#define NTSC_LORANGE_6 CVBS_CMD_DATA(21,4,27,22,21)
#define NTSC_LORANGE_3 CVBS_CMD_DATA(27,10,21,22,27)
#define NTSC_LORANGE_7 CVBS_CMD_DATA(21,10,27,22,21)
#define NTSC_PINK_0 CVBS_CMD_DATA(5,13,11,22,5)
#define NTSC_PINK_4 CVBS_CMD_DATA(11,13,5,22,11)
#define NTSC_PINK_1 CVBS_CMD_DATA(5,19,11,3,11)
#define NTSC_PINK_5 CVBS_CMD_DATA(11,19,5,3,5)
#define NTSC_PINK_2 CVBS_CMD_DATA(11,4,5,22,11)
#define NTSC_PINK_6 CVBS_CMD_DATA(5,4,11,22,5)
#define NTSC_PINK_3 CVBS_CMD_DATA(11,8,5,22,11)
#define NTSC_PINK_7 CVBS_CMD_DATA(5,8,11,22,5)
#define NTSC_LCYAN_0 CVBS_CMD_DATA(15,13,3,22,15)
#define NTSC_LCYAN_4 CVBS_CMD_DATA(3,13,15,22,3)
#define NTSC_LCYAN_1 CVBS_CMD_DATA(15,19,3,3,3)
#define NTSC_LCYAN_5 CVBS_CMD_DATA(3,19,15,3,15)
#define NTSC_LCYAN_2 CVBS_CMD_DATA(3,4,15,22,3)
#define NTSC_LCYAN_6 CVBS_CMD_DATA(15,4,3,22,15)
#define NTSC_LCYAN_3 CVBS_CMD_DATA(3,8,15,22,3)
#define NTSC_LCYAN_7 CVBS_CMD_DATA(15,8,3,22,15)
#define NTSC_LPURPLE_0 CVBS_CMD_DATA(21,7,27,22,21)
#define NTSC_LPURPLE_4 CVBS_CMD_DATA(27,7,21,22,27)
#define NTSC_LPURPLE_1 CVBS_CMD_DATA(21,13,27,22,21)
#define NTSC_LPURPLE_5 CVBS_CMD_DATA(27,13,21,22,27)
#define NTSC_LPURPLE_2 CVBS_CMD_DATA(21,18,27,3,27)
#define NTSC_LPURPLE_6 CVBS_CMD_DATA(27,18,21,3,21)
#define NTSC_LPURPLE_3 CVBS_CMD_DATA(27,4,21,22,27)
#define NTSC_LPURPLE_7 CVBS_CMD_DATA(21,4,27,22,21)
#define NTSC_LGREEN_0 CVBS_CMD_DATA(23,7,29,22,23)
#define NTSC_LGREEN_4 CVBS_CMD_DATA(29,7,23,22,29)
#define NTSC_LGREEN_1 CVBS_CMD_DATA(23,13,29,22,23)
#define NTSC_LGREEN_5 CVBS_CMD_DATA(29,13,23,22,29)
#define NTSC_LGREEN_2 CVBS_CMD_DATA(23,18,29,3,29)
#define NTSC_LGREEN_6 CVBS_CMD_DATA(29,18,23,3,23)
#define NTSC_LGREEN_3 CVBS_CMD_DATA(29,4,23,22,29)
#define NTSC_LGREEN_7 CVBS_CMD_DATA(23,4,29,22,23)
#define NTSC_LBLUE_0 CVBS_CMD_DATA(19,21,25,3,25)
#define NTSC_LBLUE_4 CVBS_CMD_DATA(25,21,19,3,19)
#define NTSC_LBLUE_1 CVBS_CMD_DATA(25,5,19,22,25)
#define NTSC_LBLUE_5 CVBS_CMD_DATA(19,5,25,22,19)
#define NTSC_LBLUE_2 CVBS_CMD_DATA(25,10,19,22,25)
#define NTSC_LBLUE_6 CVBS_CMD_DATA(19,10,25,22,19)
#define NTSC_LBLUE_3 CVBS_CMD_DATA(25,16,19,22,25)
#define NTSC_LBLUE_7 CVBS_CMD_DATA(19,16,25,22,19)
#define NTSC_LYELLOW_0 CVBS_CMD_DATA(19,21,31,3,31)
#define NTSC_LYELLOW_4 CVBS_CMD_DATA(31,21,19,3,19)
#define NTSC_LYELLOW_1 CVBS_CMD_DATA(31,5,19,22,31)
#define NTSC_LYELLOW_5 CVBS_CMD_DATA(19,5,31,22,19)
#define NTSC_LYELLOW_2 CVBS_CMD_DATA(31,10,19,22,31)
#define NTSC_LYELLOW_6 CVBS_CMD_DATA(19,10,31,22,19)
#define NTSC_LYELLOW_3 CVBS_CMD_DATA(31,16,19,22,31)
#define NTSC_LYELLOW_7 CVBS_CMD_DATA(19,16,31,22,19)
#define NTSC_BURST_0 CVBS_CMD_DATA(12,22,6,3,6)
#define NTSC_BURST_4 CVBS_CMD_DATA(6,22,12,3,12)
#define NTSC_BURST_1 CVBS_CMD_DATA(6,6,12,22,6)
#define NTSC_BURST_5 CVBS_CMD_DATA(12,6,6,22,12)
#define NTSC_BURST_2 CVBS_CMD_DATA(6,11,12,22,6)
#define NTSC_BURST_6 CVBS_CMD_DATA(12,11,6,22,12)
#define NTSC_BURST_3 CVBS_CMD_DATA(6,17,12,3,12)
#define NTSC_BURST_7 CVBS_CMD_DATA(12,17,6,3,6)

#endif /* _CVBS_NTSC_H_ */
 
