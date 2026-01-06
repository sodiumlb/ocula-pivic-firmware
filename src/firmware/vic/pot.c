/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "vic/pot.h"
#include "vic/vic.h"
#include "sys/cfg.h"
#include "sys/mem.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdbool.h>
#include <stdio.h>

uint16_t pot_x_counter;
uint16_t pot_y_counter;

void pot_init(void){
    gpio_init(POTX_PIN);
    gpio_init(POTY_PIN);
    gpio_set_input_enabled(POTX_PIN, true);
    gpio_set_input_enabled(POTY_PIN, true);
    gpio_put(POTX_PIN, false);                  //Set latent output value low
    gpio_put(POTY_PIN, false);                  //Set latent output value low
    gpio_set_function(POTX_PIN, GPIO_FUNC_PWM);
    gpio_set_function(POTY_PIN, GPIO_FUNC_PWM);

    //Turning off hysteresis seems to calm the E9 jitter some 
    gpio_set_input_hysteresis_enabled(POTX_PIN, false);
    gpio_set_input_hysteresis_enabled(POTY_PIN, false);
    gpio_set_pulls(POTX_PIN, false, false);
    gpio_set_pulls(POTY_PIN, false, false);

    pwm_config config;

    config = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&config, PWM_DIV_B_HIGH);     //Count when high

    //PWM counter is running at 4x the Phi clock rate as default
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
        case(VIC_MODE_NTSC_SVIDEO):
        case(VIC_MODE_TEST_NTSC_SVIDEO):
            pwm_config_set_clkdiv_int(&config, 77);         //Assuming 315MHz 
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
        case(VIC_MODE_PAL_SVIDEO):
        case(VIC_MODE_TEST_PAL_SVIDEO):
            pwm_config_set_clkdiv_int(&config, 72);         //Assuming 319.2MHz 
        break;
        default:
            printf("POT mode %d not supported\n", cfg_get_mode());
    }
    pwm_init(POTX_PWM_SLICE, &config, true);
    pwm_init(POTY_PWM_SLICE, &config, true);
    pwm_set_enabled(POTX_PWM_SLICE, true);
    pwm_set_enabled(POTY_PWM_SLICE, true);
}

void pot_task(void){
    static absolute_time_t pot_timer = 0;
    static bool do_sample = false;
    static absolute_time_t print_timer = 0;
    if(absolute_time_diff_us(get_absolute_time(), pot_timer) < 0){
        if(do_sample){
            pot_x_counter = pwm_get_counter(POTX_PWM_SLICE);
            pot_y_counter = pwm_get_counter(POTY_PWM_SLICE);

            //Paddle calibration currently static based on VIC original 470kOhm paddles
            //TODO make calibration configurable, or at least add Atari calibration profile
            //500 is ca low point for the PWM counter
            //1073 is ca high point the PWM counter calculated as the reverse of the (<<2 / 9) range
            if(pot_x_counter > 1142){
                pot_x_counter = 1142;
            }else if(pot_x_counter < 250){
                pot_x_counter = 250;
            }
            if(pot_y_counter > 1142){
                pot_y_counter = 1142;
            }else if(pot_y_counter < 250){
                pot_y_counter = 250;
            }
            // 250 low point 
            // 1140 high point
            vic_cr8 = 255 - (((pot_x_counter - 250)<<1) / 7);
            vic_cr9 = 255 - (((pot_y_counter - 250)<<1) / 7);

            //Reset external RC circuit
            gpio_set_function(POTX_PIN, GPIO_FUNC_SIO);
            gpio_set_function(POTY_PIN, GPIO_FUNC_SIO);
            gpio_put(POTX_PIN, false);
            gpio_put(POTY_PIN, false);
            gpio_set_dir(POTX_PIN, true);
            gpio_set_dir(POTY_PIN, true);
            do_sample = false;
            pot_timer = delayed_by_us(get_absolute_time(), 50);
        }else{
            //Enable external RC circuit for taking new sample
            pwm_set_counter(POTX_PWM_SLICE, 0);
            pwm_set_counter(POTY_PWM_SLICE, 0);
            gpio_set_dir(POTX_PIN, false);
            gpio_set_dir(POTY_PIN, false);
            gpio_set_function(POTX_PIN, GPIO_FUNC_PWM);
            gpio_set_function(POTY_PIN, GPIO_FUNC_PWM);
            do_sample = true;
            pot_timer = delayed_by_us(get_absolute_time(), 256);
        }
    }
    // if(absolute_time_diff_us(get_absolute_time(), print_timer) < 0){
    //     printf("POTX %d\n", pot_x_counter);
    //     print_timer = delayed_by_ms(get_absolute_time(), 500);        
    // }
}
 
void pot_print_status(void){
    printf("POTX %d\n", pot_x_counter);
    printf("POTY %d\n", pot_y_counter);
}
