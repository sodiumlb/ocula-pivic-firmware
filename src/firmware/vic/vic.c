/*
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "vic/vic.h"
#include "vic/vic_ntsc.h"
#include "vic/vic_pal.h"
#include "vic/char_rom.h"
#include "sys/cfg.h"
#include "sys/dvi.h"
#include "sys/dvi.h"
#include "sys/mem.h"
#include "sys/rev.h"
#include "vic.pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>

// These are the actual VIC 20 memory addresses, but note that the VIC chip sees memory a bit differently.
// $8000-$83FF: 1 KB uppercase/glyphs
// $8400-$87FF: 1 KB uppercase/lowercase
// $8800-$8BFF: 1 KB inverse uppercase/glyphs
// $8C00-$8FFF: 1 KB inverse uppercase/lowercase
// From the VIC chip's perspective, the following are the corresponding addresses:
#define ADDR_UPPERCASE_GLYPHS_CHRSET            0x0000
#define ADDR_UPPERCASE_LOWERCASE_CHRSET         0x0400
#define ADDR_INVERSE_UPPERCASE_GLYPHS_CHRSET    0x0800
#define ADDR_INVERSE_UPPERCASE_LOWERCASE_CHRSET 0x0C00

// These are the 'standard' screen memory locations in the VIC 20 memory map, but in practice the 
// programmer can set screen memory to several other locations.
#define ADDR_UNEXPANDED_SCR  0x3E00    // Equivalent to $1E00 in the VIC 20.
#define ADDR_8KPLUS_EXP_SCR  0x3000    // Equivalent to $1000 in the VIC 20.

// Colour RAM addresses.
#define ADDR_COLOUR_RAM      0x1600    // Equivalent to $9600 in the VIC 20
#define ADDR_ALT_COLOUR_RAM  0x1400    // Equivalent to $9400 in the VIC 20

volatile uint32_t overruns = 0;

dvi_modeline_t dvi_pal_modes[] = {
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

dvi_modeline_t dvi_ntsc_modes[] = {
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

void vic_pio_init(void) {
    //Make PHI2 PIN possible to also sample as input
    uint phi2_pin;
    if(rev_get() == REV_1_1){
        phi2_pin = VIC_PHI2_PIN_1_1;
    }else{
        phi2_pin = VIC_PHI2_PIN_1_2;
    }
    gpio_init(phi2_pin);
    gpio_set_input_enabled(phi2_pin, true);

    // Set up VIC PIO.
    pio_set_gpio_base(VIC_PIO, VIC_PIN_OFFS);
    // TODO: We might add the second output clock in the future.
    pio_gpio_init(VIC_PIO, phi2_pin);
    gpio_set_drive_strength(phi2_pin, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(VIC_PIO, VIC_SM, phi2_pin, 1, true);
    uint offset;
    pio_sm_config config;
    uint16_t dot_div;
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
        case(VIC_MODE_NTSC_SVIDEO):
        case(VIC_MODE_TEST_NTSC_SVIDEO):
            offset = pio_add_program(VIC_PIO, &clkgen_ntsc_program);
            config = clkgen_ntsc_program_get_default_config(offset);
            dot_div = 77;
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
        case(VIC_MODE_PAL_SVIDEO):
        case(VIC_MODE_TEST_PAL_SVIDEO):
            offset = pio_add_program(VIC_PIO, &clkgen_pal_program);
            config = clkgen_pal_program_get_default_config(offset);
            dot_div = 72;
            break;
        }
    sm_config_set_sideset_pin_base(&config, phi2_pin);
    pio_sm_init(VIC_PIO, VIC_SM, offset, &config);
    offset = pio_add_program(VIC_DOTCLK_PIO, &clkgen_dot_program);
    config = clkgen_dot_program_get_default_config(offset);
    sm_config_set_clkdiv_int_frac(&config, dot_div, 0);
    pio_sm_init(VIC_DOTCLK_PIO, VIC_DOTCLK_SM, offset, &config);
    pio_set_sm_mask_enabled(VIC_PIO, (1u << VIC_SM) | (1u << VIC_DOTCLK_SM), true);
    printf("VIC init done\n");
}

void vic_memory_init() {
    // Load the VIC 20 Character ROM into internal PIVIC RAM.
    memcpy((void*)(&xram[ADDR_UPPERCASE_GLYPHS_CHRSET]), (void*)vic_char_rom, sizeof(vic_char_rom));
    // Clear VIC CRs for deterministic loop start-up
    memset((void*)(&xram[0x1000]), 0, 16); 
}

void vic_splash_init() {
    // Set up hard coded control registers for now (from default PAL VIC).
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_NTSC_SVIDEO):
            xram[0x1000] = 0x05;    // Screen Origin X = 5 (NTSC)
            xram[0x1001] = 0x19;    // Screen Origin Y = 25 (NTSC)
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_PAL_SVIDEO):
        default:
            xram[0x1000] = 0x0C;    // Screen Origin X = 12 (PAL)
            xram[0x1001] = 0x26;    // Screen Origin Y = 38 (PAL)
            break;
    }
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_NTSC_SVIDEO):
            xram[0x1000] = 0x05;    // Screen Origin X = 5 (NTSC)
            xram[0x1001] = 0x19;    // Screen Origin Y = 25 (NTSC)
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_PAL_SVIDEO):
        default:
            xram[0x1000] = 0x0C;    // Screen Origin X = 12 (PAL)
            xram[0x1001] = 0x26;    // Screen Origin Y = 38 (PAL)
            break;
    }
    xram[0x1002] = 0x96;    // Number of Columns = 22 (bits 0-6) Video Mem Start (bit 7)
    xram[0x1003] = 0x2E;    // Number of Rows = 23 (bits 1-6)
    xram[0x1005] = 0xF0;    // Video Mem Start = 0x3E00 (bits 4-7), Char Mem Start = 0x0000 (bits 0-3)
    xram[0x100e] = 0;       // Black auxiliary colour (bits 4-7)
    xram[0x100f] = 0x1B;    // White background (bits 4-7), Cyan border (bits 0-2), Reverse OFF (bit 3)

    // Set up a test page for the screen memory.
    memset((void*)&xram[ADDR_UNEXPANDED_SCR], 0x20, 1024);

    // First row - Reserved for name etc.
    xram[ADDR_UNEXPANDED_SCR + 0] = 16;  // P
    xram[ADDR_UNEXPANDED_SCR + 1] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 2] = 22;  // V
    xram[ADDR_UNEXPANDED_SCR + 3] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 4] = 3;   // C
    xram[ADDR_COLOUR_RAM + 0] = 2;
    xram[ADDR_COLOUR_RAM + 1] = 3;
    xram[ADDR_COLOUR_RAM + 2] = 4;
    xram[ADDR_COLOUR_RAM + 3] = 5;
    xram[ADDR_COLOUR_RAM + 4] = 6;

    // Add some coloured blocks to the end of the first line.
    int i;
    for (i=6; i<14; i++) {
        xram[ADDR_UNEXPANDED_SCR + i] = 0x66;
        xram[ADDR_UNEXPANDED_SCR + i + 8] = 0xA0;
        xram[ADDR_COLOUR_RAM + i] = (i - 6);
        xram[ADDR_COLOUR_RAM + i + 8] = (i - 6);
    }

    // Second row onwards - Test characters
    for (i=0; i<256; i++) {
        xram[ADDR_UNEXPANDED_SCR + i + 22] = (i & 0xFF);
        xram[ADDR_COLOUR_RAM + i + 22] = (i & 0x07);
    }
    for (i=256; i<484; i++) {
        xram[ADDR_UNEXPANDED_SCR + i + 22] = (i & 0xFF);
        xram[ADDR_COLOUR_RAM + i + 22] = ((i & 0x07) | 0x08);
    }

    // Last line.
    xram[ADDR_UNEXPANDED_SCR + 484 + 0] = 16;  // P
    xram[ADDR_UNEXPANDED_SCR + 484 + 1] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 484 + 2] = 22;  // V
    xram[ADDR_UNEXPANDED_SCR + 484 + 3] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 484 + 4] = 3;   // C
    xram[ADDR_COLOUR_RAM + 484 + 0] = 2;
    xram[ADDR_COLOUR_RAM + 484 + 1] = 3;
    xram[ADDR_COLOUR_RAM + 484 + 2] = 4;
    xram[ADDR_COLOUR_RAM + 484 + 3] = 5;
    xram[ADDR_COLOUR_RAM + 484 + 4] = 6;
}

void vic_init(void) {
    // Initialisation.
    vic_pio_init();
    vic_memory_init();
    uint8_t dvi_mode = cfg_get_dvi();
    if(cfg_get_splash())
        vic_splash_init();
    switch(cfg_get_mode()){
        case(VIC_MODE_PAL):
        case(VIC_MODE_PAL_SVIDEO):
            if(dvi_mode > count_of(dvi_pal_modes))
                dvi_mode = 0;
            dvi_set_modeline(&dvi_pal_modes[dvi_mode]);
            multicore_launch_core1(vic_core1_loop_pal);
            break;
        case(VIC_MODE_NTSC):
        case(VIC_MODE_NTSC_SVIDEO):
            if(dvi_mode > count_of(dvi_ntsc_modes))
                dvi_mode = 0;
            dvi_set_modeline(&dvi_ntsc_modes[dvi_mode]);
            multicore_launch_core1(vic_core1_loop_ntsc);
            break;
        default:    //Ignore test modes
            break;
    }
}

void vic_task(void) {
    if (overruns > 0) {
        printf("X.");
        overruns = 0;
    }
}

void vic_print_status(void){
    printf("VIC registers\n");
        printf(" CR0 %02x %d X-Orig %s\n", vic_cr0, vic_cr0 & 0x7F, (vic_cr0 & 0x80 ? "(intl)" : "" ));
        printf(" CR1 %02x %d Y-Orig\n", vic_cr1, vic_cr1);
        printf(" CR2 %02x %d Columns\n", vic_cr2, vic_cr2 & 0x7F);
        printf(" CR3 %02x %d Rows D:%d\n", vic_cr3, (vic_cr3 >> 1) & 0x3F, vic_cr3 & 1u);
        printf(" CR4 %02x %d Raster\n", vic_cr4, (vic_cr4 << 1) | (vic_cr3 >> 7));
        printf(" CR5 %02x BV:%04x BC:%04x\n", vic_cr5, (vic_cr5 & 0xF0) << (10-4), (vic_cr5 & 0x0F) << 10);
        printf(" CR6 %02x %d LP X\n", vic_cr6, vic_cr6);
        printf(" CR7 %02x %d LP Y\n", vic_cr7, vic_cr7);
        printf(" CR8 %02x %d POT X\n", vic_cr8, vic_cr8);
        printf(" CR9 %02x %d POT Y\n", vic_cr9, vic_cr9);
        printf(" CRA %02x %d V1 E:%d\n", vic_cra, vic_cra & 0x7F, (vic_cra >> 7));
        printf(" CRB %02x %d V2 E:%d\n", vic_crb, vic_crb & 0x7F, (vic_crb >> 7));
        printf(" CRC %02x %d V3 E:%d\n", vic_crc, vic_crc & 0x7F, (vic_crc >> 7));
        printf(" CRD %02x %d No E:%d\n", vic_crd, vic_crd & 0x7F, (vic_crd >> 7));
        printf(" CRE %02x %d Vol CA:%d\n", vic_cre, vic_cre & 0x0F, (vic_cre >> 4));
        printf(" CRF %02x CB:%d R:%d CE:%d\n", vic_crf, (vic_crf >> 4), (vic_crf >> 3) & 1u, (vic_crf & 0x7));
}

void vic_print_dvi_modes(void){
    dvi_modeline_t *modes;
    uint count;
    uint8_t sel = cfg_get_dvi();
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
        case(VIC_MODE_NTSC_SVIDEO):
        case(VIC_MODE_TEST_NTSC_SVIDEO):
            modes = dvi_ntsc_modes;
            count = count_of(dvi_ntsc_modes);
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
        case(VIC_MODE_PAL_SVIDEO):
        case(VIC_MODE_TEST_PAL_SVIDEO):
            modes = dvi_pal_modes;
            count = count_of(dvi_pal_modes);
            break;
        default:
            count = 0;
    }
    for(int i=0; i < count; i++){
        printf("%c %2d - ", (sel == i ? '*' : ' '), i);
        dvi_print_modeline(&modes[i]);
    }
}