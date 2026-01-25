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
//Commands for the PIO program (each word 32bits):
//    dc_run: Repeate L0 for count periods
//            [29:10]=repeat, [9:5]=L0, [4:0]=dc_run_id
//
//     pixel: CVBS pixel data payload for pixel_run commands
//            [31:25]=delay1, [24:18]=L1, [17:12]=delay0, [11:5] L0, [4:0]=pixel_id
//     burst: Colour burst special. Alternative to doing pixel_run of burst pixels
//            [31:25]=DC, [24:18]=L1, [17:11]=L0, [10:5]=pre_delay(phase), [4:0]=burst_id

#define CVBS_CMD_ID_SYNC cvbs_ntsc_offset_cvbs_cmd_sync
#define CVBS_CMD_ID_BLANK cvbs_ntsc_offset_cvbs_cmd_blank
#define CVBS_CMD_ID_DC_RUN cvbs_ntsc_offset_cvbs_cmd_dc_run
#define CVBS_CMD_ID_PIXEL cvbs_ntsc_offset_cvbs_cmd_pixel
#define CVBS_CMD_ID_BURST cvbs_ntsc_offset_cvbs_cmd_burst

#define CVBS_CMD_DC_RUN(L0,count) \
        (((count-2) << 12) | ((L0&0x7F) << 5)| CVBS_CMD_ID_DC_RUN)
#define CVBS_CMD_PIXEL(L0,delay0,L1,delay1,L2_unused) \
        ((((delay1-4)&0x3F) << 25) | ((L1&0x7F) << 18) | (((delay0-3)&0x3F) << 12) | ((L0&0x7F) << 5) | CVBS_CMD_ID_PIXEL)
#define CVBS_CMD_BURST(L0,L1,DC,delay,count_unused) \
        (((DC&0x7F) << 25) | ((L1&0x7F) << 18) | ((L0&0x7F) << 11) | (((delay-1)&0x3F) << 5) | CVBS_CMD_ID_BURST)

//Front porch split into two, to allow for change to vertical blanking after first part, if required.
#define NTSC_FRONTPORCH_1 CVBS_CMD_DC_RUN( 9<<2,12)
#define NTSC_FRONTPORCH_2 CVBS_CMD_DC_RUN( 9<<2,4)

#define NTSC_FRONTPORCH  CVBS_CMD_DC_RUN( 9<<2,16)
#define NTSC_HSYNC       CVBS_CMD_DC_RUN( 0<<2,20)
#define NTSC_BREEZEWAY   CVBS_CMD_DC_RUN( 9<<2, 2)
#define NTSC_BACKPORCH   CVBS_CMD_DC_RUN( 9<<2, 2)
#define NTSC_LONG_SYNC_L  CVBS_CMD_DC_RUN( 0<<2,110)
#define NTSC_LONG_SYNC_H  CVBS_CMD_DC_RUN( 9<<2, 20)
#define NTSC_SHORT_SYNC_L CVBS_CMD_DC_RUN( 0<<2, 20)
#define NTSC_SHORT_SYNC_H CVBS_CMD_DC_RUN( 9<<2,110)
#define NTSC_BLANKING     CVBS_CMD_DC_RUN( 9<<2,200)
//Two NTSC burst methodes available CMD_BURST or PIXEL_RUN
//CMD_BURST is more experimental since it runs carrier cycles instead of dot clock cycles
//17 carrier cycles => 19,5 dot clock cycles, syncs up as 20 dot clock cycles

//VIC-20 NTSC dot clock/colour carrier ratio is 4/3.5
//We solve this by using 8 precomputed carrier to pixel
//mappings per color bearing pixel.
//Run versions 0..7 0..7 etc

static const cvbs_palette_t palette_default_ntsc = {
        .version = 1,
        .burst = {  4,  9, +2, 0 },
        .colours = {
                 { 44,  9,  0, 0 },     //Black
                 { 44, 28,  0, 0 },     //White
                 { 21, 14, +4, 0 },     //Red
                 { 21, 23, -4, 0 },     //Cyan
                 { 32, 16, +4, 0 },     //Purple
                 { 32, 21, -4, 0 },     //Green
                 {  4, 13, -4, 0 },     //Blue
                 {  4, 24, +4, 0 },     //Yellow
                 { 15, 17, +4, 0 },     //Orange
                 { 23, 23, +2, 0 },     //LOrange
                 { 28, 21, +2, 0 },     //Pink
                 { 28, 26, -2, 0 },     //LCyan
                 { 39, 23, +2, 0 },     //LPurple
                 { 39, 25, -2, 0 },     //LGreen
                 { 12, 20, -2, 0 },     //LBlue
                 { 12, 27, +2, 0 }      //LYellow
        }
};
#endif /* _CVBS_NTSC_H_ */
 
