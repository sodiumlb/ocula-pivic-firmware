/*
 * Copyright (c) 2025 Sodiumlightbaby
 * Copyright (c) 2025 dreamseal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CVBS_PAL_H_
#define _CVBS_PAL_H_ 

// DAC driving logic is using square wave
//L0 and L1 levels are asserted for half periods
//DC level is ignored (left over from oversampling attempt)
//Set L0 and L1 to same level to get DC
//Delay is used to shift the phase a number of cycles. 
//CVBS_DELAY_CONST assures the period is constant. Delay must be less or equal to this value
//Count is the number of iterations of the signal to generate. NB minimum count is 2
#define CVBS_DELAY_CONST_POST_PAL (18-3)
#define CVBS_CMD(L0,L1,DC,delay,count) \
         ((((CVBS_DELAY_CONST_POST_PAL-delay)&0xF)<<23) |  ((L1&0x1F)<<18) | (((count-1)&0x1FF)<<9) |((L0&0x1F)<<4) | ((delay&0x0F)))
#define CVBS_REP(cmd,count) ((cmd & ~(0x1FF<<9)) | ((count-1) & 0x1FF)<<9)
//Experimental timings (eyeballing + experimenting)
//Levels bit-reverse hard-coded
// CVBS commands for optimised core1_loop that sends longer commands upfront when appropriate.
// Horiz Blanking - From: 70.5  To: 12  Len: 12.5 cycles
// Front Porch - From: 70.5  To: 1.5  Len: 2 cycles (Note: Partially split over VC lines)
// Horiz Sync - From: 1.5  To: 6.5  Len: 5 cycles
// Breezeway - From: 6.5  To: 7.5  Len: 1 cycle
// Colour Burst - From: 7.5  To: 11  Len: 3.5 cycles
// Back Porch - From: 11  To: 12  Len: 1 cycle
#define PAL_HSYNC        CVBS_CMD( 0, 0, 0, 0,20)
#define PAL_BLANK        CVBS_CMD(18,18,18, 0,50)
#define PAL_FRONTPORCH   CVBS_CMD(18,18,18, 0, 8)
#define PAL_FRONTPORCH_1 CVBS_CMD(18,18,18, 0, 2)    // First two in second half of HC=70
#define PAL_FRONTPORCH_2 CVBS_CMD(18,18,18, 0, 6)    // Other six in HC=0 up to HC=1.5
#define PAL_BREEZEWAY    CVBS_CMD(18,18,18, 0, 4)
#define PAL_BACKPORCH    CVBS_CMD(18,18,18, 0, 4)

#define PAL_COLBURST_O	 CVBS_CMD( 6,12, 9,14,14)
#define PAL_COLBURST_E	 CVBS_CMD(12, 6, 9, 5,14)

// Vertical blanking and sync.
// TODO: From original cvbs.c code. Doesn't match VIC chip timings.
#define PAL_LONG_SYNC_L  CVBS_CMD( 0, 0, 0, 0,133)
#define PAL_LONG_SYNC_H  CVBS_CMD(18,18,18, 0,  9)
#define PAL_SHORT_SYNC_L CVBS_CMD( 0, 0, 0, 0,  9)
#define PAL_SHORT_SYNC_H CVBS_CMD(18,18,18, 0,133)

#define PAL_BLANKING     CVBS_CMD(18,18,18, 0,234)

//"Tobias" colours - approximated
#define PAL_BLACK	   CVBS_CMD(18,18, 9, 0, 0)
#define PAL_WHITE	   CVBS_CMD(23,23,29, 0, 0)
#define PAL_RED_O	   CVBS_CMD( 5,10,15,10, 0)
#define PAL_RED_E	   CVBS_CMD(10, 5,15, 8, 0)
#define PAL_CYAN_O	   CVBS_CMD( 9,15,24,10, 0)
#define PAL_CYAN_E	   CVBS_CMD(15, 9,24, 8, 0)
#define PAL_PURPLE_O	   CVBS_CMD(29,22,18, 5, 0)
#define PAL_PURPLE_E	   CVBS_CMD(22,29,18,13, 0)
#define PAL_GREEN_O	   CVBS_CMD( 1, 7,22, 6, 0)
#define PAL_GREEN_E	   CVBS_CMD( 7, 1,22,12, 0)
#define PAL_BLUE_O	   CVBS_CMD( 9,10,14, 0, 0)
#define PAL_BLUE_E	   CVBS_CMD( 9,10,14, 0, 0)
#define PAL_YELLOW_O	   CVBS_CMD( 5,15,25, 0, 0)
#define PAL_YELLOW_E	   CVBS_CMD( 5,15,25, 0, 0)
#define PAL_ORANGE_O	   CVBS_CMD(19,22,19,13, 0)
#define PAL_ORANGE_E	   CVBS_CMD(22,19,19, 5, 0)
#define PAL_LORANGE_O	   CVBS_CMD(27,21,24,13, 0)
#define PAL_LORANGE_E	   CVBS_CMD(21,27,24, 5, 0)
#define PAL_PINK_O	   CVBS_CMD(11, 5,23,10, 0)
#define PAL_PINK_E	   CVBS_CMD( 5,11,23, 8, 0)
#define PAL_LCYAN_O	   CVBS_CMD( 3,15,27,10, 0)
#define PAL_LCYAN_E	   CVBS_CMD(15, 3,27, 8, 0)
#define PAL_LPURPLE_O	   CVBS_CMD(27,21,24, 5, 0)
#define PAL_LPURPLE_E	   CVBS_CMD(21,27,24,13, 0)
#define PAL_LGREEN_O	   CVBS_CMD(29,23,26, 6, 0)
#define PAL_LGREEN_E	   CVBS_CMD(23,29,26,12, 0)
#define PAL_LBLUE_O	   CVBS_CMD( 3, 5,22, 0, 0)
#define PAL_LBLUE_E	   CVBS_CMD( 3, 5,22, 0, 0)
#define PAL_LYELLOW_O	   CVBS_CMD(19,31,28, 0, 0)
#define PAL_LYELLOW_E	   CVBS_CMD(19,31,28, 0, 0)

#endif /* _CVBS_PAL_H_ */
 
