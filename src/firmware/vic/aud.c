/*
 * Copyright (c) 2025 dreamseal
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "vic/aud.h"
#include "vic/aud_splash.h"
#include "sys/cfg.h"
#include "sys/dvi_audio.h"
#include "sys/mem.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include <stdbool.h>
#include <stdio.h>

//Audio shift registers
uint8_t aud_noise_sr;    //Ch 0-2 SR are kept in aud_sr
uint8_t aud_val[4];
uint16_t aud_lfsr;

volatile aud_union_t aud_counters;
volatile aud_union_t aud_ticks;
volatile aud_union_t aud_regs;
volatile aud_union_t aud_sr;

//TODO Unify with/import vic/vic.c/h definitions
#define VIC_CRA xram[0x100A]
#define VIC_CRB xram[0x100B]
#define VIC_CRC xram[0x100C]
#define VIC_CRD xram[0x100D]
#define VIC_CRE xram[0x100E]

void aud_calc_voice(uint8_t idx, uint8_t reg){
    uint8_t enable = reg >> 7;  //Keep enable bit in LSB
    uint8_t next_bit = aud_sr.ch[idx] & 0x01;
    aud_val[idx] = (enable ? (next_bit ? 4 : 0) : 1);  //High=4 ,half=1, low=0
}

void aud_step_noise(uint8_t reg){
    uint8_t prev = aud_noise_sr;
    uint8_t enable = reg >> 7;  //Keep enable bit in LSB
    uint16_t tmp = aud_lfsr;
    //XOR together LFSR bits 15,14,12,3 to get next LSB, but set to '1' if not channel enabled 
    uint8_t next_lfsr_bit = ((((tmp >> 3) ^ (tmp >> 12) ^ (tmp >> 14) ^ (tmp >> 15)) | ~enable) & 1u);
    uint8_t old_lfsr_bit = aud_lfsr & 0x1;
    aud_lfsr = (tmp << 1) | next_lfsr_bit;
    //LFSR bit clocks the waveform shift register
    if(next_lfsr_bit == 1 && old_lfsr_bit == 0){
        uint8_t next_sr_bit = (~prev >> 7) & enable;    //Shift register MSB & register enable bit
        aud_noise_sr = (prev << 1) | next_sr_bit;
        aud_val[3] = (enable ? (next_sr_bit ? 4 : 0) : 1);    //High=3, half=1, low=0. TODO - confirm enable is used to gate the noise channel
    }
}

audio_sample_t pwm_sample;
//volatile audio_sample_t dvi_sample;
#define LPF_ALPHA 15

void aud_update_pwm(void){
    uint8_t vol = VIC_CRE & 0x0F;
    uint16_t sample = (uint16_t)( aud_val[0] + aud_val[1] + aud_val[2] + aud_val[3] ) * vol;
    uint16_t pwm_value = (sample + cfg_get_bias());  //DC offset required for bias of mainboard audio circuit
    pwm_set_chan_level(AUDIO_PWM_SLICE, AUDIO_PWM_CH, pwm_value);    
    // static int16_t lpf_sample;
    // lpf_sample = (LPF_ALPHA * (lpf_sample>>4)) + ((16-LPF_ALPHA) * ((sample<<2)-(vol<<4)));   //Low-pass over 16 values. Boost vol <<2. Subtract DC.
    // //lpf_sample = (lpf_sample - (lpf_sample>>4)) + pwm_value;
    // pwm_sample.left = pwm_sample.right = (int16_t)((lpf_sample));
    pwm_sample.left = pwm_sample.right = ((int16_t)(sample<<6)-(vol<<8));
}

void aud_splash_init(void);
void aud_splash_task(void);

void aud_dvi_audio_fs_cb(void){
    aud_update_pwm();
    aud_splash_task();
}

void aud_init(void){
    gpio_set_function(AUDIO_PWM_PIN, GPIO_FUNC_PWM);

    pwm_config config;

    config = pwm_get_default_config();
    pwm_config_set_wrap(&config, (1u << 8)-2);   //Make PWM precision 8-bit
    pwm_init(AUDIO_PWM_SLICE, &config, true);
    pwm_set_chan_level(AUDIO_PWM_SLICE, AUDIO_PWM_CH, 0);
    pwm_set_enabled(AUDIO_PWM_SLICE, true);

    // Init the three voices to zero
    for(int i=0; i < 3; i++){
        aud_sr.ch[i] = 0;
        aud_val[i] = 0;
    }
    // Init noise separately
    aud_noise_sr = 0x00;
    aud_val[3] = 0x01;
    aud_lfsr = 0xFFFF;

    //Init tick system
    aud_ticks.all = 0;

    //VIC audio registers reset values. TODO: should be done central in vic.c?
    VIC_CRA = 0x00;
    VIC_CRB = 0x00;
    VIC_CRC = 0x00;
    VIC_CRD = 0x00;
    VIC_CRE = 0x8;

    dvi_audio_set_sample_source(&pwm_sample);
    dvi_audio_set_fs_cb(aud_dvi_audio_fs_cb);
    if(cfg_get_splash() == 2){
        aud_splash_init();
    }
}

static int64_t last_sample_time_diff;
static uint32_t lost_samples = 0;
// Raw update values are calculated here, final output values are calculated in the irq driven dvi_audio_fs_cb 
void aud_task(void){
    while(multicore_fifo_rvalid()){
        uint32_t upd = sio_hw->fifo_rd;
        aud_calc_voice(0, aud_regs.ch[0]);
        aud_calc_voice(1, aud_regs.ch[1]);
        aud_calc_voice(2, aud_regs.ch[2]);
        if(upd & 0x08)
            aud_step_noise(upd >> 24);
    }
}

void aud_print_status(void){
    printf("Aud sr:%02x %02x %02x %02x  val:%02d %02d %02d %02d lfsr:%04x\n", 
            aud_sr.ch[0], aud_sr.ch[1], aud_sr.ch[2], aud_noise_sr, 
            aud_val[0], aud_val[1], aud_val[2], aud_val[3],
            aud_lfsr
        );
    printf("    tck:%08x cnt:%08x upd:%01x\n", aud_ticks.all, aud_counters.all, sio_hw->doorbell_in_set);
    printf("    pwm intr:%08x cc:%d top:%d\n", pwm_hw->intr, pwm_hw->slice[AUDIO_PWM_SLICE].cc, pwm_hw->slice[AUDIO_PWM_SLICE].top);
    // printf("    last_sample_periode_us: %lld\n", last_sample_time_diff);
    // printf("    DVI lost samples: %d\n", lost_samples);
}

#define AUD_SPLASH_MAGIC 0x42
#define AUD_SPLASH_BAR_MS 1200
#define AUD_SPLASH_NOTE_MS (AUD_SPLASH_BAR_MS/6)
#define AUD_SPLASH_GAP_MS 50
bool aud_splash_active;
void aud_splash_init(void){
    VIC_CRA = AUD_SPLASH_MAGIC;
    aud_splash_active = true;
}

void aud_splash_task(void){
    static absolute_time_t timer = 0;
    static uint16_t idx = 0;
    static enum { attack, release } state = attack;
    if(aud_splash_active){
        if(absolute_time_diff_us(get_absolute_time(),timer) < 0){
            if(idx >= sizeof(notes))
                idx = 0;
            uint8_t pitch = notes[idx+0];
            uint8_t length = notes[idx+1];
            uint8_t effect = notes[idx+2];
            uint16_t gap = AUD_SPLASH_GAP_MS;
            if(effect)
                gap += AUD_SPLASH_GAP_MS * length; 
            if(pitch)
                pitch |= 0x80;  //Enable
            switch(state){
                default:
                case(attack):
                    VIC_CRC = pitch;
                    timer = delayed_by_ms(get_absolute_time(), (length * AUD_SPLASH_NOTE_MS) - gap);
                    state = release;
                    break;
                case(release):
                    VIC_CRC = 0;
                    timer = delayed_by_ms(get_absolute_time(), gap);
                    idx += 3;
                    state = attack;
                    break;
            }
            if(VIC_CRA != AUD_SPLASH_MAGIC){
                aud_splash_active = false;
            }
        }
    }
}