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
#define CVBS_DELAY_CONST_POST (15-5)
#define CVBS_CMD(L0,L1,DC,delay,count) \
         ((((CVBS_DELAY_CONST_POST-delay)&0xF)<<23) |  ((L1&0x1F)<<18) | (((count-2)&0x1FF)<<9) |((L0&0x1F)<<4) | ((delay&0x0F)))

//Experimental timings (eyeballing + experimenting)
//Levels bit-reverse hard-coded
#define PAL_HSYNC       CVBS_CMD( 0, 0, 0, 0,21)
#define PAL_BLANK       CVBS_CMD(18,18,18, 0,53)
#define PAL_FRONTPORCH  CVBS_CMD(18,18,18, 0, 7)
#define PAL_BACKPORCH1   CVBS_CMD(18,18,18, 0,4)
#define PAL_BACKPORCH2   CVBS_CMD(18,18,18, 0,11)
#define PAL_COLORBURST_E CVBS_CMD(12, 6,18, 0,11)
#define PAL_COLORBURST_O CVBS_CMD( 6,12,18, 7,11)
#define PAL_BAR_BLK     CVBS_CMD(18,18,18, 0,46)
#define PAL_BAR_WHT     CVBS_CMD(23,23,23, 0,46)
#define PAL_BAR_COL1    CVBS_CMD(19,20,30, 3,46)
#define PAL_BAR_COL2    CVBS_CMD(19,30, 5, 6,46)
#define PAL_BAR_COL3    CVBS_CMD( 5,10,30, 1,46)
#define PAL_BAR_COL4    CVBS_CMD(19,30, 5, 2,46)
#define PAL_BAR_COL5    CVBS_CMD( 5,10,30, 3,46)
#define PAL_BAR_COL6    CVBS_CMD(19,30, 5, 4,46)
#define PAL_BAR_COL7    CVBS_CMD( 5,10,30, 5,46)
#define PAL_BAR_COL8    CVBS_CMD(19,30, 5, 6,46)
#define PAL_BAR_COL9    CVBS_CMD( 5,10,30, 0,46)
#define PAL_BAR_COL10   CVBS_CMD(19,30, 5, 1,46)
#define PAL_BAR_COL11   CVBS_CMD( 5,10,30, 2,46)
#define PAL_BAR_COL12   CVBS_CMD(19,30, 5, 3,46)
#define PAL_BAR_COL13   CVBS_CMD( 5,10,30, 4,46)
#define PAL_BAR_COL14   CVBS_CMD(19,30, 5, 5,46)
#define PAL_BAR_COL15   CVBS_CMD( 5,10,30, 6,46)
#define PAL_BAR_COL16   CVBS_CMD(19,30, 5, 6,46)
#define PAL_BAR_RED_O   CVBS_CMD(21,18,30, 5,46)
#define PAL_BAR_RED_E   CVBS_CMD(18,21,30, 4,46)
#define PAL_BAR_GREEN_E CVBS_CMD( 1, 7,13, 1,46)
#define PAL_BAR_GREEN_O CVBS_CMD( 1, 7,13, 2,46)
#define PAL_BAR_BLUE_E  CVBS_CMD(18,25,14,10,46)
#define PAL_BAR_BLUE_O  CVBS_CMD(18,25,14, 9,46)
#define PAL_LONG_SYNC_L  CVBS_CMD( 0, 0, 0, 0,133)
#define PAL_LONG_SYNC_H  CVBS_CMD(18,18,18, 0,  9)
#define PAL_SHORT_SYNC_L CVBS_CMD( 0, 0, 0, 0,  9)
#define PAL_SHORT_SYNC_H CVBS_CMD(18,18,18, 0,133)
#define PAL_BLANKING     CVBS_CMD(18,18,18, 0,230)

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
   PAL_BACKPORCH1,
   PAL_COLORBURST_O,
   PAL_BACKPORCH2,
   PAL_BAR_BLK,
   PAL_BAR_WHT,
   PAL_BAR_RED_O,
   PAL_BAR_GREEN_O,
   PAL_BAR_BLUE_O,
};

uint32_t test_scanline_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BACKPORCH1,
   PAL_COLORBURST_E,
   PAL_BACKPORCH2,
   PAL_BAR_BLK,
   PAL_BAR_WHT,
   PAL_BAR_RED_E,
   PAL_BAR_GREEN_E,
   PAL_BAR_BLUE_E,
};

uint32_t test_blanking_line_odd[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BACKPORCH1,
   PAL_COLORBURST_O,
   PAL_BACKPORCH2,
   PAL_BLANKING,
};
uint32_t test_blanking_line_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BACKPORCH1,
   PAL_COLORBURST_E,
   PAL_BACKPORCH2,
   PAL_BLANKING,
};


void cvbs_init(void){ 
   pio_set_gpio_base (CVBS_PIO, CVBS_PIN_BANK);
   for(uint32_t i = 0; i < 5; i++){
      pio_gpio_init(CVBS_PIO, CVBS_PIN_BASE+i);
      gpio_set_drive_strength(CVBS_PIN_BASE, GPIO_DRIVE_STRENGTH_2MA);
   }
   pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM, CVBS_PIN_BASE, 5, true);
   uint offset = pio_add_program(CVBS_PIO, &cvbs_program);
   pio_sm_config config = cvbs_program_get_default_config(offset);
   sm_config_set_out_pins(&config, CVBS_PIN_BASE, 5);             
   sm_config_set_out_shift(&config, true, true, 27); 
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
