/*
* Copyright (c) 2025 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
//#include "sys/ria.h"
#include "vic/cvbs.h"
#include "sys/mem.h"
#include "cvbs.pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>

//TODO Currently delay is static and need to be balanced across commands to keep total timing. Should be fixed
//Static delay is 8 cycles. For PAL 7 cycles delay is thus nominal half period delay (15)
//We'll use 4 periodes as ca 1uS time unit reference
//BUG Rev 1.0 boards' DAC have bits reversed
const uint8_t rev5bit[32] = { 
   //0-7
    0,16, 8,24, 4,20,12,28,
   //8-15
    2,18,10,26, 6,22,14,30,
   //16-23
    1,17, 9,25, 5,21,13,29,
   //24-31
    3,19,11,27, 7,23,15,31 
};

// DAC driving logic is using square wave
//L0 and L1 levels are asserted for half periods
//DC level is ignored (left over from oversampling attempt)
//Set L0 and L1 to same level to get DC
//Delay is used to shift the phase a number of cycles. 
//CVBS_DELAY_CONST assures the period is constant. Delay must be less or equal to this value
//Count is the number of iterations of the signal to generate. NB minimum count is 2
#define CVBS_DELAY_CONST_POST (15-3)
#define CVBS_CMD(L0,L1,DC,delay,count) \
         ((((CVBS_DELAY_CONST_POST-delay)&0xF)<<23) |  ((L1&0x1F)<<18) | (((count-1)&0x1FF)<<9) |((L0&0x1F)<<4) | ((delay&0x0F)))
#define CVBS_REP(cmd,count) ((cmd & ~(0x1FF<<9)) | ((count-1) & 0x1FF)<<9)
//Experimental timings (eyeballing + experimenting)
//Levels bit-reverse hard-coded
#define PAL_HSYNC       CVBS_CMD( 0, 0, 0, 0,20)
#define PAL_BLANK       CVBS_CMD(18,18,18, 0,50)
#define PAL_FRONTPORCH  CVBS_CMD(18,18,18, 0, 8)
#define PAL_BREEZEWAY   CVBS_CMD(18,18,18, 0,4)
#define PAL_BACKPORCH   CVBS_CMD(18,18,18, 0,4)
/* Depricated
#define PAL_COLORBURST_E CVBS_CMD(12, 6,18, 0,11)
#define PAL_COLORBURST_O CVBS_CMD( 6,12,18, 7,11)
#define PAL_BAR_BLK     CVBS_CMD(18,18,18, 0,46)
#define PAL_BAR_WHT     CVBS_CMD(23,23,23, 0,46)
#define PAL_BAR_RED_O   CVBS_CMD(21,18,30, 5,46)
#define PAL_BAR_RED_E   CVBS_CMD(18,21,30, 4,46)
#define PAL_BAR_GREEN_E CVBS_CMD( 1, 7,13, 1,46)
#define PAL_BAR_GREEN_O CVBS_CMD( 1, 7,13, 2,46)
#define PAL_BAR_BLUE_E  CVBS_CMD(18,25,14,10,46)
#define PAL_BAR_BLUE_O  CVBS_CMD(18,25,14, 9,46)
*/
#define PAL_LONG_SYNC_L  CVBS_CMD( 0, 0, 0, 0,133)
#define PAL_LONG_SYNC_H  CVBS_CMD(18,18,18, 0,  9)
#define PAL_SHORT_SYNC_L CVBS_CMD( 0, 0, 0, 0,  9)
#define PAL_SHORT_SYNC_H CVBS_CMD(18,18,18, 0,133)
#define PAL_BLANKING     CVBS_CMD(18,18,18, 0,234)

//"Tobias" colours - approximated
#define PAL_BURST_O	   CVBS_CMD(6,12,9,11,14)
#define PAL_BURST_E	   CVBS_CMD(12,6,9,4,14)

