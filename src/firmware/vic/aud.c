/*
 * Copyright (c) 2025 dreamseal
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "vic/aud.h"
#include "sys/mem.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdbool.h>
#include <stdio.h>

//Audio shift registers
uint8_t aud_sr[4];
int8_t aud_val[4];
uint16_t aud_lfsr;

volatile aud_union_t aud_counters;
volatile aud_union_t aud_update;
aud_union_t aud_ticks;

//TODO Unify with/import vic/vic.c/h definitions
#define VIC_CRA xram[0x100A]
#define VIC_CRB xram[0x100B]
#define VIC_CRC xram[0x100C]
#define VIC_CRD xram[0x100D]
#define VIC_CRE xram[0x100E]

void aud_step_voice(uint8_t idx, uint8_t reg){
    uint8_t prev = aud_sr[idx];
    uint8_t enable = reg >> 7;  //Keep enable bit in LSB
    uint8_t next_bit = (~prev >> 7) & enable;    //Shift register MSB & register enable bit
    aud_sr[idx] = (prev << 1) | next_bit;
    aud_val[idx] = (enable ? (next_bit << 1) - 1 : 0);  //High=1 ,half=0, low=-1
}

void aud_step_noise(uint8_t reg){
    uint8_t prev = aud_sr[3];
    uint8_t enable = reg >> 7;  //Keep enable bit in LSB
    uint16_t tmp = aud_lfsr;
    //XOR together LFSR bits 15,14,12,3 to get next LSB, but set to '1' if not channel enabled 
    uint8_t next_lfsr_bit = ((((tmp >> 3) ^ (tmp >> 12) ^ (tmp >> 14) ^ (tmp >> 15)) | ~enable) & 1u);
    aud_lfsr = (tmp << 1) | next_lfsr_bit;
    //LFSR bit clocks the waveform shift register
    if(next_lfsr_bit != 0){
        uint8_t next_sr_bit = (~prev >> 7) & enable;    //Shift register MSB & register enable bit
        aud_sr[3] = (prev << 1) | next_sr_bit;
        aud_val[3] = (next_sr_bit << 1) - 1;     //High=1, low=-1
    }
}

void aud_update_pwm(uint8_t vol_reg){
    int16_t pwm_value_signed = ( aud_val[0] + aud_val[1] + aud_val[2] + aud_val[3] ) * (vol_reg & 0x0F);
    pwm_set_chan_level(AUDIO_PWM_SLICE, AUDIO_PWM_CH, (uint8_t)(pwm_value_signed + 64));
}

// Per CPU tick function to maintain audio counters
// TODO Check trigger values is it really 0x7F and not 0x80?
// TODO Check the counter reloading values - should it be reg+1 on load?
// These two questions are probably linked
void aud_tick(void){
    static uint8_t tick=0;

    if((tick % 4) == 0){
        if(++aud_counters.ch[2] == 0x80){
            aud_update.ch[2] = true;
            aud_counters.ch[2] = VIC_CRC & 0x7F; 
        }
        if((tick % 8) == 0){
            if(++aud_counters.ch[1] == 0x80){
                aud_update.ch[1] = true;
                aud_counters.ch[1] = VIC_CRB & 0x7F; 
            }
            if((tick % 16) == 0){
                if(++aud_counters.ch[0] == 0x80){
                    aud_update.ch[0] = true;
                    aud_counters.ch[0] = VIC_CRA & 0x7F; 
                }
                if((tick % 32) == 0){
                    if(++aud_counters.ch[3] == 0x80){
                        aud_update.ch[3] = true;
                        aud_counters.ch[3] = VIC_CRD & 0x7F; 
                    }
                }
            }
        }
    }
    tick++;
}


void aud_init(void){
    gpio_set_function(AUDIO_PWM_PIN, GPIO_FUNC_PWM);

    pwm_config config;

    config = pwm_get_default_config();
    pwm_config_set_wrap(&config, (1u << 7));   //Make PWM precision 7-bit
    pwm_init(AUDIO_PWM_SLICE, &config, true);
    pwm_set_chan_level(AUDIO_PWM_SLICE, AUDIO_PWM_CH, 0);
    pwm_set_enabled(AUDIO_PWM_SLICE, true);

    // Init the three voices to zero
    for(int i=0; i < 3; i++){
        aud_sr[i] = 0;
        aud_val[i] = 0;
    }
    // Init noise separately
    aud_sr[3] = 0x00;
    aud_val[3] = 0x01;
    aud_lfsr = 0xFFFF;

    // VIC_CRA = 0x80 | 0;
    // VIC_CRB = 0x80 | 0;
    // VIC_CRC = 0x80 | 0;
    VIC_CRD = 0x80 | 0;
    VIC_CRE = 0xF;
}

//TODO Is calculating and updating audio in normal task good enough?
void aud_task(void){
    if(aud_update.ch[0]){
        aud_step_voice(0, VIC_CRA);
        aud_update.ch[0] = false;
    }
    if(aud_update.ch[1]){
        aud_step_voice(1, VIC_CRB);
        aud_update.ch[1] = false;
    }
    if(aud_update.ch[2]){
        aud_step_voice(2, VIC_CRC);
        aud_update.ch[2] = false;
    }
    if(aud_update.ch[3]){
        aud_step_noise(VIC_CRD);
        aud_update.ch[3] = false;
    }
    aud_update_pwm(VIC_CRE);
}
