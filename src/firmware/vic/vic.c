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
#include "sys/mem.h"
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


void vic_pio_init(void) {
    //Make PHI PIN possible to also sample as input
    gpio_init(PHI_PIN);
    gpio_set_input_enabled(PHI_PIN, true);

    // Set up VIC PIO.
    pio_set_gpio_base(VIC_PIO, VIC_PIN_OFFS);
    // TODO: We might add the second output clock in the future.
    pio_gpio_init(VIC_PIO, VIC_PIN_BASE);
    gpio_set_drive_strength(VIC_PIN_BASE, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(VIC_PIO, VIC_SM, VIC_PIN_BASE, 1, true);
    uint offset;
    pio_sm_config config;
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
            offset = pio_add_program(VIC_PIO, &clkgen_ntsc_program);
            config = clkgen_ntsc_program_get_default_config(offset);
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
            offset = pio_add_program(VIC_PIO, &clkgen_pal_program);
            config = clkgen_pal_program_get_default_config(offset);
            break;
        }
    sm_config_set_sideset_pin_base(&config, VIC_PIN_BASE);
    pio_sm_init(VIC_PIO, VIC_SM, offset, &config);
    pio_sm_set_enabled(VIC_PIO, VIC_SM, true);   
    printf("VIC init done\n");
}

void vic_memory_init() {
    // Load the VIC 20 Character ROM into internal PIVIC RAM.
    memcpy((void*)(&xram[ADDR_UPPERCASE_GLYPHS_CHRSET]), (void*)vic_char_rom, sizeof(vic_char_rom));

    // Set up hard coded control registers for now (from default PAL VIC).
    //xram[0x1000] = 0x0C;    // Screen Origin X = 12 (PAL)
    xram[0x1000] = 0x05;    // Screen Origin X = 5 (NTSC)
    //xram[0x1001] = 0x26;    // Screen Origin Y = 38 (PAL)
    xram[0x1001] = 0x19;    // Screen Origin Y = 25 (NTSC)
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
    if(cfg_get_splash())
        vic_memory_init();
    switch(cfg_get_mode()){
        case(VIC_MODE_PAL):
            multicore_launch_core1(vic_core1_loop_pal);
            break;
        case(VIC_MODE_NTSC):
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