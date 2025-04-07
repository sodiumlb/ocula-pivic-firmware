/*
* Copyright (c) 2025 dreamsel
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
//#include "sys/ria.h"
#include "vic/vic.h"
#include "sys/mem.h"
#include "vic.pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>

void core1_entry(void) {
    // Set up VIC PIO.
    pio_set_gpio_base(VIC_PIO, VIC_PIN_BANK);
    // TODO: We might add the second output clock in the future.
    pio_gpio_init(VIC_PIO, VIC_PIN_BASE);
    // TODO: Check if the drive strength is appropriate for the clock.
    gpio_set_drive_strength(VIC_PIN_BASE, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(VIC_PIO, VIC_SM, VIC_PIN_BASE, 1, true);
    uint offset = pio_add_program(VIC_PIO, &clkgen_program);
    pio_sm_config config = clkgen_program_get_default_config(offset);
    sm_config_set_out_pins(&config, VIC_PIN_BASE, 1);
    pio_sm_init(VIC_PIO, VIC_SM, offset, &config);
    pio_sm_set_enabled(VIC_PIO, VIC_SM, true);   
    printf("VIC init done\n");

    while (1) {
        // Poll for PIO IRQ 0. This is the rising edge of F1.
        while (!pio_interrupt_get(VIC_PIO, 0)) {
            tight_loop_contents();
        }



    }
}

void vic_init(void) {
    multicore_launch_core1(core1_entry);
}

void vic_task(void) {
    // TODO: This is where core0 would process something from core1, if required.
}
