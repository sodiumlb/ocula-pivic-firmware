/*
* Copyright (c) 2025 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "str.h"
//#include "sys/ria.h"
#include "vic/vic.h"
#include "vic/cvbs.h"
#include "vic/cvbs_ntsc.h"
#include "vic/cvbs_pal.h"
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
   PAL_COLBURST_O,
   PAL_BACKPORCH,
};

uint32_t ntsc_test_scanline_odd[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   NTSC_COLBURST_O,
   NTSC_BACKPORCH,
};

uint32_t pal_test_scanline_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_COLBURST_E,
   PAL_BACKPORCH,
};

uint32_t ntsc_test_scanline_even[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   NTSC_COLBURST_E,
   NTSC_BACKPORCH,
};
uint32_t pal_test_blanking_line_odd[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_COLBURST_O,
   PAL_BACKPORCH,
   PAL_BLANKING,
};

uint32_t ntsc_test_blanking_line_odd[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   NTSC_COLBURST_O,
   NTSC_BACKPORCH,
   NTSC_BLANKING,
};

uint32_t pal_test_blanking_line_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   PAL_COLBURST_E,
   PAL_BACKPORCH,
   PAL_BLANKING,
};

uint32_t ntsc_test_blanking_line_even[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   NTSC_COLBURST_E,
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

static const uint32_t pal_palette_o[16] = {
   PAL_BLACK,PAL_WHITE,PAL_RED_O,PAL_CYAN_O,PAL_PURPLE_O,PAL_GREEN_O,PAL_BLUE_O,PAL_YELLOW_O,PAL_ORANGE_O,PAL_LORANGE_O,PAL_PINK_O,PAL_LCYAN_O,PAL_LPURPLE_O,PAL_LGREEN_O,PAL_LBLUE_O,PAL_LYELLOW_O
};
static const uint32_t pal_palette_e[16] = {
   PAL_BLACK,PAL_WHITE,PAL_RED_E,PAL_CYAN_E,PAL_PURPLE_E,PAL_GREEN_E,PAL_BLUE_E,PAL_YELLOW_E,PAL_ORANGE_E,PAL_LORANGE_E,PAL_PINK_E,PAL_LCYAN_E,PAL_LPURPLE_E,PAL_LGREEN_E,PAL_LBLUE_E,PAL_LYELLOW_E
};

void cvbs_pio_mode_init(void){ 
   pio_set_gpio_base (CVBS_PIO, CVBS_PIN_OFFS);
   for(uint32_t i = 0; i < 5; i++){
      pio_gpio_init(CVBS_PIO, CVBS_PIN_BASE+i);
      gpio_set_drive_strength(CVBS_PIN_BASE+i, GPIO_DRIVE_STRENGTH_8MA);
      gpio_set_slew_rate(CVBS_PIN_BASE+i, GPIO_SLEW_RATE_SLOW);
   }
   pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM, CVBS_PIN_BASE, 5, true);
   uint offset;
   pio_sm_config config;
   switch(cvbs_mode){
      case(VIC_MODE_NTSC):
      case(VIC_MODE_TEST_NTSC):
         offset = pio_add_program(CVBS_PIO, &cvbs_ntsc_program);
         config = cvbs_ntsc_program_get_default_config(offset);
         break;
      case(VIC_MODE_PAL):
      case(VIC_MODE_TEST_PAL):
      default:
         offset = pio_add_program(CVBS_PIO, &cvbs_pal_program);
         config = cvbs_pal_program_get_default_config(offset);
         break;
      }
   sm_config_set_out_shift(&config, true, true, 30); 
   sm_config_set_out_pins(&config, CVBS_PIN_BASE, 5);             
   sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
   pio_sm_init(CVBS_PIO, CVBS_SM, offset, &config);
   //pio_sm_put(CVBS_PIO, CVBS_SM, 0x84210FFF);    
   pio_sm_set_enabled(CVBS_PIO, CVBS_SM, true);   
   printf("CVBS mode init done\n");
}

void cvbs_test_img_pal(void){
   static uint32_t i = 0;
   static uint32_t j = 0;
   static uint32_t lines = 0;
   while(!pio_sm_is_tx_fifo_full(CVBS_PIO,CVBS_SM)){
      //pio_sm_put(CVBS_PIO, CVBS_SM, CVBS_CMD(rev5bit[i],rev5bit[i],rev5bit[i],rev5bit[i],6,40));
      if(lines < 285){
         if(i < count_of(pal_test_scanline_odd)){   //Assuming same length  
            if(lines & 1u){
               pio_sm_put(CVBS_PIO, CVBS_SM, pal_test_scanline_odd[i++]);
            }else{
               pio_sm_put(CVBS_PIO, CVBS_SM, pal_test_scanline_even[i++]);
            }
         }else{
            if(j >= 234){
               i = 0;
               j = 0;
               lines++;
            }else{
               if(lines & 1u){
                  pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette_o[(j>>3)&0xF]);
               }else{
                  pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette_e[(j>>3)&0xF]);
               }
               j++;
            }
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

void cvbs_ntsc_gen(uint32_t* col,uint8_t delay,uint8_t base_col){

   uint8_t L0 = (ntsc_palette[0][base_col] >>  0) & 0x1F;
   uint8_t L1 = (ntsc_palette[0][base_col] >> 10) & 0x1F;
   uint8_t head[] = { 0, 6,11,17};
   uint8_t pixlen[] = {38,39,39,39};
   if(delay > 22){
      uint8_t tmp = L0;
      L0 = L1;
      L1 = tmp;
      delay = delay - 22;
   }
   printf("Delay:%2d\n", delay);
   for(uint8_t i=0; i < 4; i++){
      uint8_t delay0 = delay + head[i]; 
      uint8_t delay1 = 22;
      uint8_t L0o, L1o, L2o;
      if(delay0 > 22){
         delay0 = delay0 - 22;
         L0o = L0;
         L1o = L1;
         L2o = L0;
      }else if(delay0 == 0){
         delay0 = 22;
         L0o = L0;
         L1o = L1;
         L2o = L0;
      }else{
         L0o = L1;
         L1o = L0;
         L2o = L1;
      }
      if(delay0 < 3 && delay0 > 0){
         delay1 = delay1 - (3 - delay0);
         delay0 = 3;
      }
      if((delay0+delay1) >= pixlen[i] || delay1 == 0){
         delay1 = 3;
         L2o = L1o;
      }
      printf("%2d L0:%2d d0:%2d L1:%2d d1:%2d L2:%2d\n", i, L0o, delay0, L1o, delay1, L2o);
      col[i] = CVBS_CMD_PIXEL(L0o, delay0, L1o, delay1, L2o);
      if(L1o == L2o){
         col[i+4] = CVBS_CMD_PIXEL(L1o, delay0, L0o, delay1, L0o);
      }else{
         col[i+4] = CVBS_CMD_PIXEL(L1o, delay0, L0o, delay1, L1o);
      }
   }

}

volatile uint8_t ntsc_test_col = 2;
volatile uint32_t ntsc_col[8];

void cvbs_test_img_ntsc(void){
   static uint32_t i = 0;
   static uint32_t j = 0;
   static uint32_t lines = 0;
   static uint32_t run_lines = 0;
   while(!pio_sm_is_tx_fifo_full(CVBS_PIO,CVBS_SM)){
      //pio_sm_put(CVBS_PIO, CVBS_SM, CVBS_CMD(rev5bit[i],rev5bit[i],rev5bit[i],rev5bit[i],6,40));
      if(lines < 120){  
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
                  pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_palette[(j+0+2)&0x7][(j>>3)&0xF]);
               }else{
                  pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_palette[(j+4+2)&0x7][(j>>3)&0xF]);
               }
               j++;
            }
         }
      }else if (lines < 240){
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
                  pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_col[(j+0+2)&0x7]);
               }else{
                  pio_sm_put(CVBS_PIO, CVBS_SM, ntsc_col[(j+4+2)&0x7]);
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

void cvbs_test_loop_ntsc(void){
   while(1){
         cvbs_test_img_ntsc();
   }
}

void cvbs_test_loop_pal(void){
   while(1){
         cvbs_test_img_pal();
   }
}

void cvbs_init(void){
   cvbs_mode = cfg_get_mode();
   cvbs_pio_mode_init();   //Needs to be first
   switch(cvbs_mode){
      case(VIC_MODE_TEST_PAL):
         multicore_launch_core1(cvbs_test_loop_pal);
         break;
      case(VIC_MODE_TEST_NTSC):
         cvbs_ntsc_gen((uint32_t*)&ntsc_col,0,ntsc_test_col);
         multicore_launch_core1(cvbs_test_loop_ntsc);
         break;
      default:             //Don't run test screens in normal modes
         break;
   };
}

void cvbs_task(void){
}

void cvbs_mon_tune(const char *args, size_t len){
   uint32_t val;
   if (len)
   {
       if (parse_uint32(&args, &len, &val) &&
           parse_end(args, len))
       {
         cvbs_ntsc_gen((uint32_t*)&ntsc_col, val, ntsc_test_col);
       }
       else
       {
           printf("?invalid argument\n");
           return;
       }
   }
}

void cvbs_mon_colour(const char *args, size_t len){
   uint32_t val;
   if (len)
   {
       if (parse_uint32(&args, &len, &val) &&
           parse_end(args, len))
       {
         ntsc_test_col = val & 0xF;
       }
       else
       {
           printf("?invalid argument\n");
           return;
       }
   }
}

void cvbs_print_status(void){
   printf("CVBS FIFO dbg:%08x lvl:%08x\n", CVBS_PIO->fdebug, CVBS_PIO->flevel);
   CVBS_PIO->fdebug = CVBS_PIO->fdebug;            //Clear FIFO status
}