#define PAL_BLACK	      CVBS_CMD(18,18,9,0,0)
#define PAL_WHITE	      CVBS_CMD(23,23,29,0,0)
#define PAL_RED_O	      CVBS_CMD(5,10,15,9,0)
#define PAL_RED_E	      CVBS_CMD(10,5,15,6,0)
#define PAL_CYAN_O	   CVBS_CMD(9,15,24,9,0)
#define PAL_CYAN_E	   CVBS_CMD(15,9,24,7,0)
#define PAL_PURPLE_O	   CVBS_CMD(29,22,18,5,0)
#define PAL_PURPLE_E	   CVBS_CMD(22,29,18,10,0)
#define PAL_GREEN_O	   CVBS_CMD(1,7,22,5,0)
#define PAL_GREEN_E	   CVBS_CMD(7,1,22,10,0)
#define PAL_BLUE_O	   CVBS_CMD(9,10,14,0,0)
#define PAL_BLUE_E	   CVBS_CMD(9,10,14,0,0)
#define PAL_YELLOW_O	   CVBS_CMD(5,15,25,0,0)
#define PAL_YELLOW_E	   CVBS_CMD(5,15,25,0,0)
#define PAL_ORANGE_O	   CVBS_CMD(19,22,19,11,0)
#define PAL_ORANGE_E	   CVBS_CMD(22,19,19,4,0)
#define PAL_LORANGE_O	CVBS_CMD(27,21,24,11,0)
#define PAL_LORANGE_E	CVBS_CMD(21,27,24,4,0)
#define PAL_PINK_O	   CVBS_CMD(11,5,23,9,0)
#define PAL_PINK_E	   CVBS_CMD(5,11,23,6,0)
#define PAL_LCYAN_O	   CVBS_CMD(3,15,27,9,0)
#define PAL_LCYAN_E	   CVBS_CMD(15,3,27,7,0)
#define PAL_LPURPLE_O	CVBS_CMD(27,21,24,5,0)
#define PAL_LPURPLE_E	CVBS_CMD(21,27,24,10,0)
#define PAL_LGREEN_O	   CVBS_CMD(29,23,26,5,0)
#define PAL_LGREEN_E	   CVBS_CMD(23,29,26,10,0)
#define PAL_LBLUE_O	   CVBS_CMD(3,5,22,0,0)
#define PAL_LBLUE_E	   CVBS_CMD(3,5,22,0,0)
#define PAL_LYELLOW_O	CVBS_CMD(19,31,28,0,0)
#define PAL_LYELLOW_E	CVBS_CMD(19,31,28,0,0)

uint32_t test_vsync_p[] = {
   //Lines 310-312
   PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H, PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H,
   PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H, PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H,
   PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H, PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H,

   //Lines 1-5
   PAL_LONG_SYNC_L, PAL_LONG_SYNC_H, PAL_LONG_SYNC_L, PAL_LONG_SYNC_H,
   PAL_LONG_SYNC_L, PAL_LONG_SYNC_H, PAL_LONG_SYNC_L, PAL_LONG_SYNC_H,
   PAL_LONG_SYNC_L, PAL_LONG_SYNC_H, PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H,
   PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H, PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H,
   PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H, PAL_SHORT_SYNC_L, PAL_SHORT_SYNC_H,
};

uint32_t test_scanline_odd[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_BURST_O,
   PAL_BACKPORCH,
   CVBS_REP(PAL_BLACK, 12),
   CVBS_REP(PAL_WHITE, 12),
   CVBS_REP(PAL_RED_O, 15),
   CVBS_REP(PAL_CYAN_O, 15),
   CVBS_REP(PAL_PURPLE_O, 15),
   CVBS_REP(PAL_GREEN_O, 15),
   CVBS_REP(PAL_BLUE_O, 15),
   CVBS_REP(PAL_YELLOW_O, 15),
   CVBS_REP(PAL_ORANGE_O, 15),
   CVBS_REP(PAL_LORANGE_O, 15),
   CVBS_REP(PAL_PINK_O, 15),
   CVBS_REP(PAL_LCYAN_O, 15),
   CVBS_REP(PAL_LPURPLE_O, 15),
   CVBS_REP(PAL_LGREEN_O, 15),
   CVBS_REP(PAL_LBLUE_O, 15),
   CVBS_REP(PAL_LYELLOW_O, 15),
};

