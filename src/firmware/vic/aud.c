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
#include "pico/multicore.h"
#include <stdbool.h>
#include <stdio.h>

//Audio shift registers
uint8_t aud_sr[4];
int8_t aud_val[4];
uint16_t aud_lfsr;

aud_union_t aud_counters;
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

    //Init tick system
    aud_ticks.all = 0;
    multicore_doorbell_claim(0, 0b11);
    multicore_doorbell_claim(1, 0b11);
    multicore_doorbell_claim(2, 0b11);
    multicore_doorbell_claim(3, 0b11);

    VIC_CRA = 0xED;     //Startup tone C
    VIC_CRB = 0x00;
    VIC_CRC = 0x00;
    VIC_CRD = 0x00;
    VIC_CRE = 0xF;
}

//TODO Is calculating and updating audio in normal task good enough?
void aud_task(void){
    if(multicore_doorbell_is_set_current_core(0)){
        aud_step_voice(0, VIC_CRA);
        multicore_doorbell_clear_current_core(0);
    }
    if(multicore_doorbell_is_set_current_core(1)){
        aud_step_voice(1, VIC_CRB);
        multicore_doorbell_clear_current_core(1);
    }
    if(multicore_doorbell_is_set_current_core(2)){
        aud_step_voice(2, VIC_CRC);
        multicore_doorbell_clear_current_core(2);
    }
    if(multicore_doorbell_is_set_current_core(3)){
        aud_step_noise(VIC_CRD);
        multicore_doorbell_clear_current_core(3);
    }
    aud_update_pwm(VIC_CRE);
}

void aud_print_status(void){
    printf("Aud sr:%02x %02x %02x %02x  val:%02d %02d %02d %02d lfsr:%04x\n", 
            aud_sr[0], aud_sr[1], aud_sr[2], aud_sr[3], 
            aud_val[0], aud_val[1], aud_val[2], aud_val[3],
            aud_lfsr
        );
    printf("    tck:%08x cnt:%08x upd:%01x\n", aud_ticks.all, aud_counters.all, sio_hw->doorbell_in_set);
}