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
#include "sys/lfs.h"
#include "sys/mem.h"
#include "sys/rev.h"
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

static uint8_t cvbs_mode;
uint32_t cvbs_burst_cmd_odd; 
uint32_t cvbs_burst_cmd_even;
cvbs_palette_t cvbs_source_palette;
uint32_t cvbs_palette[8][16];

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
   0,
   PAL_BACKPORCH,
};

uint32_t ntsc_test_scanline_odd[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   0,
   NTSC_BACKPORCH,
};

uint32_t pal_test_scanline_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   0,
   PAL_BACKPORCH,
};

uint32_t ntsc_test_scanline_even[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   0,
   NTSC_BACKPORCH,
};

uint32_t pal_test_blanking_line_odd[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   0,
   PAL_BACKPORCH,
   PAL_BLANKING,
};

uint32_t ntsc_test_blanking_line_odd[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   0,
   NTSC_BACKPORCH,
   NTSC_BLANKING,
};

uint32_t pal_test_blanking_line_even[] = {
   PAL_FRONTPORCH,
   PAL_HSYNC,
   PAL_BREEZEWAY,
   0,
   PAL_BACKPORCH,
   PAL_BLANKING,
};

uint32_t ntsc_test_blanking_line_even[] = {
   NTSC_FRONTPORCH,
   NTSC_HSYNC,
   NTSC_BREEZEWAY,
   0,
   NTSC_BACKPORCH,
   NTSC_BLANKING,
};

void cvbs_pio_mode_init(void){ 
   pio_set_gpio_base (CVBS_PIO, CVBS_PIN_OFFS);
   uint offset, entry;
   pio_sm_config config;
   bool is_svideo = false;
   bool is_pal = false;
   switch(cvbs_mode){
      case(VIC_MODE_NTSC_SVIDEO):
      case(VIC_MODE_TEST_NTSC_SVIDEO):
         is_svideo = true;
      case(VIC_MODE_NTSC):
      case(VIC_MODE_TEST_NTSC):
         offset = pio_add_program(CVBS_PIO, &cvbs_ntsc_program);
         config = cvbs_ntsc_program_get_default_config(offset);
         entry = cvbs_ntsc_offset_entry;
         break;
      case(VIC_MODE_PAL_SVIDEO):
      case(VIC_MODE_TEST_PAL_SVIDEO):
         is_svideo = true;
      case(VIC_MODE_PAL):
      case(VIC_MODE_TEST_PAL):
      default:
         is_pal = true;
         offset = pio_add_program(CVBS_PIO, &cvbs_pal_program);
         config = cvbs_pal_program_get_default_config(offset);
         entry = cvbs_pal_offset_entry;
         break;
   }
   if(is_svideo && rev_get() == REV_1_2){
      for(uint32_t i = 0; i < CVBS_SVIDEO_PIN_COUNT; i++){
         pio_gpio_init(CVBS_PIO, CVBS_SVIDEO_PIN_BASE+i);
         //gpio_set_drive_strength(CVBS_SVIDEO_PIN_BASE+i,GPIO_DRIVE_STRENGTH_2MA);
         gpio_set_slew_rate(CVBS_SVIDEO_PIN_BASE+i, GPIO_SLEW_RATE_FAST);
      }
      pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM, CVBS_SVIDEO_PIN_BASE, CVBS_SVIDEO_PIN_COUNT, true);
      //pio_gpio_init(CVBS_PIO, CVBS_SYNC_PIN);
      //pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM, CVBS_SYNC_PIN, 1, true);
      gpio_set_pulls(CVBS_SYNC_PIN, false, false); //Not used for SVIDEO
      sm_config_set_out_pins(&config, CVBS_SVIDEO_PIN_BASE, CVBS_SVIDEO_PIN_COUNT);
      sm_config_set_set_pins(&config, CVBS_LUMA_PIN_BASE, CVBS_LUMA_PIN_COUNT);             
   }else{
      uint pin_base;
      if(rev_get() == REV_1_1){
         pin_base = CVBS_PIN_BASE_1_1;
      }else{
         pin_base = CVBS_PIN_BASE_1_2;
      }
      for(uint32_t i = 0; i < CVBS_PIN_COUNT; i++){
         pio_gpio_init(CVBS_PIO, pin_base+i);
         //gpio_set_drive_strength(CVBS_PIN_BASE+i, GPIO_DRIVE_STRENGTH_2MA);
         gpio_set_slew_rate(pin_base+i, GPIO_SLEW_RATE_FAST);
      }
      gpio_set_pulls(CVBS_SYNC_PIN, false, false); //Not used for direct CVBS
      gpio_set_pulls(CVBS_SVIDEO_PIN_BASE+0, false, false);
      gpio_set_pulls(CVBS_SVIDEO_PIN_BASE+1, false, false);
      pio_sm_set_consecutive_pindirs(CVBS_PIO, CVBS_SM, pin_base, CVBS_PIN_COUNT, true);
      //Trick to reuse as much defines and code as possible. Lower two bits (chroma) are ignored on Rev 1.2
      //and not mapped to PIO in Rev 1.1
      sm_config_set_out_pins(&config, pin_base-2, CVBS_PIN_COUNT+2);
      sm_config_set_set_pins(&config, pin_base, CVBS_PIN_COUNT);             
   }

   sm_config_set_out_shift(&config, true, true, 32); 
   sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
   pio_sm_init(CVBS_PIO, CVBS_SM, offset, &config);
   //pio_sm_put(CVBS_PIO, CVBS_SM, 0x84210FFF);    
   pio_sm_exec_wait_blocking(CVBS_PIO, CVBS_SM, pio_encode_jmp(entry));
   pio_sm_set_enabled(CVBS_PIO, CVBS_SM, true);   
   printf("CVBS mode init done\n");
}

