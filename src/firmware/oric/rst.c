/*
* Copyright (c) 2025 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "oric/rst.h"
#include "sys/mem.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <string.h>
#include <stdio.h>

/* Oric reset handling 
   The Oric has a very short reset period
   This attempts to get the 6502 into a good reset state as early as possible
   Driving zero on the data bus, while cycling the clock two times
   DIR set to output, nROMSEL and nIO kept high (not asserted)
*/
#define RST_EARLY_PINS_MASK ( (1u << PHI_PIN) | (0xFF << DATA_PIN_BASE) | (1u << DIR_PIN) | (1u << NROMSEL_PIN) | (1u << NIO_PIN) )
#define RST_EARLY_PINS_VALUE ( (1u << NROMSEL_PIN) | (1u << NIO_PIN) )

void rst_init(void){
    gpio_init_mask( RST_EARLY_PINS_MASK );     //Enable PHI and DATA GPIO for SIO
    gpio_set_dir_out_masked( RST_EARLY_PINS_MASK );
    gpio_put_masked( RST_EARLY_PINS_MASK, RST_EARLY_PINS_VALUE);
    sleep_us(1);
    gpio_put(PHI_PIN, true);
    sleep_us(1);
    gpio_put(PHI_PIN, false);
    sleep_us(1);
    gpio_put(PHI_PIN, true);
    sleep_us(1);
    gpio_put(PHI_PIN, false);

    //Prepare emulated memory with jmp ($FFFC) for when ula_init() is ready
    // xram[0] = 0x6C;
    // xram[1] = 0xFC;
    // xram[2] = 0xFF;
    xram[0x0] = 0xB8;    //CLV
    xram[0x1] = 0xA9;    //STA #AA
    xram[0x2] = 0xBB;
    xram[0x3] = 0x8D;    //STA $0100
    xram[0x4] = 0x00;
    xram[0x5] = 0x01;   
    xram[0x6] = 0x50;    //BVC -5
    xram[0x7] = 0xFB;
    xram[0xFFFE] = 0x00;
    xram[0xFFFF] = 0x00;
}