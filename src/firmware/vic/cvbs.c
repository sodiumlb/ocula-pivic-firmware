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

// DAC driving logic is using oversampling
//L0 and L1 levels are asserted for a single cycle
//DC level is asserted for half periods
//Delay is used to shift the phase a number of cycles. 
//CVBS_DELAY_CONST assures the period is constant. Delay must be less or equal to this value
//Repeat is the number of iterations of the signal to generate
#define CVBS_DELAY_CONST (15-9)
#define CVBS_CMD(L0,L1,DC,delay,repeat) \
         ((((CVBS_DELAY_CONST-delay)&0xF)<<28) | ((L1&0x1F)<<23) | ((L0&0x1F)<<18) | ((DC&0x1F)<<13) | ((repeat&0x3FF)<<3) | ((delay&0x07)))

//Experimental timings (eyeballing + exerimenting)
//Levels bit-reverse hard-coded
#define PAL_HSYNC       CVBS_CMD( 0, 0, 0, 0,18)
#define PAL_BLANK       CVBS_CMD(18,18,18, 0,48)
#define PAL_FRONTPORCH  CVBS_CMD(18,18,18, 0, 8)
#define PAL_BACKPORCH   CVBS_CMD(18,18,18, 0,11)
#define PAL_COLORBURST_E CVBS_CMD(14, 4,18, 0,14)
#define PAL_COLORBURST_O CVBS_CMD( 4,14,18, 6,14)
#define PAL_BAR_BLK     CVBS_CMD(26,26,26, 0,42)
#define PAL_BAR_WHT     CVBS_CMD(23,23,23, 0,42)
#define PAL_BAR_COL1    CVBS_CMD(19,20,30, 3,42)
#define PAL_BAR_COL2    CVBS_CMD(19,30, 5, 6,42)
#define PAL_BAR_COL3    CVBS_CMD( 5,10,30, 1,42)
#define PAL_BAR_COL4    CVBS_CMD(19,30, 5, 2,42)
#define PAL_BAR_COL5    CVBS_CMD( 5,10,30, 3,42)
#define PAL_BAR_COL6    CVBS_CMD(19,30, 5, 4,42)
#define PAL_BAR_COL7    CVBS_CMD( 5,10,30, 5,42)
#define PAL_BAR_COL8    CVBS_CMD(19,30, 5, 6,42)
#define PAL_BAR_COL9    CVBS_CMD( 5,10,30, 0,42)
#define PAL_BAR_COL10   CVBS_CMD(19,30, 5, 1,42)
#define PAL_BAR_COL11   CVBS_CMD( 5,10,30, 2,42)
#define PAL_BAR_COL12   CVBS_CMD(19,30, 5, 3,42)
#define PAL_BAR_COL13   CVBS_CMD( 5,10,30, 4,42)
#define PAL_BAR_COL14   CVBS_CMD(19,30, 5, 5,42)
#define PAL_BAR_COL15   CVBS_CMD( 5,10,30, 6,42)
#define PAL_BAR_COL16   CVBS_CMD(19,30, 5, 6,42)
#define PAL_LONG_SYNC_L  CVBS_CMD( 0, 0, 0, 0,126)
#define PAL_LONG_SYNC_H  CVBS_CMD(18,18,18, 0, 10)
#define PAL_SHORT_SYNC_L CVBS_CMD( 0, 0, 0, 0, 10)
#define PAL_SHORT_SYNC_H CVBS_CMD(18,18,18, 0,126)
#define PAL_BLANKING     CVBS_CMD(18,18,18, 0,210)

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
   PAL_HSYNC,
   PAL_BACKPORCH,
   PAL_COLORBURST_O,
   PAL_BACKPORCH,
   PAL_BAR_BLK,
   PAL_BAR_WHT,
   PAL_BAR_COL1,
   PAL_BAR_COL2,
   PAL_BAR_COL3,
   PAL_FRONTPORCH,
};

uint32_t test_scanline_even[] = {
   PAL_HSYNC,
   PAL_BACKPORCH,
   PAL_COLORBURST_E,
   PAL_BACKPORCH,
   PAL_BAR_BLK,
   PAL_BAR_WHT,
   PAL_BAR_COL1,
   PAL_BAR_COL2,
   PAL_BAR_COL3,
   PAL_FRONTPORCH,
};

uint32_t test_blanking_line[] = {
   PAL_HSYNC,
   PAL_BACKPORCH,
   PAL_COLORBURST_O,
   PAL_BACKPORCH,
   PAL_BLANKING,
   PAL_FRONTPORCH,
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
   sm_config_set_out_shift(&config, true, false, 0); 
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
         //if(lines & 1u){
            pio_sm_put(CVBS_PIO, CVBS_SM, test_scanline_odd[i++]);
         //}else{
         //   pio_sm_put(CVBS_PIO, CVBS_SM, test_scanline_even[i++]);
         //}
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
         pio_sm_put(CVBS_PIO, CVBS_SM, test_blanking_line[i++]);
         if(i >= count_of(test_blanking_line)){
            i = 0;
            lines++;
         }
      }else{
         i = 0;
         lines = 0;
      }
   }
}
