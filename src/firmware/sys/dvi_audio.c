/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "sys/dvi_audio.h"
#include "pico_hdmi/hstx_packet.h"
#include <stdio.h>
#include <string.h>

//Buffer size must be power of 2 and max 128 
#define AUDIO_BUF_SIZE 32
static audio_sample_t audio_buf[AUDIO_BUF_SIZE];
static uint8_t head_idx = 0;
static uint8_t tail_idx = 0;

//Running own small DI queue
#define DI_BUF_SIZE 8
hstx_data_island_t di_buf[DI_BUF_SIZE];
static uint8_t di_head_idx = 0;
static uint8_t di_tail_idx = 0;

bool dvi_audio_push_sample(uint16_t sample){
    static uint16_t count = 0;
    if((head_idx + 1) % AUDIO_BUF_SIZE == tail_idx){
        return false;
    }
    //TODO scale sample to reasonable 16bit levels
    audio_buf[head_idx].left = sample << 5;
    audio_buf[head_idx].right = sample << 5;
    count += 312;
    head_idx = (head_idx + 1) % AUDIO_BUF_SIZE;
    return true;
}

uint8_t dvi_audio_get_buflen(void){
    return (head_idx - tail_idx) & (AUDIO_BUF_SIZE-1);
}

//Pop just moves the tail index. 
bool dvi_audio_pop_samples(uint8_t count){
    if(dvi_audio_get_buflen() < count){
        return false;
    }
    tail_idx = (tail_idx + count) % AUDIO_BUF_SIZE;
    return true;    
}

bool dvi_audio_di_buf_is_full(void){
    return (di_head_idx + 1) % DI_BUF_SIZE == di_tail_idx;
}

//NULL pointer will skip copy. For use with direct allocation.
bool dvi_audio_push_di(hstx_data_island_t *di){
    if((di_head_idx + 1) % DI_BUF_SIZE == di_tail_idx){
        return false;
    }
    if(di){
        memcpy((void*)&di_buf[di_head_idx], (void*)di, sizeof(hstx_data_island_t));
    }
    di_head_idx = (di_head_idx + 1) % DI_BUF_SIZE;
    return true;
}

bool dvi_audio_pop_di(uint32_t *di){
    if(di_head_idx == di_tail_idx){
        const uint32_t *null_di = hstx_get_null_data_island(false,false);
        for(int i = 0; i < W_DATA_ISLAND; i++){
            di[i] = null_di[i];
        }
        return false;
    }else{
        uint32_t *tail_di = (uint32_t*)&di_buf[di_tail_idx];
        for(int i = 0; i < W_DATA_ISLAND; i++){
            di[i] = tail_di[i];
        }
        di_tail_idx = (di_tail_idx + 1) % DI_BUF_SIZE;
        return true;
    }
}

void dvi_audio_init(void){

}

void dvi_audio_task(void){
    hstx_packet_t packet;
    //hstx_data_island_t* di_ptr;
    static int audio_frame_cnt = 0;
    while(dvi_audio_get_buflen() >= 4 && !dvi_audio_di_buf_is_full()){
        //Assuming working on groups of 4 samples means we won't cross ring buffer end
        audio_frame_cnt = hstx_packet_set_audio_samples(&packet,&audio_buf[tail_idx],4,audio_frame_cnt);
        //Assuming audio DI only in non-synch hblank period. TODO: sync polarity needs to be taking into account
        hstx_encode_data_island(&di_buf[di_head_idx],&packet,false,false);
        dvi_audio_pop_samples(4);
        dvi_audio_push_di(NULL);
    }
}