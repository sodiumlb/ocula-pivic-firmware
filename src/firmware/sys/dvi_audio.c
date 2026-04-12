/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "sys/cfg.h"
#include "sys/dvi.h"
#include "sys/dvi_audio.h"
#include "pico_hdmi/hstx_packet.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include <stdio.h>
#include <string.h>

//Buffer size must be power of 2 and max 128 
//Alignment for direct DMA use
#define DVI_AUDIO_BUF_LEN_BITS 7
#define DVI_AUDIO_BUF_LEN (1<<DVI_AUDIO_BUF_LEN_BITS)
#define DVI_AUDIO_BUF_SIZE_BITS (DVI_AUDIO_BUF_LEN_BITS+2)

audio_sample_t audio_buf[DVI_AUDIO_BUF_LEN] 
    __attribute__ ((aligned (sizeof(audio_sample_t)*DVI_AUDIO_BUF_LEN)));

dma_channel_hw_t *dma_sample_chan;
dma_channel_hw_t *dma_di_chan;
int sample_timer;
int dvi_audio_dma_sample_chan_idx;
static uint8_t tail_idx = 0;

bool vsync_polarity;
bool hsync_polarity;

//Running own small DI queue
#define DI_BUF_LEN_BITS 3
#define DI_BUF_LEN (1<<DI_BUF_LEN_BITS)
#define DI_BUF_SIZE_BITS (DI_BUF_LEN_BITS+2)

hstx_data_island_t di_buf[DI_BUF_LEN];
static uint8_t di_head_idx = 0;
static uint8_t di_tail_idx = 0;

static volatile audio_sample_t *source_sample = 0;

bool dvi_audio_buf_is_full(void){
    return (((dma_sample_chan->write_addr >> 2) % (DVI_AUDIO_BUF_LEN)) + 1) % (DVI_AUDIO_BUF_LEN-1) == tail_idx;
}

uint8_t dvi_audio_get_buflen(void){
   return (((dma_sample_chan->write_addr >> 2) % (DVI_AUDIO_BUF_LEN)) - tail_idx) % (DVI_AUDIO_BUF_LEN-1);
}

//Pop just moves the tail index. 
bool dvi_audio_pop_samples(uint8_t count){
    tail_idx = (tail_idx + count) % (DVI_AUDIO_BUF_LEN);
    return true;    
}

bool dvi_audio_di_buf_is_full(void){
    return (di_head_idx + 1) % (DI_BUF_LEN) == di_tail_idx;
}

//NULL pointer will skip copy. For use with direct allocation.
bool dvi_audio_push_di(hstx_data_island_t *di){
    if((di_head_idx + 1) % (DI_BUF_LEN) == di_tail_idx){
        return false;
    }
    if(di){
        memcpy((void*)&di_buf[di_head_idx], (void*)di, sizeof(hstx_data_island_t));
    }
    di_head_idx = (di_head_idx + 1) % (DI_BUF_LEN);
    return true;
}

bool dvi_audio_pop_di(uint32_t *di){
    if(di_head_idx == di_tail_idx){
        const uint32_t *null_di = hstx_get_null_data_island(vsync_polarity,hsync_polarity);
        dma_di_chan->read_addr = (uint32_t)null_di;
        dma_di_chan->al2_write_addr_trig = (uint32_t)di;
        return false;
    }else{
        uint32_t *tail_di = (uint32_t*)&di_buf[di_tail_idx];
        dma_di_chan->read_addr = (uint32_t)tail_di;
        dma_di_chan->al2_write_addr_trig = (uint32_t)di;
        di_tail_idx = (di_tail_idx + 1) % (DI_BUF_LEN);
        return true;
    }
}

void dvi_audio_set_sample_source(volatile audio_sample_t *src_ptr){
    source_sample = src_ptr;
}

bool dvi_audio_enabled = false;

