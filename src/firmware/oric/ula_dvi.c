/*
* Copyright (c) 2026 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "oric/ula.h"
#include "oric/ula_dvi.h"
#include "sys/cfg.h"
#include "sys/dvi.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

dvi_modeline_t ula_dvi_modes[] = {
    //VGA 640x480p60
    {
        .pixel_format = dvi_4_rgb332,
        .scale_x = 2,
        .scale_y = 2,
        .offset_x = -36,
        .offset_y = 2,
        .hstx_div = 2,
        .h_front_porch = 16,
        .h_sync_width = 96,
        .h_back_porch = 48+64,
        .h_active_pixels = 640,
        .v_front_porch = 10,
        .v_sync_width = 2,
        .v_back_porch = 33+7,
        .v_active_lines = 480,
        .sync_polarity = vneg_hneg
    },
    //VGA 640x480p66
    {
        .pixel_format = dvi_4_rgb332,
        .scale_x = 2,
        .scale_y = 2,
        .offset_x = -36,
        .offset_y = 2,
        .hstx_div = 2,
        .h_front_porch = 16,
        .h_sync_width = 96,
        .h_back_porch = 48,
        .h_active_pixels = 640,
        .v_front_porch = 10,
        .v_sync_width = 2,
        .v_back_porch = 33,
        .v_active_lines = 480,
        .sync_polarity = vneg_hneg
    },
    //576p 720x576p50
    {
        .pixel_format = dvi_4_rgb332,
        .scale_x = 3,
        .scale_y = 2,
        .offset_x = 6,
        .offset_y = -50,
        .hstx_div = 2,
        .h_front_porch = 12,
        .h_sync_width = 64,
        .h_back_porch = 68+15,
        .h_active_pixels = 720,
        .v_front_porch = 5,
        .v_sync_width = 5,
        .v_back_porch = 39+2,
        .v_active_lines = 576,
        .sync_polarity = vneg_hneg
    },
    //576p 720x576p50
    {
        .pixel_format = dvi_4_rgb332,
        .scale_x = 3,
        .scale_y = 2,
        .offset_x = 6,
        .offset_y = -50,
        .hstx_div = 2,
        .h_front_porch = 12,
        .h_sync_width = 64,
        .h_back_porch = 68,
        .h_active_pixels = 720,
        .v_front_porch = 5,
        .v_sync_width = 5,
        .v_back_porch = 39,
        .v_active_lines = 576,
        .sync_polarity = vneg_hneg
    },
    //480p 720x480p60
    {
        .pixel_format = dvi_4_rgb332,
        .scale_x = 3,
        .scale_y = 2,
        .offset_x = 6,
        .offset_y = 6,
        .hstx_div = 2,
        .h_front_porch = 16,
        .h_sync_width = 62,
        .h_back_porch = 60+10,
        .h_active_pixels = 720,
        .v_front_porch = 9,
        .v_sync_width = 6,
        .v_back_porch = 30+4,
        .v_active_lines = 480,
        .sync_polarity = vneg_hneg
    },
    //480p 720x480p61
    {
        .pixel_format = dvi_4_rgb332,
        .scale_x = 3,
        .scale_y = 2,
        .offset_x = 6,
        .offset_y = 6,
        .hstx_div = 2,
        .h_front_porch = 16,
        .h_sync_width = 62,
        .h_back_porch = 60,
        .h_active_pixels = 720,
        .v_front_porch = 9,
        .v_sync_width = 6,
        .v_back_porch = 30,
        .v_active_lines = 480,
        .sync_polarity = vneg_hneg
    },
};

void ula_dvi_init(void){
    uint8_t dvi_mode = cfg_get_dvi();
    if(dvi_mode > count_of(ula_dvi_modes))
        dvi_mode = 0;
    dvi_set_modeline(&ula_dvi_modes[dvi_mode]);
}


void ula_print_dvi_modes(void){
    uint8_t sel = cfg_get_dvi();
    for(int i=0; i < count_of(ula_dvi_modes); i++){
        printf("%c %2d - ", (sel == i ? '*' : ' '), i);
        dvi_print_modeline(&ula_dvi_modes[i]);
    }
}