void cvbs_test_img_pal(void){
   static uint32_t i = 0;
   static uint32_t j = 0;
   static uint32_t lines = 0;
   while(!pio_sm_is_tx_fifo_full(CVBS_PIO,CVBS_SM)){
      //pio_sm_put(CVBS_PIO, CVBS_SM, CVBS_CMD(i,i,i,i,6,40));
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
                  pio_sm_put(CVBS_PIO, CVBS_SM, cvbs_palette[0][(j>>3)&0xF]);
               }else{
                  pio_sm_put(CVBS_PIO, CVBS_SM, cvbs_palette[1][(j>>3)&0xF]);
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

uint8_t cvbs_tune_col = 2;

void cvbs_test_img_ntsc(void){
   static uint32_t i = 0;
   static uint32_t j = 0;
   static uint32_t lines = 0;
   static uint32_t run_lines = 0;
   
   while(!pio_sm_is_tx_fifo_full(CVBS_PIO,CVBS_SM)){
      //pio_sm_put(CVBS_PIO, CVBS_SM, CVBS_CMD(i,i,i,i,6,40));
      if(lines < 240){  
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
                  pio_sm_put(CVBS_PIO, CVBS_SM, cvbs_palette[(j+0+2)&0x7][(j>>3)&0xF]);
               }else{
                  pio_sm_put(CVBS_PIO, CVBS_SM, cvbs_palette[(j+4+2)&0x7][(j>>3)&0xF]);
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
   ntsc_test_scanline_odd[3] = cvbs_burst_cmd_odd;
   ntsc_test_scanline_even[3] = cvbs_burst_cmd_even;
   ntsc_test_blanking_line_odd[3] = cvbs_burst_cmd_odd;
   ntsc_test_blanking_line_even[3] = cvbs_burst_cmd_even;   
   while(1){
         cvbs_test_img_ntsc();
   }
}

void cvbs_test_loop_pal(void){
   pal_test_scanline_odd[3] = cvbs_burst_cmd_odd;
   pal_test_scanline_even[3] = cvbs_burst_cmd_even;
   pal_test_blanking_line_odd[3] = cvbs_burst_cmd_odd;
   pal_test_blanking_line_even[3] = cvbs_burst_cmd_even;   
   while(1){
         cvbs_test_img_pal();
   }
}

uint8_t cvbs_luma_chroma_to_dac(uint8_t luma, int8_t chroma, bool is_svideo){
   //Assumes luma is a 5 bit limit and chroma is 3 bit.
   if(is_svideo){
      if(chroma < 0){
         return luma << 2; 
      }else{
         return (luma << 2) | (chroma >> 1);
      }
   }else{   // 5 bit CVBS DAC
      return (luma + chroma) << 2;
   }
}

uint32_t cvbs_colour_to_pixel_pal(cvbs_colour_t col, bool is_odd, bool is_svideo, uint8_t truncate){
   uint8_t L0;
   uint8_t L1;
   uint8_t delay0;
   uint8_t delay1;
   if(is_odd || col.delay == 36){
      delay0 = col.delay;
      L0 = cvbs_luma_chroma_to_dac(col.luma, +col.chroma, is_svideo);
      L1 = cvbs_luma_chroma_to_dac(col.luma, -col.chroma, is_svideo);
   }else{
      delay0 = 36 - col.delay;
      L0 = cvbs_luma_chroma_to_dac(col.luma, -col.chroma, is_svideo);
      L1 = cvbs_luma_chroma_to_dac(col.luma, +col.chroma, is_svideo);
   }
   delay1 = 72 - delay0;
   if(delay1 > 36){
      delay1 = 36;
   }else{
      delay1 = 6;    //Special value to not output L0 again
   }
   return CVBS_CMD_PAL_PIXEL(L0, delay0, L1, delay1, truncate);
}

uint32_t cvbs_colour_to_burst_pal(cvbs_colour_t col, bool is_odd, bool is_svideo){
   uint8_t L0;
   uint8_t L1;
   uint8_t DC;
   uint8_t tmp;
   uint8_t delay;
   L0 = cvbs_luma_chroma_to_dac(col.luma, +col.chroma, is_svideo);
   L1 = cvbs_luma_chroma_to_dac(col.luma, -col.chroma, is_svideo);
   DC = cvbs_luma_chroma_to_dac(col.luma, 0, is_svideo);
   if(is_odd){
      delay = col.delay;
   }else{
      delay = col.delay + 18;
   }
   if(delay > 36){
      delay -= 36;
      tmp = L0; L0 = L1; L1 = tmp;  //Swap L0 and L1   delay1 = 36 - delay0;
   }
   return CVBS_CMD_PAL_BURST(L0, L1, DC, delay, 0);
}

uint32_t cvbs_colour_to_pixel_ntsc(cvbs_colour_t col, uint8_t idx, bool is_svideo){
   uint8_t L0;
   uint8_t L1;
   uint8_t tmp;
   uint8_t delay0;
   uint8_t delay1;
   uint32_t cmd;
   L0 = cvbs_luma_chroma_to_dac(col.luma, +col.chroma, is_svideo);
   L1 = cvbs_luma_chroma_to_dac(col.luma, -col.chroma, is_svideo);
   if(idx >= 4){
      idx -= 4;
      tmp = L0; L0 = L1; L1 = tmp;  //Swap L0 and L1
   }
   delay0 = col.delay + (idx * 11);
   while(delay0 > 44){
      delay0 -= 44;
      tmp = L0; L0 = L1; L1 = tmp;  //Swap L0 and L1
   }
   if(delay0 < 3){
      delay0 = 3;
   }
   delay1 = 44;
   if((delay0 + delay1) >= 77){
      delay1 = 4;    //Special value to not output L0 again
   }
   return CVBS_CMD_PIXEL(L0, delay0, L1, delay1, 0);
}

uint32_t cvbs_colour_to_burst_ntsc(cvbs_colour_t col, bool is_odd, bool is_svideo){
   uint8_t L0;
   uint8_t L1;
   uint8_t DC;
   uint8_t tmp;
   uint8_t delay = col.delay;
   L0 = cvbs_luma_chroma_to_dac(col.luma, +col.chroma, is_svideo);
   L1 = cvbs_luma_chroma_to_dac(col.luma, -col.chroma, is_svideo);
   DC = cvbs_luma_chroma_to_dac(col.luma, 0, is_svideo);
   if(!is_odd){
      tmp = L0; L0 = L1; L1 = tmp;  //Swap L0 and L1   delay1 = 36 - delay0;
   }
   while(delay > 44){
      delay -= 44;
      tmp = L0; L0 = L1; L1 = tmp;  //Swap L0 and L1   delay1 = 36 - delay0;
   }
   return CVBS_CMD_BURST(L0, L1, DC, delay, 17);
}

bool cvbs_calc_palette(uint8_t mode, cvbs_palette_t *src){
   cvbs_colour_t col;
   switch(mode){
      case(VIC_MODE_NTSC_SVIDEO):
         for(int i=0; i<8; i++){
            for(int j=0; j<16; j++){
               col = src->colours[j];
               cvbs_palette[i][j] = cvbs_colour_to_pixel_ntsc(col,i,true);
            }
         }
         cvbs_burst_cmd_odd  = cvbs_colour_to_burst_ntsc(src->burst, true, true);
         cvbs_burst_cmd_even = cvbs_colour_to_burst_ntsc(src->burst, false, true);
         break;
      case(VIC_MODE_NTSC):
         for(int i=0; i<8; i++){
            for(int j=0; j<16; j++){
               col = src->colours[j];
               cvbs_palette[i][j] = cvbs_colour_to_pixel_ntsc(col,i,false);
            }
         }
         cvbs_burst_cmd_odd  = cvbs_colour_to_burst_ntsc(src->burst, true, false);
         cvbs_burst_cmd_even = cvbs_colour_to_burst_ntsc(src->burst, false, false);
         break;
      case(VIC_MODE_PAL_SVIDEO):
         //memcpy(&cvbs_source_palette, &palette_default_pal, sizeof(cvbs_palette_t));
         for(int i=0; i<16; i++){
            col = src->colours[i];
            cvbs_palette[0][i] = cvbs_colour_to_pixel_pal(col,true,true,false);
            cvbs_palette[1][i] = cvbs_colour_to_pixel_pal(col,false,true,false);
            cvbs_palette[2][i] = cvbs_colour_to_pixel_pal(col,true,true,true);
            cvbs_palette[3][i] = cvbs_colour_to_pixel_pal(col,false,true,true);
         }
         cvbs_burst_cmd_odd  = cvbs_colour_to_burst_pal(src->burst, true, true);
         cvbs_burst_cmd_even = cvbs_colour_to_burst_pal(src->burst, false, true);
         break;
      case(VIC_MODE_PAL):
         //memcpy(&cvbs_source_palette, &palette_default_pal, sizeof(cvbs_palette_t));
         for(int i=0; i<16; i++){
            col = src->colours[i];
            cvbs_palette[0][i] = cvbs_colour_to_pixel_pal(col,true,false,false);
            cvbs_palette[1][i] = cvbs_colour_to_pixel_pal(col,false,false,false);
            cvbs_palette[2][i] = cvbs_colour_to_pixel_pal(col,true,false,true);
            cvbs_palette[3][i] = cvbs_colour_to_pixel_pal(col,false,false,true);
         }
         cvbs_burst_cmd_odd  = cvbs_colour_to_burst_pal(src->burst, true, false);
         cvbs_burst_cmd_even = cvbs_colour_to_burst_pal(src->burst, false, false);
         break;
      default:
         return false;
   }
   return true;
}

void cvbs_default_palette(uint8_t mode, const char *path){
   lfs_file_t lfs_file;
   LFS_FILE_CONFIG(lfs_file_config);
   char str[LFS_NAME_MAX];
   snprintf(str, 8, "p%d",mode);
   if(path){
      int lfs_result = lfs_file_opencfg(&lfs_volume, &lfs_file, str, LFS_O_RDWR | LFS_O_CREAT, &lfs_file_config);
      lfs_file_write(&lfs_volume, &lfs_file, path, LFS_NAME_MAX);
      lfs_file_close(&lfs_volume, &lfs_file);
      printf("Default palette for mode %d set to %s\n", mode, path);
   }else{
      if(lfs_file_opencfg(&lfs_volume, &lfs_file, str, LFS_O_RDONLY, &lfs_file_config) >= 0){
         lfs_gets(str, LFS_NAME_MAX, &lfs_volume, &lfs_file);
         puts(str+sizeof("/palettes")-1);           //Skip /palettes part of string
         lfs_file_close(&lfs_volume, &lfs_file);
      }
   }
}

bool cvbs_load_palette(uint8_t mode, const char *path){
   lfs_file_t lfs_file;
   LFS_FILE_CONFIG(lfs_file_config);
   char file_name[LFS_NAME_MAX];
   int lfs_result;
   if(!path){
      //Try loading stored mode default name file
      snprintf(file_name, 8, "p%d", mode);
      lfs_result = lfs_file_opencfg(&lfs_volume, &lfs_file, file_name, LFS_O_RDONLY, &lfs_file_config);
      if(lfs_result >= 0){
         lfs_file_read(&lfs_volume, &lfs_file, file_name, LFS_NAME_MAX);
         lfs_file_close(&lfs_volume, &lfs_file);
         //Open palette file
         lfs_result = lfs_file_opencfg(&lfs_volume, &lfs_file, file_name, LFS_O_RDONLY, &lfs_file_config);
      }else{
         //Load hardcoded default
         switch(mode){
            case(VIC_MODE_NTSC_SVIDEO):
            case(VIC_MODE_TEST_NTSC_SVIDEO):
            case(VIC_MODE_NTSC):
            case(VIC_MODE_TEST_NTSC):
               memcpy(&cvbs_source_palette, &palette_default_ntsc, sizeof(cvbs_palette_t));
               break;
            case(VIC_MODE_PAL_SVIDEO):
            case(VIC_MODE_TEST_PAL_SVIDEO):
            case(VIC_MODE_PAL):
            case(VIC_MODE_TEST_PAL):
               memcpy(&cvbs_source_palette, &palette_default_pal, sizeof(cvbs_palette_t));
               break;
         default:
            return false;
         }
         return true;
      }
   }else{  
      //Attempt to load palette from path
      lfs_result = lfs_file_opencfg(&lfs_volume, &lfs_file, path, LFS_O_RDONLY, &lfs_file_config);
   }
   if(lfs_result < 0){
      printf("?Error opening %s for reading (%d)\n", path, lfs_result);
      return false;
   }else{
      lfs_file_read(&lfs_volume, &lfs_file, &cvbs_source_palette, sizeof(cvbs_palette_t));
      lfs_file_close(&lfs_volume, &lfs_file);
   }
   return true;
}

bool cvbs_save_palette(const char *path){
   lfs_file_t lfs_file;
   LFS_FILE_CONFIG(lfs_file_config);
   int lfs_result = lfs_file_opencfg(&lfs_volume, &lfs_file, path, LFS_O_RDWR | LFS_O_CREAT, &lfs_file_config);
   if(lfs_result < 0){
      printf("?Error opening %s for writing (%d)\n", path, lfs_result);
      return false;
   }else{
      lfs_file_write(&lfs_volume, &lfs_file, &cvbs_source_palette, sizeof(cvbs_palette_t));
      lfs_file_close(&lfs_volume, &lfs_file);
      cvbs_default_palette(cvbs_mode, path);
   }
   return true;
}

void cvbs_init(void){
   cvbs_mode = cfg_get_mode();
   cvbs_load_palette(cvbs_mode, 0);
   cvbs_calc_palette(cvbs_mode, &cvbs_source_palette);
   cvbs_pio_mode_init();   //Needs to be first
   switch(cvbs_mode){
      case(VIC_MODE_TEST_PAL):
      case(VIC_MODE_TEST_PAL_SVIDEO):
         multicore_launch_core1(cvbs_test_loop_pal);
         break;
      case(VIC_MODE_TEST_NTSC):
      case(VIC_MODE_TEST_NTSC_SVIDEO):
         multicore_launch_core1(cvbs_test_loop_ntsc);
         break;
      default:             //Don't run test screens in normal modes
         break;
   };
}

void cvbs_task(void){
}

void cvbs_tune(uint8_t col_idx, int8_t delay_diff, int8_t luma_diff, int8_t chroma_diff){
   //TODO  Add post change limiters 
   cvbs_colour_t *col;
   if(col_idx < 16){
      col = &cvbs_source_palette.colours[col_idx];
   }else{
      col = &cvbs_source_palette.burst;
   }

   if(delay_diff)
      col->delay += delay_diff;
   if(luma_diff)
      col->luma += luma_diff;
   if(chroma_diff)
      col->chroma += chroma_diff;
}

static const char* colour_name[] = {
   "Black", "White", "Red", "Cyan", "Purple", "Green", "Blue", "Yellow", "Orange",
   "LOrange", "Pink", "LCyan", "LPurple", "LGreen", "LBlue", "LYellow",
   "Burst" };
void cvbs_print_col(uint8_t idx){
   cvbs_colour_t *col;
   if(idx < 16){
      col = &cvbs_source_palette.colours[idx];
   }else{
      col = &cvbs_source_palette.burst;
      idx = 16;
   }
   printf("%-8s: Hue/Phase %3d, Brightness/Luma %3d, Saturation/Chroma %3d\n", colour_name[idx], col->delay, col->luma, col->chroma);
}

void cvbs_print_palette_list(void){
   lfs_dir_t dir;
   struct lfs_info info;
   char path[16];
   for(int i=0; i<VIC_MODE_COUNT; i++){
      snprintf(path, 16, "/palettes/%d", i);
      if(lfs_dir_open(&lfs_volume, &dir, path) >= 0){
         while(lfs_dir_read(&lfs_volume, &dir, &info)){
            if(info.name[0] == '.')
               continue;
            printf("/%d/%s\n", i, info.name);
         lfs_dir_close(&lfs_volume, &dir);
         }
      }
   }
}

//Simple step tuning "tune +b, tune -s, tune +h"
void cvbs_mon_tune(const char *args, size_t len){
   uint8_t change;
   bool err = false;
   if (len>=2)
   {
      switch(args[0]){
         case('+'):
            change = +1;
            break;
         case('-'):
            change = -1;
            break;
         default:
           printf("?invalid argument\n");
           return;
      }
      switch(args[1]){
         //Saturation (chroma magnitude)
         case('s'):
         case('S'):
            cvbs_tune(cvbs_tune_col,0,0,change);
            break;
         //Hue (chroma delay/phase)
         case('h'):
         case('H'):
            cvbs_tune(cvbs_tune_col,change,0,0);
            break;
         //Brightness (luma)
         case('b'):
         case('B'):
            cvbs_tune(cvbs_tune_col,0,change,0);
            break;
         default:
           printf("?invalid argument\n");
           return;
      }
      cvbs_calc_palette(cvbs_mode, &cvbs_source_palette);
      cvbs_print_col(cvbs_tune_col);
   }
}

void cvbs_mon_colour(const char *args, size_t len){
   uint32_t val;
   if (len)
   {
       if (parse_uint32(&args, &len, &val) &&
           parse_end(args, len))
       {
         if(val > 16){
            val = 16;
         }
         cvbs_tune_col = val & 0xFF;
         cvbs_print_col(val);
       }
       else
       {
           printf("?invalid argument\n");
           return;
       }
   }else{
      for(int i=0; i<17; i++){
         printf("%c%2d ", i == cvbs_tune_col ? '*' : ' ', i);
         cvbs_print_col(i);
      }
   }
}

void cvbs_mon_save(const char *args, size_t len){
   if(len){
      char full_path[LFS_NAME_MAX+1];
      lfs_mkdir(&lfs_volume, "/palettes");
      snprintf(full_path, LFS_NAME_MAX, "/palettes/%d", cvbs_mode);
      lfs_mkdir(&lfs_volume, full_path);
      snprintf(full_path, LFS_NAME_MAX, "/palettes/%d/%s", cvbs_mode, args);
      cvbs_save_palette(full_path);
   }
}

void cvbs_mon_load(const char *args, size_t len){
   if(len){
      char full_path[LFS_NAME_MAX+1];
      if(args[0] == '-'){
         cvbs_load_palette(cvbs_mode, 0); //Load hardcoded default
         return;
      }else if(args[0] == '/'){
         snprintf(full_path, LFS_NAME_MAX, "/palettes%s", args);
      }else{
         snprintf(full_path, LFS_NAME_MAX, "/palettes/%d/%s", cvbs_mode, args);
      }
      cvbs_load_palette(cvbs_mode, full_path);
      cvbs_calc_palette(cvbs_mode, &cvbs_source_palette);
   }
}

// #define GET_L0(cmd) ( 0x7f & (cmd >> 5 )) 
// #define GET_L1(cmd) ( 0x7f & (cmd >> 18 ))
// #define GET_D0(cmd) ( 0x3f & (cmd >> 12 ))
// #define GET_D1(cmd) ( 0x3f & (cmd >> 25 )) 

bool active = false;
void cvbs_print_status(void){
   printf("CVBS FIFO debug:%08x level:%d\n", CVBS_PIO->fdebug, pio_sm_get_tx_fifo_level(CVBS_PIO, (CVBS_SM)));
   CVBS_PIO->fdebug = CVBS_PIO->fdebug;            //Clear FIFO status

   printf("Default palettes:\n");
   for(int i=0; i<VIC_MODE_COUNT; i++){
      cvbs_default_palette(i, 0);
   }
   printf("Available palettes:\n");
   cvbs_print_palette_list();

   rev_print();

   // for(int i=0; i<16; i++){
   //    printf(" %08x %08x %08x %08x\n", cvbs_palette[0][i], cvbs_palette[1][i],cvbs_palette[2][i],cvbs_palette[3][i]);
   // }

   // printf("Burst Odd delay  %d %08x\n", (cvbs_burst_cmd_odd>>5)&0x3F, cvbs_burst_cmd_odd);
   // printf("Burst Even delay %d %08x\n", (cvbs_burst_cmd_even>>5)&0x3F, cvbs_burst_cmd_even);
   // for(int i=0; i<16; i++){
   //    printf("%d:\n", i);
   //    for(int j=0; j<4; j++){ 
   //       uint32_t cmd = cvbs_palette[j][i];
   //       printf("\t%d(%3d,%3d,%2d,%2d)", j, GET_L0(cmd)>>2, GET_L1(cmd)>>2, GET_D0(cmd), GET_D1(cmd) );
   //       cmd = cvbs_palette[j+4][i];
   //       printf("\t%d(%3d,%3d,%2d,%2d)\n", j+4, GET_L0(cmd)>>2, GET_L1(cmd)>>2, GET_D0(cmd), GET_D1(cmd) );
   //    }
   //    //printf("\n");
   // }
}
