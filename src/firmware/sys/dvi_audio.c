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
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include <pico/stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

//Buffer size must be power of 2
//Alignment for direct DMA use
#define DVI_AUDIO_BUF_LEN_BITS 7
#define DVI_AUDIO_BUF_LEN (1<<DVI_AUDIO_BUF_LEN_BITS)
#define DVI_AUDIO_BUF_SIZE_BITS (DVI_AUDIO_BUF_LEN_BITS+2)
#define DVI_AUDIO_BUF_SIZE (1<<DVI_AUDIO_BUF_SIZE_BITS)

static audio_sample_t __scratch_x("audio_data") audio_buf[DVI_AUDIO_BUF_LEN] 
    __attribute__ ((aligned(DVI_AUDIO_BUF_SIZE)));

static int dma_sample_chan_idx;
static dma_channel_hw_t *dma_sample_chan;
static dma_channel_hw_t *dma_di_chan;
static int sample_timer;

//static volatile uint16_t head_idx = 0;
static volatile uint16_t tail_idx = 0;
static bool vsync_polarity;
static bool hsync_polarity;

//Running own small DI queue
#define DI_BUF_LEN_BITS 5
#define DI_BUF_LEN (1<<DI_BUF_LEN_BITS)

static hstx_data_island_t di_buf[DI_BUF_LEN];
static volatile uint16_t di_head_idx = 0;
static volatile uint16_t di_tail_idx = 0;

static volatile audio_sample_t __scratch_x("audio_data") *source_sample = 0;

bool dvi_audio_buf_is_full(void){
    uint16_t head_idx = (uint16_t)((dma_sample_chan->write_addr>>2) & (DVI_AUDIO_BUF_LEN-1));
    return ((head_idx + 1) & (DVI_AUDIO_BUF_LEN-1)) == tail_idx;
}

uint16_t dvi_audio_get_buflen(void){
    uint16_t head_idx = (uint16_t)((dma_sample_chan->write_addr>>2) & (DVI_AUDIO_BUF_LEN-1));
    return (head_idx - tail_idx) & (DVI_AUDIO_BUF_LEN-1);
}

uint16_t dvi_audio_get_buflen_to_end(void){
    uint16_t buflen = dvi_audio_get_buflen();
    uint16_t to_end = DVI_AUDIO_BUF_LEN - tail_idx;
    return (to_end < buflen ? to_end : buflen);
}

//Pop just moves the tail index. 
bool dvi_audio_pop_samples(uint16_t count){
    tail_idx = (tail_idx + count) & (DVI_AUDIO_BUF_LEN-1);
    __dmb();
    return true;    
}

bool dvi_audio_di_buf_is_full(void){
    return ((di_head_idx + 1) & (DI_BUF_LEN-1)) == di_tail_idx;
}

bool dvi_audio_di_buf_is_empty(void){
    return di_head_idx == di_tail_idx;
}

//NULL pointer will skip copy. For use with direct allocation.
bool dvi_audio_push_di(hstx_data_island_t *di){
    if(((di_head_idx + 1) & (DI_BUF_LEN-1)) == di_tail_idx){
        return false;
    }
    if(di){
        memcpy((void*)&di_buf[di_head_idx], (void*)di, sizeof(hstx_data_island_t));
        // dma_di_chan->read_addr = (uint32_t)di;
        // dma_di_chan->al2_write_addr_trig = (uint32_t)&di_buf[di_head_idx];
    }
    di_head_idx = (di_head_idx + 1) & (DI_BUF_LEN-1);
    __dmb();
    return true;
}

static uint32_t dvi_audio_pop_underflow = 0;
bool dvi_audio_pop_di(uint32_t *di){
    if(di_head_idx == di_tail_idx){
        const uint32_t *null_di = hstx_get_null_data_island(vsync_polarity,hsync_polarity);
        //memcpy((void*)di,(void*)(null_di),sizeof(hstx_data_island_t));
        dma_di_chan->read_addr = (uint32_t)null_di;
        dma_di_chan->al2_write_addr_trig = (uint32_t)di;
        dvi_audio_pop_underflow++;
        return false;
    }else{
        // memcpy((void*)di,(void*)(&di_buf[di_tail_idx]),sizeof(hstx_data_island_t));
        uint32_t *tail_di = (uint32_t*)&di_buf[di_tail_idx];
        dma_di_chan->read_addr = (uint32_t)tail_di;
        dma_di_chan->al2_write_addr_trig = (uint32_t)di;
        di_tail_idx = (di_tail_idx + 1) & (DI_BUF_LEN-1);
        __dmb();
        return true;
    }
}

void dvi_audio_cpy_di(uint32_t *di_out, hstx_data_island_t *di_in){
        dma_di_chan->read_addr = (uint32_t)di_in;
        dma_di_chan->al2_write_addr_trig = (uint32_t)di_out;
}

// //Always return an audio packet from the buffer, but only advance the tail_idx if not empty
// bool dvi_audio_pop_di(uint32_t *di){
//     uint32_t *tail_di = (uint32_t*)&di_buf[di_tail_idx];
//     dma_di_chan->read_addr = (uint32_t)tail_di;
//     dma_di_chan->al2_write_addr_trig = (uint32_t)di;
//     if(di_head_idx == di_tail_idx){
//         dvi_audio_pop_underflow++;
//         return false;
//     }else{
//         // memcpy((void*)di,(void*)(&di_buf[di_tail_idx]),sizeof(hstx_data_island_t));
//         di_tail_idx = (di_tail_idx + 1) & (DI_BUF_LEN-1);
//         __dmb();
//         return true;
//     }
// }
static irq_handler_t audio_fs_cb = NULL;

