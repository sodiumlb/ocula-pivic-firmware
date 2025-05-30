/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "vic/pot.h"
#include "vic/vic.h"
#include "sys/cfg.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdbool.h>
#include <stdio.h>

uint16_t pot_x_counter;

void pot_init(void){
    gpio_init(POTX_PIN);
    gpio_set_input_enabled(POTX_PIN, true);
    gpio_put(POTX_PIN, false);                  //Set latent output value low
    gpio_set_function(POTX_PIN, GPIO_FUNC_PWM);
    //gpio_set_inover(POTX_PIN, GPIO_OVERRIDE_INVERT);
    gpio_set_pulls(POTX_PIN, false, false);

    pwm_config config;

    config = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&config, PWM_DIV_B_HIGH);     //Count when high

    //PWM counter is running at 4x the Phi clock rate as
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
            pwm_config_set_clkdiv_int(&config, 77);         //Assuming 315MHz 
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
            pwm_config_set_clkdiv_int(&config, 72);         //Assuming 319.2MHz 
        break;
        default:
            printf("POT mode %d not supported\n", cfg_get_mode());
    }
    pwm_init(POTX_PWM_SLICE, &config, true);
    pwm_set_enabled(POTX_PWM_SLICE, true);
}

void pot_task(void){
    static absolute_time_t pot_timer = 0;
    static bool do_sample = false;
    static absolute_time_t print_timer = 0;
    if(absolute_time_diff_us(get_absolute_time(), pot_timer) < 0){
        if(do_sample){
            pot_x_counter = pwm_get_counter(POTX_PWM_SLICE);
            //Reset external RC circuit
            gpio_set_function(POTX_PIN, GPIO_FUNC_SIO);
            gpio_put(POTX_PIN, false);
            gpio_set_dir(POTX_PIN, true);
            do_sample = false;
            pot_timer = delayed_by_us(get_absolute_time(), 50);
        }else{
            //Enable external RC circuit for taking new sample
            pwm_set_counter(POTX_PWM_SLICE, 0);
            gpio_set_dir(POTX_PIN, false);
            gpio_set_function(POTX_PIN, GPIO_FUNC_PWM);
            do_sample = true;
            pot_timer = delayed_by_us(get_absolute_time(), 256);
        }
    }
    if(absolute_time_diff_us(get_absolute_time(), print_timer) < 0){
        printf("POTX %d\n", pot_x_counter);
        print_timer = delayed_by_ms(get_absolute_time(), 500);        
    }
}
 
void pot_print_status(void){
    printf("POTX %d\n", pot_x_counter);
}
