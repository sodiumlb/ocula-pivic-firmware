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
//L0 and L2 levels are asserted for a single cycle
//L1 and L3 levels are asserted for half periods
#define CVBS_CMD(L0,L1,L2,L3,delay,repeat) \
         (((L3&0x1F)<<27) | ((L2&0x1F)<<22) | ((L1&0x1F)<<17) | ((L0&0x1F)<<12) | ((repeat&0xFF)<<4) | ((delay&0x0F)))

//Experimental timings (eyeballing + exerimenting)
//Levels bit-reverse hard-coded
#define PAL_HSYNC       CVBS_CMD( 0, 0, 0, 0, 6,19)
#define PAL_BLANK       CVBS_CMD(18,18,18,18, 6,48)
#define PAL_FRONTPORCH  CVBS_CMD(18,18,18,18, 6,18)
#define PAL_BACKPORCH   CVBS_CMD(18,18,18,18, 6, 9)
#define PAL_COLORBURST  CVBS_CMD(26,18,28,18, 6,30)
#define PAL_VSYNC_PART  CVBS_CMD( 0, 0, 0, 0, 6,255)
#define PAL_BAR_BLK     CVBS_CMD(18,18,18,18, 6,40)
#define PAL_BAR_WHT     CVBS_CMD(23,23,23,23, 6,40)
#define PAL_BAR_COL1    CVBS_CMD( 5,30,10,30, 0,40)
#define PAL_BAR_COL2    CVBS_CMD(19, 5,30, 5, 2,40)
#define PAL_BAR_COL3    CVBS_CMD( 5,30,10,30, 4,40)
#define PAL_BAR_COL4    CVBS_CMD(19, 5,30, 5, 6,40)
#define PAL_BAR_COL5    CVBS_CMD( 5,30,10,30, 8,40)
#define PAL_BAR_COL6    CVBS_CMD(19, 5,30, 5, 6,40)
#define PAL_BAR_COL7    CVBS_CMD( 5,30,10,30, 8,40)
#define PAL_BAR_COL8    CVBS_CMD(19, 5,30, 5,10,40)
#define PAL_BAR_COL9    CVBS_CMD( 5,30,10,30,12,40)
#define PAL_BAR_COL10   CVBS_CMD(19, 5,30, 5,14,40)
#define PAL_BAR_COL11   CVBS_CMD( 5,30,10,30, 0,40)
#define PAL_BAR_COL12   CVBS_CMD(19, 5,30, 5, 2,40)
#define PAL_BAR_COL13   CVBS_CMD( 5,30,10,30, 4,40)
#define PAL_BAR_COL14   CVBS_CMD(19, 5,30, 5, 6,40)
#define PAL_BAR_COL15   CVBS_CMD( 5,30,10,30, 8,40)
#define PAL_BAR_COL16   CVBS_CMD(19, 5,30, 5, 6,40)
#define PAL_BAR_COL17   CVBS_CMD( 5,30,10,30, 8,40)
#define PAL_BAR_COL18   CVBS_CMD(19, 5,30, 5,10,40)
#define PAL_BAR_COL19   CVBS_CMD( 5,30,10,30,12,40)
#define PAL_BAR_COL20   CVBS_CMD(19, 5,30, 5,14,40)

uint32_t test_scanline[] = {
   PAL_HSYNC,
   PAL_BACKPORCH,
   PAL_COLORBURST,
   PAL_BACKPORCH,
   PAL_BAR_BLK,
   PAL_BAR_WHT,
   PAL_BAR_COL1,
   PAL_BAR_COL2,
   PAL_BAR_COL3,
   PAL_BAR_COL4,
   PAL_BAR_COL5,
   PAL_BAR_COL6,
   PAL_BAR_COL7,
   PAL_BAR_COL8,
   PAL_BAR_WHT,
   PAL_BAR_BLK,
   PAL_FRONTPORCH,
};

void cvbs_init(void){ 
   pio_set_gpio_base (CVBS_PIO, CVBS_PIN_BANK);
   for(uint32_t i = 0; i < 5; i++)
      pio_gpio_init(CVBS_PIO, CVBS_PIN_BASE+i);
   
   pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM, CVBS_PIN_BASE, 5, true);
   uint offset = pio_add_program(CVBS_PIO, &cvbs_program);
   pio_sm_config config = cvbs_program_get_default_config(offset);
   sm_config_set_out_pins(&config, CVBS_PIN_BASE, 5);             
   sm_config_set_out_shift(&config, true, false, 0); 
   pio_sm_init(CVBS_PIO, CVBS_SM, offset, &config);
   pio_sm_put(CVBS_PIO, CVBS_SM, 0x84210FFF);    
   pio_sm_set_enabled(CVBS_PIO, CVBS_SM, true);   
}

void cvbs_task(void){
   static uint32_t i = 0;
   while(!pio_sm_is_tx_fifo_full(CVBS_PIO,CVBS_SM)){
      pio_sm_put(CVBS_PIO, CVBS_SM, test_scanline[i++]);
      //pio_sm_put(CVBS_PIO, CVBS_SM, CVBS_CMD(rev5bit[i],rev5bit[i],rev5bit[i],rev5bit[i],6,40));
      if(i >= count_of(test_scanline))
         i = 0;
   }
}
