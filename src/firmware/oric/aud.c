/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "oric/aud.h"
#include "emu2149/emu2149.h"
#include "sys/dvi_audio.h"
#include "sys/mem.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <string.h>
#include <stdio.h>

#define VIA_DRA xram[0x0303]
#define VIA_PCR xram[0x030c]
//Assuming only the non-handshake ORA version is used for AY access. Could be wrong
#define VIA_ORA xram[0x030F]

//Static allocation instead of using PSG_new
PSG psg;
volatile audio_sample_t psg_sample;

void aud_init(void){
    PSG_setClock(&psg, 1000000);
    PSG_setClockDivider(&psg, 0);
    PSG_setRate(&psg, DVI_AUDIO_FS);
    PSG_setVolumeMode(&psg, 2); // AY style
    PSG_setQuality(&psg, 0);
    PSG_setMask(&psg, 0x00);
    PSG_reset(&psg);
    dvi_audio_set_sample_source(&psg_sample);
}

void aud_task(void){
    //TODO This is not good buffer filling. Needs rework.
    if(dvi_audio_fs_tick()){
        uint16_t sample = PSG_calc(&psg);
        psg_sample.left = sample;
        psg_sample.right = sample;
    }
}

void aud_tick(void){
    static uint32_t ay_addr;
    //CA2 -> BC1
    //CB2 -> BDIR
    //VIA CA2/CB2: C = 0, E = 1
    //(BDIR,BC1) (1,0)=write (1,1)=latch address
    uint8_t pcr = VIA_PCR; 
    if((pcr & 0xEE) == 0xEE){
        ay_addr = VIA_ORA & VIA_DRA;
    }
    if((pcr & 0xEE) == 0xEC){
        PSG_writeReg(&psg, ay_addr, VIA_ORA & VIA_DRA);
    }
}

void aud_print_status(void){

}