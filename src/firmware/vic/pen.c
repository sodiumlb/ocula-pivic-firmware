/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "vic/pen.h"
#include "vic/vic.h"
#include "sys/cfg.h"
#include "sys/mem.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include <stdbool.h>
#include <stdio.h>

volatile uint16_t *pen_xy;
volatile uint32_t *pen_dma_trans_reg;
int data_chan;

void pen_init(void){
    gpio_init(PEN_PIN);
    gpio_set_input_enabled(PEN_PIN, true);
    gpio_set_function(PEN_PIN, GPIO_FUNC_PWM);

    gpio_set_pulls(PEN_PIN, true, false);

    pwm_config config;

    config = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&config, PWM_DIV_B_FALLING);     //Trigger on falling edge
    pwm_config_set_wrap(&config, 0);                            //Wrap and trigger every time
    pwm_init(PEN_PWM_SLICE, &config, true);

    data_chan = dma_claim_unused_channel(true);
    pen_dma_trans_reg = &dma_channel_hw_addr(data_chan)->al1_transfer_count_trig;

    // DMA move the latest VIC loop horizontal and vertical counter values 
    // to CR6 and CR7 when PWM triggers
    dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    channel_config_set_high_priority(&data_dma, true);
    channel_config_set_dreq(&data_dma, pwm_get_dreq(PEN_PWM_SLICE));
    channel_config_set_read_increment(&data_dma, false);
    channel_config_set_write_increment(&data_dma, false);
    channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_16);
    pen_xy = (uint16_t*)&pwm_hw->slice[PEN_COUNTER_PWM_SLICE].ctr;
    dma_channel_configure(
        data_chan,
        &data_dma,
        &xram[0x1006],                    // dst VIC CR6 & CR7
        pen_xy,                           // src Latest 
        0xFF000000,                       // Single transfer
        false);

    //Using second PWM slice as position counter running at 1/2 dot clock speed
    config = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&config, PWM_DIV_FREE_RUNNING);
    pwm_config_set_wrap(&config, 0xFFFF);
    //Run counter 
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
            pwm_config_set_clkdiv_int(&config, 154);         //Assuming 315MHz 
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
            pwm_config_set_clkdiv_int(&config, 144);         //Assuming 319.2MHz 
        break;
        default:
            printf("PEN mode %d not supported\n", cfg_get_mode());
    }
    pwm_init(PEN_COUNTER_PWM_SLICE, &config, true);
    *pen_xy = 0xAA55;
}
 
void pen_print_status(void){
    printf("PEN status (%d,%d) %04x\n", xram[0x1006], xram[0x1007], *pen_xy);
}