void dvi_audio_set_sample_source(volatile audio_sample_t *src_ptr){
    source_sample = src_ptr;
}


void dvi_audio_set_fs_cb(irq_handler_t fn){
    audio_fs_cb = fn;
}

static absolute_time_t irq_time_stamp;
static void irq_handler(void){
    dma_hw->ints2 = dma_hw->ints2;
    audio_fs_cb();
}

bool dvi_audio_enabled = false;

void dvi_audio_init(void){
    // Audio sources may use the audio_fs_cb which requires this DMA and IRQ setup regardless dvi_audio is enabled or not.
    dma_sample_chan_idx = dma_claim_unused_channel(true);
    dma_sample_chan = &dma_hw->ch[dma_sample_chan_idx];
    dma_channel_config sample_dma = dma_channel_get_default_config(dma_sample_chan_idx);
    sample_timer = dma_claim_unused_timer(true);
    
    dma_timer_set_fraction(sample_timer, 2, (2*clock_get_hz(clk_sys))/(DVI_AUDIO_FS+64));    //Assumes 1/2 integer divisable sys_clk to fs ratio
    channel_config_set_dreq(&sample_dma, dma_get_timer_dreq(sample_timer));
    channel_config_set_read_increment(&sample_dma, false);
    channel_config_set_write_increment(&sample_dma, true);
    channel_config_set_ring(&sample_dma, true, DVI_AUDIO_BUF_SIZE_BITS);
    channel_config_set_transfer_data_size(&sample_dma, DMA_SIZE_32);
    dma_channel_configure(
        dma_sample_chan_idx,
        &sample_dma,
        audio_buf,                        // dst exactly timed sample for DVI output
        source_sample,                    // src Latest sample value from source 
        0x10000001,                       // Self triggering, report every transfers (FS)
        //0x10000000 + DVI_AUDIO_BUF_LEN,   // Self triggering, report every ring loop 
        true);

    dma_hw->ints2 = (1u << dma_sample_chan_idx);
    dma_hw->inte2 = (1u << dma_sample_chan_idx);

    irq_add_shared_handler(DMA_IRQ_2, irq_handler, PICO_SHARED_IRQ_HANDLER_HIGHEST_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_2, true);
    
    if(source_sample == NULL || cfg_get_dvi_audio() == 0)
        return;

    dvi_audio_enabled = true;

    int dma_chan_idx = dma_claim_unused_channel(true);
    dma_di_chan = &dma_hw->ch[dma_chan_idx];
    dma_channel_config di_dma = dma_channel_get_default_config(dma_chan_idx);
    channel_config_set_read_increment(&di_dma, true);
    channel_config_set_write_increment(&di_dma, true);
    channel_config_set_transfer_data_size(&di_dma, DMA_SIZE_32);
    channel_config_set_high_priority(&di_dma, true);
    dma_channel_configure(
        dma_chan_idx,
        &di_dma,
        NULL,                              // dst set each time
        &di_buf[0],                        // src set each time 
        W_DATA_ISLAND,                     // Length of data island
        false);

    dvi_get_modeline_polarity(&vsync_polarity, &hsync_polarity);
}

uint32_t dvi_audio_count=0;

void dvi_audio_task(void){
    //SW interrupt monitoring of fs clock to not disturb DVI interrupt system
    static int audio_frame_cnt = 0;

    while((dvi_audio_get_buflen() >= 4) && !dvi_audio_di_buf_is_full()){
        absolute_time_t t = get_absolute_time();
        //Assuming working on groups of 4 samples means we won't cross ring buffer end
        hstx_packet_t packet;
        int sample_num = dvi_audio_get_buflen_to_end();
        audio_frame_cnt = hstx_packet_set_audio_samples(&packet, &audio_buf[tail_idx], sample_num, audio_frame_cnt);
        //Assuming audio DI only in non-synch hblank period. TODO: sync polarity needs to be taken into account
        hstx_encode_data_island(&di_buf[di_head_idx], &packet, vsync_polarity, hsync_polarity);
        dvi_audio_pop_samples(sample_num);
        dvi_audio_push_di(NULL);
        dvi_audio_count++;
    }
}

void dvi_audio_print_status(void){
    puts("DVI audio status");
    printf(" dvi audio:%s\n", dvi_audio_enabled ? "enabled" : "disabled");
    printf(" source_sample addr:%08x\n", source_sample);
    printf(" samples:%d\n packets:%d\n", dvi_audio_count<<2, dvi_audio_count);
    printf(" audio buf level:%d addr:%08x head:%d tail:%d\n", dvi_audio_get_buflen(), audio_buf, ((dma_sample_chan->transfer_count) & (DI_BUF_LEN-1)), tail_idx);
    printf(" di buf level:%d head:%d tail:%d\n", (di_head_idx - di_tail_idx) & (DI_BUF_LEN-1), di_head_idx, di_tail_idx);
    printf(" dma rd:%08x wr:%08x cnt:%08x\n", dma_sample_chan->read_addr, dma_sample_chan->write_addr, dma_sample_chan->transfer_count);
    printf(" sample timer: %d / %d\n", dma_hw->timer[sample_timer]>>16, dma_hw->timer[sample_timer] & 0xFFFF);
    printf(" sample %d %d\n", audio_buf[tail_idx].left, audio_buf[tail_idx].right);
}