uint32_t test_scanline_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_BURST_E,
   PAL_BACKPORCH,
   CVBS_REP(PAL_BLACK, 12),
   CVBS_REP(PAL_WHITE, 12),
   CVBS_REP(PAL_RED_E, 15),
   CVBS_REP(PAL_CYAN_E, 15),
   CVBS_REP(PAL_PURPLE_E, 15),
   CVBS_REP(PAL_GREEN_E, 15),
   CVBS_REP(PAL_BLUE_E, 15),
   CVBS_REP(PAL_YELLOW_E, 15),
   CVBS_REP(PAL_ORANGE_E, 15),
   CVBS_REP(PAL_LORANGE_E, 15),
   CVBS_REP(PAL_PINK_E, 15),
   CVBS_REP(PAL_LCYAN_E, 15),
   CVBS_REP(PAL_LPURPLE_E, 15),
   CVBS_REP(PAL_LGREEN_E, 15),
   CVBS_REP(PAL_LBLUE_E, 15),
   CVBS_REP(PAL_LYELLOW_E, 15),
};

uint32_t test_blanking_line_odd[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_BURST_O,
   PAL_BACKPORCH,
   PAL_BLANKING,
};
uint32_t test_blanking_line_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_BURST_E,
   PAL_BACKPORCH,
   PAL_BLANKING,
};


void cvbs_init(void){ 
   pio_set_gpio_base (CVBS_PIO, CVBS_PIN_OFFS);
   for(uint32_t i = 0; i < 5; i++){
      pio_gpio_init(CVBS_PIO, CVBS_PIN_BASE+i);
      gpio_set_drive_strength(CVBS_PIN_BASE+i, GPIO_DRIVE_STRENGTH_2MA);
      gpio_set_slew_rate(CVBS_PIN_BASE+i, GPIO_SLEW_RATE_SLOW);
   }
   pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM, CVBS_PIN_BASE, 5, true);
   uint offset = pio_add_program(CVBS_PIO, &cvbs_program);
   pio_sm_config config = cvbs_program_get_default_config(offset);
   sm_config_set_out_pins(&config, CVBS_PIN_BASE, 5);             
   sm_config_set_out_shift(&config, true, true, 27); 
   sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
   pio_sm_init(CVBS_PIO, CVBS_SM, offset, &config);
   //pio_sm_put(CVBS_PIO, CVBS_SM, 0x84210FFF);    
   pio_sm_set_enabled(CVBS_PIO, CVBS_SM, true);   
   printf("CVBS init done\n");
}

void cvbs_task(void){
   static uint32_t i = 0;
   static uint32_t lines = 0;
   while(!pio_sm_is_tx_fifo_full(CVBS_PIO,CVBS_SM)){
      //pio_sm_put(CVBS_PIO, CVBS_SM, CVBS_CMD(rev5bit[i],rev5bit[i],rev5bit[i],rev5bit[i],6,40));
      if(lines < 285){  
         if(lines & 1u){
            pio_sm_put(CVBS_PIO, CVBS_SM, test_scanline_odd[i++]);
         }else{
            pio_sm_put(CVBS_PIO, CVBS_SM, test_scanline_even[i++]);
         }
         if(i >= count_of(test_scanline_odd)){  //Assuming same length
            i = 0;
            lines++;
         }
      }else if(lines < 293){ 
         //8 lines block
         pio_sm_put(CVBS_PIO, CVBS_SM, test_vsync_p[i++]);
         if(i >= count_of(test_vsync_p)){
            i = 0;
            lines+=8;
         }
      }else if(lines < 312){
         if(lines & 1u){
            pio_sm_put(CVBS_PIO, CVBS_SM, test_blanking_line_odd[i++]);
         }else{
            pio_sm_put(CVBS_PIO, CVBS_SM, test_blanking_line_even[i++]);
         }
         if(i >= count_of(test_blanking_line_odd)){ //Assuming same length
            i = 0;
            lines++;
         }
      }else{
         i = 0;
         lines = 0;
      }
   }
}
