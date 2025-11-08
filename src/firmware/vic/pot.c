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
#include "sys/rev.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdbool.h>
#include <stdio.h>

uint16_t pot_x_counter;
uint16_t pot_y_counter;
uint potx_pin, poty_pin, potx_pwm_slice, poty_pwm_slice;

void pot_init(void){
    if(rev_get() == REV_1_1){
        potx_pin = POTX_PIN_1_1;
        poty_pin = POTY_PIN_1_1;
        potx_pwm_slice = POTX_PWM_SLICE_1_1;
        poty_pwm_slice = POTY_PWM_SLICE_1_1;
    }else{
        potx_pin = POTX_PIN_1_2;
        poty_pin = POTY_PIN_1_2;
        potx_pwm_slice = POTX_PWM_SLICE_1_2;
        poty_pwm_slice = POTY_PWM_SLICE_1_2;
    }
    gpio_init(potx_pin);
    gpio_init(poty_pin);
    gpio_set_input_enabled(potx_pin, true);
    gpio_set_input_enabled(poty_pin, true);
    gpio_put(potx_pin, false);                  //Set latent output value low
    gpio_put(poty_pin, false);                  //Set latent output value low
    gpio_set_function(potx_pin, GPIO_FUNC_PWM);
    gpio_set_function(poty_pin, GPIO_FUNC_PWM);

    //Turning off hysteresis seems to calm the E9 jitter some 
    gpio_set_input_hysteresis_enabled(potx_pin, false);
    gpio_set_input_hysteresis_enabled(poty_pin, false);
    gpio_set_pulls(potx_pin, false, false);
    gpio_set_pulls(poty_pin, false, false);

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
            printf("POT video mode %d not supported\n", cfg_get_mode());
    }
    pwm_init(potx_pwm_slice, &config, true);
    pwm_init(poty_pwm_slice, &config, true);
    pwm_set_enabled(potx_pwm_slice, true);
    pwm_set_enabled(poty_pwm_slice, true);
}

void pot_task(void){
    static absolute_time_t pot_timer = 0;
    static bool do_sample = false;
    static absolute_time_t print_timer = 0;
    if(absolute_time_diff_us(get_absolute_time(), pot_timer) < 0){
        if(do_sample){
            pot_x_counter = pwm_get_counter(potx_pwm_slice);
            pot_y_counter = pwm_get_counter(poty_pwm_slice);

            switch(cfg_get_pot()){
                //Paddle calibration CMB original 470kOhm paddles
                // 250 is ca low point for the PWM counter
                // 1073 is ca high point the PWM counter calculated as the reverse of the (<<1 / 7) range
                // 256us sample time
                case POT_MODE_CMB:
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
                    vic_cr8 = 255 - (((pot_x_counter - 250)<<1) / 7);
                    vic_cr9 = 255 - (((pot_y_counter - 250)<<1) / 7);
                    break;
                //Paddle calibration Atari original 1MOhm paddles
                // 10 is ca low point for th PWM counter
                // 2050 is ca high point for the PWM counter calcuated as the reverse of ( / 8) range
                // 512us sample time
                case POT_MODE_ATARI:
                    if(pot_x_counter > 2050){
                        pot_x_counter = 2050;
                    }else if(pot_x_counter < 10){
                        pot_x_counter = 10;
                    }
                    if(pot_y_counter > 2050){
                        pot_y_counter = 2050;
                    }else if(pot_y_counter < 10){
                        pot_y_counter = 10;
                    }
                    vic_cr8 = 255 - ((pot_x_counter-10) / 8);
                    vic_cr9 = 255 - ((pot_y_counter-10) / 8);
                    break;
                default:
                    printf("POT pot mode %d not supported\n", cfg_get_pot());
            }

            //Reset external RC circuit
            gpio_set_function(potx_pin, GPIO_FUNC_SIO);
            gpio_set_function(poty_pin, GPIO_FUNC_SIO);
            gpio_put(potx_pin, false);
            gpio_put(poty_pin, false);
            gpio_set_dir(potx_pin, true);
            gpio_set_dir(poty_pin, true);
            do_sample = false;
            pot_timer = delayed_by_us(get_absolute_time(), 50);
        }else{
            //Enable external RC circuit for taking new sample
            pwm_set_counter(potx_pwm_slice, 0);
            pwm_set_counter(poty_pwm_slice, 0);
            gpio_set_dir(potx_pin, false);
            gpio_set_dir(poty_pin, false);
            gpio_set_function(potx_pin, GPIO_FUNC_PWM);
            gpio_set_function(poty_pin, GPIO_FUNC_PWM);
            do_sample = true;
            uint sample_time_us;
            switch(cfg_get_pot()){
                case POT_MODE_ATARI:
                    sample_time_us = 512;
                    break;
                case POT_MODE_CMB:
                default:
                    sample_time_us = 256;
                    break;
            }
            pot_timer = delayed_by_us(get_absolute_time(), sample_time_us);
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