void dvi_audio_init(void){
    if(source_sample == NULL || cfg_get_dvi_audio() == 0)
        return;

    dvi_audio_enabled = true;
    
    int dma_chan_idx = dma_claim_unused_channel(true);
    dvi_audio_dma_sample_chan_idx = dma_chan_idx;
    dma_sample_chan = &dma_hw->ch[dma_chan_idx];
    dma_channel_config sample_dma = dma_channel_get_default_config(dma_chan_idx);
    sample_timer = dma_claim_unused_timer(true);
    dma_timer_set_fraction(sample_timer, 1, clock_get_hz(clk_sys)/DVI_AUDIO_FS);    //Assumes integer divisable sys_clk to fs ratio
    channel_config_set_dreq(&sample_dma, dma_get_timer_dreq(sample_timer));
    channel_config_set_read_increment(&sample_dma, false);
    channel_config_set_write_increment(&sample_dma, true);
    channel_config_set_ring(&sample_dma, true, DVI_AUDIO_BUF_SIZE_BITS);
    channel_config_set_transfer_data_size(&sample_dma, DMA_SIZE_32);
    dma_channel_configure(
        dma_chan_idx,
        &sample_dma,
        audio_buf,                        // dst exactly timed sample for DVI output
        source_sample,                    // src Latest sample value from source 
        0x10000001,                       // Self triggering, report every transfers
        true);

    dma_chan_idx = dma_claim_unused_channel(true);
    dma_di_chan = &dma_hw->ch[dma_chan_idx];
    dma_channel_config di_dma = dma_channel_get_default_config(dma_chan_idx);
    channel_config_set_read_increment(&di_dma, true);
    channel_config_set_write_increment(&di_dma, true);
    channel_config_set_transfer_data_size(&di_dma, DMA_SIZE_32);
    dma_channel_configure(
        dma_chan_idx,
        &di_dma,
        NULL,                              // dst set each time
        &di_buf[0],                        // src set each time 
        W_DATA_ISLAND,                     // Length of data island
        false);

    dvi_get_modeline_polarity(&vsync_polarity, &hsync_polarity);
}

bool dvi_audio_fs_tick(void){
    bool tick = !!(dma_hw->intr & 1u << dvi_audio_dma_sample_chan_idx);
    if(tick){
        dma_hw->intr = 1u << dvi_audio_dma_sample_chan_idx;
    }
    return tick;
}

uint32_t dvi_audio_count=0;

void dvi_audio_task(void){
    //SW interrupt monitoring of fs clock to not disturb DVI interrupt system

        hstx_packet_t packet;
        static int audio_frame_cnt = 0;
        if(dvi_audio_get_buflen() >= 4 && !dvi_audio_di_buf_is_full()){
            //Assuming working on groups of 4 samples means we won't cross ring buffer end
            audio_frame_cnt = hstx_packet_set_audio_samples(&packet,&audio_buf[tail_idx],4,audio_frame_cnt);
            //Assuming audio DI only in non-synch hblank period. TODO: sync polarity needs to be taking into account
            hstx_encode_data_island(&di_buf[di_head_idx],&packet,vsync_polarity,hsync_polarity);
            dvi_audio_pop_samples(4);
            dvi_audio_push_di(NULL);
            dvi_audio_count++;
    }
}

void dvi_audio_print_status(void){
    puts("DVI audio status");
    printf(" dvi audio:%s\n", dvi_audio_enabled ? "enabled" : "disabled");
    printf(" source_sample addr:%08x\n", source_sample);
    printf(" samples:%d\n packets:%d\n", dvi_audio_count<<2, dvi_audio_count);
    printf(" audio buf level:%d addr:%08x tail:%d\n", dvi_audio_get_buflen(), audio_buf, tail_idx);
    printf(" di buf level:%d\n", (di_head_idx - di_tail_idx) % DI_BUF_LEN);
    printf(" dma rd:%08x wr:%08x cnt:%08x\n", dma_sample_chan->read_addr, dma_sample_chan->write_addr, dma_sample_chan->transfer_count);
    printf(" sample %d %d\n", audio_buf[tail_idx].left, audio_buf[tail_idx].left);
}
