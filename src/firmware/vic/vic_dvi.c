/*
* Copyright (c) 2026 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "vic/vic.h"
#include "vic/vic_dvi.h"
#include "sys/cfg.h"
#include "sys/dvi.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

dvi_modeline_t vic_dvi_pal_modes[] = {
//dvi_modeline_t vic_pal_mode_640x480p60 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = 8,
    .offset_y = 80,
    .hstx_div = 2,
    .h_front_porch = 16,
    .h_sync_width = 96,
    .h_back_porch = 48+122,
    .h_active_pixels = 640,
    .v_front_porch = 10,
    .v_sync_width = 2,
    .v_back_porch = 33+52,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_pal_mode_640x480p76 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = 8,
    .offset_y = 80,
    .hstx_div = 2,
    .h_front_porch = 16,
    .h_sync_width = 96,
    .h_back_porch = 48,
    .h_active_pixels = 640,
    .v_front_porch = 10,
    .v_sync_width = 2,
    .v_back_porch = 33,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_pal_mode_720x480p60x3 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -4,
    .offset_y = 85,
    .hstx_div = 2,
    .h_front_porch = 16,
    .h_sync_width = 62,
    .h_back_porch = 60+123,
    .h_active_pixels = 720,
    .v_front_porch = 6,
    .v_sync_width = 6-2,
    .v_back_porch = 30+20,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_pal_mode_720x480p60x2 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 2,
    .scale_y = 1,
    .offset_x = -80,
    .offset_y = -60,
    .hstx_div = 2,
    .h_front_porch = 16,
    .h_sync_width = 62,
    .h_back_porch = 60+123,
    .h_active_pixels = 720,
    .v_front_porch = 6,
    .v_sync_width = 6-2,
    .v_back_porch = 30+20,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_pal_mode_720x576p50 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -5,
    .offset_y = 50,
    .hstx_div = 2,
    .h_front_porch = 12,
    .h_sync_width = 64+64,
    .h_back_porch = 68+60,
    .h_active_pixels = 720,
    .v_front_porch = 5,
    .v_sync_width = 5-1,
    .v_back_porch = 39+22+1,
    .v_active_lines = 576,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_pal_mode_856x576p50 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -25,
    .offset_y = 50,
    .hstx_div = 2,
    .h_front_porch = 24,
    .h_sync_width = 80,
    .h_back_porch = 80,
    .h_active_pixels = 856,
    .v_front_porch = 3,
    .v_sync_width = 10,
    .v_back_porch = 10,
    .v_active_lines = 576,
    .sync_polarity = vpos_hneg,
},
//dvi_modeline_t vic_pal_mode_800x600p54 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -18,
    .offset_y = 10,
    .hstx_div = 2,
    .h_front_porch = 48,
    .h_sync_width = 32,
    .h_back_porch = 80,
    .h_active_pixels = 800,
    .v_front_porch = 3,
    .v_sync_width = 4,
    .v_back_porch = 11,
    .v_active_lines = 600,
    .sync_polarity = vneg_hpos,
},
//dvi_modeline_t vic_pal_mode_1280x720p50 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 4,
    .scale_y = 2,
    .offset_x = -50,
    .offset_y = -50,
    .hstx_div = 1,
    .h_front_porch = 128,
    .h_sync_width = 128,
    .h_back_porch = 76+48,
    .h_active_pixels = 1280,
    .v_front_porch = 23,
    .v_sync_width = 5-1,
    .v_back_porch = 16+5+1,
    .v_active_lines = 720,
    .sync_polarity = vpos_hneg,
},

};

dvi_modeline_t vic_dvi_ntsc_modes[] = {
//dvi_modeline_t vic_ntsc_mode_640x480p60 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -7,
    .offset_y = 40,
    .hstx_div = 2,
    .h_front_porch = 16,
    .h_sync_width = 96,
    .h_back_porch = 48+121,
    .h_active_pixels = 640,
    .v_front_porch = 10,
    .v_sync_width = 2+45,
    .v_back_porch = 33,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_ntsc_mode_640x480p75 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -7,
    .offset_y = 40,
    .hstx_div = 2,
    .h_front_porch = 16,
    .h_sync_width = 96,
    .h_back_porch = 48,
    .h_active_pixels = 640,
    .v_front_porch = 10,
    .v_sync_width = 2,
    .v_back_porch = 33,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_ntsc_mode_720x480p60x3 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,//2,
    .scale_y = 2,//1,
    .offset_x = -20,//-80,
    .offset_y = 40,//-100,
    .hstx_div = 2,
    .h_front_porch = 16,
    .h_sync_width = 62,
    .h_back_porch = 60+108,
    .h_active_pixels = 720,
    .v_front_porch = 6,
    .v_sync_width = 6-2,
    .v_back_porch = 30+20,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_ntsc_mode_720x480p60px2 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 2,
    .scale_y = 1,
    .offset_x = -80,
    .offset_y = -100,
    .hstx_div = 2,
    .h_front_porch = 16,
    .h_sync_width = 62,
    .h_back_porch = 60+108,
    .h_active_pixels = 720,
    .v_front_porch = 6,
    .v_sync_width = 6-2,
    .v_back_porch = 30+20,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
},
//dvi_modeline_t vic_ntsc_mode_720x524p60 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -20,
    .offset_y = 5,
    .hstx_div = 2,
    .h_front_porch = 24,
    .h_sync_width = 64,
    .h_back_porch = 88+63,
    .h_active_pixels = 720,
    .v_front_porch = 3,
    .v_sync_width = 10,
    .v_back_porch = 8+4,
    .v_active_lines = 524,
    .sync_polarity = vpos_hneg,
},
//dvi_modeline_t vic_ntsc_mode_720x576p60 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -20,
    .offset_y = -20,
    .hstx_div = 2,
    .h_front_porch = 48,
    .h_sync_width = 32,
    .h_back_porch = 80+3,
    .h_active_pixels = 720,
    .v_front_porch = 3,
    .v_sync_width = 7-3,
    .v_back_porch = 7+1+3,
    .v_active_lines = 576,
    .sync_polarity = vneg_hpos,
},
//dvi_modeline_t vic_ntsc_mode_800x600p54 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = -35,
    .offset_y = -40,
    .hstx_div = 2,
    .h_front_porch = 48,
    .h_sync_width = 32,
    .h_back_porch = 80,
    .h_active_pixels = 800,
    .v_front_porch = 3,
    .v_sync_width = 4,
    .v_back_porch = 11,
    .v_active_lines = 600,
    .sync_polarity = vneg_hpos,
},
//dvi_modeline_t vic_ntsc_mode_1280x720p60 = 
{
    .pixel_format = dvi_4_rgb332,
    .scale_x = 4,
    .scale_y = 2,
    .offset_x = -70,
    .offset_y = -50,
    .hstx_div = 1,
    .h_front_porch = 48,
    .h_sync_width = 32,
    .h_back_porch = 80,
    .h_active_pixels = 1280,
    .v_front_porch = 3,
    .v_sync_width = 5-1,
    .v_back_porch = 13,
    .v_active_lines = 720,
    .sync_polarity = vneg_hpos,
},

};

void vic_dvi_init_ntsc(void){
    uint8_t dvi_mode = cfg_get_dvi();
    if(dvi_mode > count_of(vic_dvi_ntsc_modes))
        dvi_mode = 0;
    dvi_set_modeline(&vic_dvi_ntsc_modes[dvi_mode]);
}

void vic_dvi_init_pal(void){
    uint8_t dvi_mode = cfg_get_dvi();
    if(dvi_mode > count_of(vic_dvi_pal_modes))
        dvi_mode = 0;
    dvi_set_modeline(&vic_dvi_pal_modes[dvi_mode]);
}


void vic_print_dvi_modes(void){
    dvi_modeline_t *modes;
    int count;
    uint8_t sel = cfg_get_dvi();
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
        case(VIC_MODE_NTSC_SVIDEO):
        case(VIC_MODE_TEST_NTSC_SVIDEO):
            modes = vic_dvi_ntsc_modes;
            count = count_of(vic_dvi_ntsc_modes);
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
        case(VIC_MODE_PAL_SVIDEO):
        case(VIC_MODE_TEST_PAL_SVIDEO):
            modes = vic_dvi_pal_modes;
            count = count_of(vic_dvi_pal_modes);
            break;
        default:
            count = 0;
    }
    for(int i=0; i < count; i++){
        printf("%c %2d - ", (sel == i ? '*' : ' '), i);
        dvi_print_modeline(&modes[i]);
    }
}