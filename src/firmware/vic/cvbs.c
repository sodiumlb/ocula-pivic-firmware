/*
* Copyright (c) 2025 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
//#include "sys/ria.h"
#include "vic/cvbs.h"
#include "vic/cvbs_ntsc.h"
#include "sys/cfg.h"
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

static uint8_t cvbs_mode;

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



//Colodore colours, approximated
uint32_t pal_test_vsync[] = {
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

uint32_t ntsc_test_vsync[] = {
   //Lines 259-261
   NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H, NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H,
   NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H, NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H,
   NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H, NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H,

   //Lines 1-6
   NTSC_LONG_SYNC_L, NTSC_LONG_SYNC_H, NTSC_LONG_SYNC_L, NTSC_LONG_SYNC_H,
   NTSC_LONG_SYNC_L, NTSC_LONG_SYNC_H, NTSC_LONG_SYNC_L, NTSC_LONG_SYNC_H,
   NTSC_LONG_SYNC_L, NTSC_LONG_SYNC_H, NTSC_LONG_SYNC_L, NTSC_LONG_SYNC_H,
   NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H, NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H,
   NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H, NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H,
   NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H, NTSC_SHORT_SYNC_L, NTSC_SHORT_SYNC_H,
};


uint32_t pal_test_scanline_odd[] = {
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

uint32_t ntsc_test_scanline_odd[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   NTSC_BURST_O,
   NTSC_BACKPORCH,
   CVBS_CMD_PIXEL_RUN(200),
};

uint32_t pal_test_scanline_even[] = {
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

uint32_t ntsc_test_scanline_even[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   NTSC_BURST_E,
   NTSC_BACKPORCH,
   CVBS_CMD_PIXEL_RUN(200),
};
uint32_t pal_test_blanking_line_odd[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_BURST_O,
   PAL_BACKPORCH,
   PAL_BLANKING,
};

uint32_t ntsc_test_blanking_line_odd[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   NTSC_BURST_O,
   NTSC_BACKPORCH,
   NTSC_BLANKING,
};

uint32_t pal_test_blanking_line_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_BURST_E,
   PAL_BACKPORCH,
   PAL_BLANKING,
};

uint32_t ntsc_test_blanking_line_even[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   NTSC_BURST_E,
   NTSC_BACKPORCH,
   NTSC_BLANKING,
};

static const uint32_t ntsc_palette[8][16] = {
   NTSC_BLACK,NTSC_WHITE,NTSC_RED_0,NTSC_CYAN_0,NTSC_PURPLE_0,NTSC_GREEN_0,NTSC_BLUE_0,NTSC_YELLOW_0,NTSC_ORANGE_0,NTSC_LORANGE_0,NTSC_PINK_0,NTSC_LCYAN_0,NTSC_LPURPLE_0,NTSC_LGREEN_0,NTSC_LBLUE_0,NTSC_LYELLOW_0,
   NTSC_BLACK,NTSC_WHITE,NTSC_RED_1,NTSC_CYAN_1,NTSC_PURPLE_1,NTSC_GREEN_1,NTSC_BLUE_1,NTSC_YELLOW_1,NTSC_ORANGE_1,NTSC_LORANGE_1,NTSC_PINK_1,NTSC_LCYAN_1,NTSC_LPURPLE_1,NTSC_LGREEN_1,NTSC_LBLUE_1,NTSC_LYELLOW_1,
   NTSC_BLACK,NTSC_WHITE,NTSC_RED_2,NTSC_CYAN_2,NTSC_PURPLE_2,NTSC_GREEN_2,NTSC_BLUE_2,NTSC_YELLOW_2,NTSC_ORANGE_2,NTSC_LORANGE_2,NTSC_PINK_2,NTSC_LCYAN_2,NTSC_LPURPLE_2,NTSC_LGREEN_2,NTSC_LBLUE_2,NTSC_LYELLOW_2,
   NTSC_BLACK,NTSC_WHITE,NTSC_RED_3,NTSC_CYAN_3,NTSC_PURPLE_3,NTSC_GREEN_3,NTSC_BLUE_3,NTSC_YELLOW_3,NTSC_ORANGE_3,NTSC_LORANGE_3,NTSC_PINK_3,NTSC_LCYAN_3,NTSC_LPURPLE_3,NTSC_LGREEN_3,NTSC_LBLUE_3,NTSC_LYELLOW_3,
   NTSC_BLACK,NTSC_WHITE,NTSC_RED_4,NTSC_CYAN_4,NTSC_PURPLE_4,NTSC_GREEN_4,NTSC_BLUE_4,NTSC_YELLOW_4,NTSC_ORANGE_4,NTSC_LORANGE_4,NTSC_PINK_4,NTSC_LCYAN_4,NTSC_LPURPLE_4,NTSC_LGREEN_4,NTSC_LBLUE_4,NTSC_LYELLOW_4,
   NTSC_BLACK,NTSC_WHITE,NTSC_RED_5,NTSC_CYAN_5,NTSC_PURPLE_5,NTSC_GREEN_5,NTSC_BLUE_5,NTSC_YELLOW_5,NTSC_ORANGE_5,NTSC_LORANGE_5,NTSC_PINK_5,NTSC_LCYAN_5,NTSC_LPURPLE_5,NTSC_LGREEN_5,NTSC_LBLUE_5,NTSC_LYELLOW_5,
   NTSC_BLACK,NTSC_WHITE,NTSC_RED_6,NTSC_CYAN_6,NTSC_PURPLE_6,NTSC_GREEN_6,NTSC_BLUE_6,NTSC_YELLOW_6,NTSC_ORANGE_6,NTSC_LORANGE_6,NTSC_PINK_6,NTSC_LCYAN_6,NTSC_LPURPLE_6,NTSC_LGREEN_6,NTSC_LBLUE_6,NTSC_LYELLOW_6,
   NTSC_BLACK,NTSC_WHITE,NTSC_RED_7,NTSC_CYAN_7,NTSC_PURPLE_7,NTSC_GREEN_7,NTSC_BLUE_7,NTSC_YELLOW_7,NTSC_ORANGE_7,NTSC_LORANGE_7,NTSC_PINK_7,NTSC_LCYAN_7,NTSC_LPURPLE_7,NTSC_LGREEN_7,NTSC_LBLUE_7,NTSC_LYELLOW_7,
};

void cvbs_pio_dotclk_init(void){
   uint offset = pio_add_program(CVBS_DOTCLK_PIO, &cvbs_dotclk_program);
   pio_sm_config config = cvbs_dotclk_program_get_default_config(offset);   
   pio_sm_init(CVBS_DOTCLK_PIO, CVBS_DOTCLK_SM, offset, &config);
   pio_sm_set_enabled(CVBS_DOTCLK_PIO, CVBS_DOTCLK_SM, true);   
}

void cvbs_pio_mode_init(void){ 
   cvbs_mode = cfg_get_mode();
   pio_set_gpio_base (CVBS_PIO, CVBS_PIN_OFFS);
   for(uint32_t i = 0; i < 5; i++){
      pio_gpio_init(CVBS_PIO, CVBS_PIN_BASE+i);
      gpio_set_drive_strength(CVBS_PIN_BASE+i, GPIO_DRIVE_STRENGTH_2MA);
      gpio_set_slew_rate(CVBS_PIN_BASE+i, GPIO_SLEW_RATE_SLOW);
   }
   pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM, CVBS_PIN_BASE, 5, true);
   uint offset;
   pio_sm_config config;
   if(cvbs_mode == CVBS_MODE_NTSC){
      offset = pio_add_program(CVBS_PIO, &cvbs_ntsc_program);
      config = cvbs_ntsc_program_get_default_config(offset);
      sm_config_set_out_shift(&config, true, true, 25); 
   }else{
      offset = pio_add_program(CVBS_PIO, &cvbs_program);
      config = cvbs_program_get_default_config(offset);
      sm_config_set_out_shift(&config, true, true, 27); 
   }
   sm_config_set_out_pins(&config, CVBS_PIN_BASE, 5);             
   sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
   pio_sm_init(CVBS_PIO, CVBS_SM, offset, &config);
   //pio_sm_put(CVBS_PIO, CVBS_SM, 0x84210FFF);    
   pio_sm_set_enabled(CVBS_PIO, CVBS_SM, true);   
   printf("CVBS mode init done\n");
}

void cvbs_test_img_pal(void){
   static uint32_t i = 0;
   static uint32_t lines = 0;
   while(!pio_sm_is_tx_fifo_full(CVBS_PIO,CVBS_SM)){
      //pio_sm_put(CVBS_PIO, CVBS_SM, CVBS_CMD(rev5bit[i],rev5bit[i],rev5bit[i],rev5bit[i],6,40));
      if(lines < 285){  
         if(lines & 1u){
            pio_sm_put(CVBS_PIO, CVBS_SM, pal_test_scanline_odd[i++]);
         }else{
            pio_sm_put(CVBS_PIO, CVBS_SM, pal_test_scanline_even[i++]);
         }
         if(i >= count_of(pal_test_scanline_odd)){  //Assuming same length
            i = 0;
            lines++;
         }
      }else if(lines < 293){ 
         //8 lines block
         pio_sm_put(CVBS_PIO, CVBS_SM, pal_test_vsync[i++]);
         if(i >= count_of(pal_test_vsync)){
            i = 0;
            lines+=8;
         }
      }else if(lines < 312){
         if(lines & 1u){
            pio_sm_put(CVBS_PIO, CVBS_SM, pal_test_blanking_line_odd[i++]);
         }else{
            pio_sm_put(CVBS_PIO, CVBS_SM, pal_test_blanking_line_even[i++]);
         }
         if(i >= count_of(pal_test_blanking_line_odd)){ //Assuming same length
            i = 0;
            lines++;
         }
      }else{
         i = 0;
         lines = 0;
      }
   }
}
void cvbs_test_img_ntsc(void){
   static uint32_t i = 0;
   static uint32_t j = 0;
   static uint32_t lines = 0;
   static uint32_t run_lines = 0;
   while(!pio_sm_is_tx_fifo_full(CVBS_PIO,CVBS_SM)){
      //pio_sm_put(CVBS_PIO, CVBS_SM, CVBS_CMD(rev5bit[i],rev5bit[i],rev5bit[i],rev5bit[i],6,40));
      if(lines < 234){  
         if(i < count_of(ntsc_test_scanline_odd)){  //Assuming same length both odd/even
            if(run_lines & 1u){
               pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_test_scanline_odd[i++]);
            }else{
               pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_test_scanline_even[i++]);
            }
         }else{
            if(j >= 200){
               i = 0;
               j = 0;
               lines++;
               run_lines++;
            }else{
               if(run_lines & 1u){
                  pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_palette[(j+0)&0x7][(j>>3)&0xF]);
               }else{
                  pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_palette[(j+4)&0x7][(j>>3)&0xF]);
               }
               j++;
            }
         }
      }else if(lines < 249){ 
         //9 lines block
         pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_test_vsync[i++]);
         if(i >= count_of(ntsc_test_vsync)){
            i = 0;
            lines+=9;
            run_lines+=9;
         }
      }else if(lines < 261){
         if(run_lines & 1u){
            pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_test_blanking_line_odd[i++]);
         }else{
            pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_test_blanking_line_even[i++]);
         }
         if(i >= count_of(ntsc_test_blanking_line_odd)){ //Assuming same length
            i = 0;
            lines++;
            run_lines++;
         }
      }else{
         i = 0;
         lines = 0;
      }
   }
}

void cvbs_test_loop(void){
   while(1){
         cvbs_test_img_ntsc();
   }
}

void cvbs_init(void){
   cvbs_pio_mode_init();   //Needs to be first
   cvbs_pio_dotclk_init();

   multicore_launch_core1(cvbs_test_loop);
}
void cvbs_task(void){
}