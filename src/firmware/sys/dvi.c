/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Raspberry Pi (Trading) Ltd.
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
    Using dvi_out_hstx_encoder from pico-examples as starting point, fitted in an RP6502 like display system 
    Integrating extensions from filliperama86
*/

#include "main.h"
#include "str.h"
#include "sys/cfg.h"
#include "sys/dvi.h"
#include "sys/dvi_audio.h"
#include "sys/mem.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/sio.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "pico_hdmi/hstx_packet.h"
#include <stdio.h>
#include <string.h>

volatile uint8_t dvi_framebuf[DVI_FB_HEIGHT][DVI_FB_WIDTH];

//System specific configs are defined in their respective display subsystems
dvi_modeline_t local_mode = {
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = 0,
    .offset_y = 0,
    .h_front_porch = 16,
    .h_sync_width = 96,
    .h_back_porch = 48,
    .h_active_pixels = 640,
    .v_front_porch = 10,
    .v_sync_width = 2,
    .v_back_porch = 33,
    .v_active_lines = 480,
    .sync_polarity = vneg_hneg,
    .vic = 1,
};
dvi_modeline_t *dvi_mode = &local_mode;
dvi_modeline_t *next_dvi_mode;
static bool switch_dvi_mode = false;

int32_t fb_mode_offset;
uint32_t fb_mode_transfers;

// ----------------------------------------------------------------------------
// DVI constants

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define DI_PREAMBLE ((TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))

#define VIDEO_PREAMBLE ((TMDS_CTRL_01 << 10) | (TMDS_CTRL_00 << 20))

#define VIDEO_GUARD_BAND (0x2CCu | (0x133u << 10) | (0x2CCu << 20))

#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

#define W_VIDEO_PREAMBLE 8
#define W_VIDEO_GUARD_BAND 2

//Data Islands
static hstx_data_island_t di_acr;
static hstx_data_island_t di_avi;
static hstx_data_island_t di_aud;

// ----------------------------------------------------------------------------
// HSTX command lists

// Lists are padded with NOPs to be >= HSTX FIFO size, to avoid DMA rapidly
// pingponging and tripping up the IRQs.

static uint32_t vblank_line_vsync_off[7];
static uint32_t __scratch_y("dvi_data") vblank_line_vsync_off_di[46];
static uint32_t vblank_line_vsync_on[7];
static uint32_t __scratch_y("dvi_data") vblank_line_vsync_on_di[46];
static uint32_t vactive_line[10];
static uint32_t vactive_line_gb[13+3];
static uint32_t __scratch_y("dvi_data") vactive_line_di[50+7];
static uint32_t vborder_line[11];
static uint32_t vborder_line_gb[15];
static uint32_t __scratch_y("dvi_data") vborder_line_di[2][52+4];
static uint32_t *ptr_di_active;
static uint32_t *ptr_di_border[2];
static uint32_t *ptr_di_vsync_off;
static uint32_t *ptr_di_vsync_on;
static uint32_t acr_line_incr;
static uint32_t acr_cts;

static hstx_packet_t debug_packet;

static uint32_t vblank_line_vsync_off_size;
static uint32_t vblank_line_vsync_off_di_size;
static uint32_t vblank_line_vsync_on_size;
static uint32_t vblank_line_vsync_on_di_size;
static uint32_t vactive_line_size;
static uint32_t vactive_line_gb_size;
static uint32_t vactive_line_di_size;
static uint32_t vborder_line_size;
static uint32_t vborder_line_gb_size;
static uint32_t vborder_line_di_size;

#define HSTX_SET_LANE0(lane21, lane0) ((lane21 & 0x3FFFFC00) | (lane0 & 0x000003FF))

void dvi_build_hstx_lists(dvi_modeline_t *mode){
    uint32_t *p;
    uint32_t *di_words;
    uint32_t sync_von_hon, sync_von_hof, sync_vof_hon, sync_vof_hof;
    bool vpol, hpol;
    dvi_get_modeline_polarity(&vpol,&hpol);
    switch(mode->sync_polarity){
        case(vneg_hneg):
            sync_von_hon = SYNC_V0_H0;
            sync_von_hof = SYNC_V0_H1;
            sync_vof_hon = SYNC_V1_H0;
            sync_vof_hof = SYNC_V1_H1;
            break;
        case(vneg_hpos):
            sync_von_hon = SYNC_V0_H1;
            sync_von_hof = SYNC_V0_H0;
            sync_vof_hon = SYNC_V1_H1;
            sync_vof_hof = SYNC_V1_H0;
            break;
        case(vpos_hneg):
            sync_von_hon = SYNC_V1_H0;
            sync_von_hof = SYNC_V1_H1;
            sync_vof_hon = SYNC_V0_H0;
            sync_vof_hof = SYNC_V0_H1;
            break;
        case(vpos_hpos):
            sync_von_hon = SYNC_V1_H1;
            sync_von_hof = SYNC_V1_H0;
            sync_vof_hon = SYNC_V0_H1;
            sync_vof_hof = SYNC_V0_H0;
            break;
    }
    p = &vblank_line_vsync_off[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_RAW_REPEAT | (mode->h_back_porch + mode->h_active_pixels);
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    vblank_line_vsync_off_size = (p-vblank_line_vsync_off);

    p = &vblank_line_vsync_off_di[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
    *p++ = HSTX_SET_LANE0(DI_PREAMBLE, sync_vof_hof);
    //*p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW | W_DATA_ISLAND;
    ptr_di_vsync_off = p;
    dvi_audio_pop_di(p);
    p += W_DATA_ISLAND;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch + mode->h_active_pixels - W_PREAMBLE - W_DATA_ISLAND;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    vblank_line_vsync_off_di_size = (p-vblank_line_vsync_off_di);

    p = &vblank_line_vsync_on[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_von_hof;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_von_hon;
    *p++ = HSTX_CMD_RAW_REPEAT | (mode->h_back_porch + mode->h_active_pixels);
    *p++ = sync_von_hof;
    *p++ = HSTX_CMD_NOP;
    vblank_line_vsync_on_size = (p-vblank_line_vsync_on);

    p = &vblank_line_vsync_on_di[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_von_hof;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_von_hon;
    *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
    *p++ = HSTX_SET_LANE0(DI_PREAMBLE, sync_von_hof);
    *p++ = HSTX_CMD_RAW | W_DATA_ISLAND;
    ptr_di_vsync_on = p;
    dvi_audio_pop_di(p);
    p += W_DATA_ISLAND;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch + mode->h_active_pixels - W_PREAMBLE - W_DATA_ISLAND;
    *p++ = sync_von_hof;
    *p++ = HSTX_CMD_NOP;
    vblank_line_vsync_on_di_size = (p-vblank_line_vsync_on_di);
    
    p = &vactive_line[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_TMDS       | mode->h_active_pixels;
    vactive_line_size = (p-vactive_line);

    p = &vactive_line_gb[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_PREAMBLE;
    *p++ = HSTX_SET_LANE0(VIDEO_PREAMBLE, sync_vof_hof);
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_GUARD_BAND;
    *p++ = VIDEO_GUARD_BAND;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_TMDS       | mode->h_active_pixels;
    vactive_line_gb_size = (p-vactive_line_gb);

    p = &vactive_line_di[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
    *p++ = HSTX_SET_LANE0(DI_PREAMBLE, sync_vof_hof);
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW | W_DATA_ISLAND;
    ptr_di_active = p;
    dvi_audio_pop_di(p);
    p += W_DATA_ISLAND;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch - W_PREAMBLE - W_DATA_ISLAND - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_PREAMBLE;
    *p++ = HSTX_SET_LANE0(VIDEO_PREAMBLE, sync_vof_hof);
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_GUARD_BAND;
    *p++ = VIDEO_GUARD_BAND;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_TMDS       | mode->h_active_pixels;
    vactive_line_di_size = (p-vactive_line_di);

    p = &vborder_line[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_TMDS_REPEAT| mode->h_active_pixels;
    *p++ = 0x00000000; //black border
    *p++ = HSTX_CMD_NOP;
    vborder_line_size = (p-vborder_line);
    
    p = &vborder_line_gb[0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_PREAMBLE;
    *p++ = HSTX_SET_LANE0(VIDEO_PREAMBLE, sync_vof_hof);
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_GUARD_BAND;
    *p++ = VIDEO_GUARD_BAND;
    *p++ = HSTX_CMD_TMDS_REPEAT| mode->h_active_pixels;
    *p++ = 0x00000000; //black border
    *p++ = HSTX_CMD_NOP;
    vborder_line_gb_size = (p-vborder_line_gb);

    p = &vborder_line_di[0][0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
    *p++ = HSTX_SET_LANE0(DI_PREAMBLE, sync_vof_hof);
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW | W_DATA_ISLAND;
    ptr_di_border[0] = p;
    dvi_audio_pop_di(p);
    p += W_DATA_ISLAND;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch - W_PREAMBLE - W_DATA_ISLAND - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_PREAMBLE;
    *p++ = HSTX_SET_LANE0(VIDEO_PREAMBLE, sync_vof_hof);
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_GUARD_BAND;
    *p++ = VIDEO_GUARD_BAND;
    *p++ = HSTX_CMD_TMDS_REPEAT| mode->h_active_pixels;
    *p++ = 0x00000000; //black border
    *p++ = HSTX_CMD_NOP;
    vborder_line_di_size = (p-vborder_line_di[0]);

    p = &vborder_line_di[1][0];
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_front_porch;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_sync_width;
    *p++ = sync_vof_hon;
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
    *p++ = HSTX_SET_LANE0(DI_PREAMBLE, sync_vof_hof);
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW | W_DATA_ISLAND;
    ptr_di_border[1] = p;
    dvi_audio_pop_di(p);
    p += W_DATA_ISLAND;
    *p++ = HSTX_CMD_RAW_REPEAT | mode->h_back_porch - W_PREAMBLE - W_DATA_ISLAND - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND;
    *p++ = sync_vof_hof;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_PREAMBLE;
    *p++ = HSTX_SET_LANE0(VIDEO_PREAMBLE, sync_vof_hof);
    *p++ = HSTX_CMD_NOP;
    *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_GUARD_BAND;
    *p++ = VIDEO_GUARD_BAND;
    *p++ = HSTX_CMD_TMDS_REPEAT| mode->h_active_pixels;
    *p++ = 0x00000000; //black border
    *p++ = HSTX_CMD_NOP;
};

// ----------------------------------------------------------------------------
// DMA logic

volatile uint32_t irq_count = 0;
volatile uint32_t audio_count = 0;
volatile uint32_t frame_count = 0;
volatile uint32_t acr_count = 0;

// First we ping. Then we pong. Then... we ping again.
static bool dma_pong = false;

// A ping and a pong are cued up initially, so the first time we enter this
// handler it is to cue up the second ping after the first ping has completed.
// This is the third scanline overall (-> =2 because zero-based).
static uint v_scanline = 2;

// During the vertical active period, we take two IRQs per scanline: one to
// post the command list, and another to post the pixels.
static bool vactive_cmdlist_posted = false;

int DMACH_PING;
int DMACH_PONG;

static uint mode_h_total_pixels;
static uint mode_v_total_lines;
static uint mode_v_sync_end;
static uint mode_v_blank_end;
static uint mode_v_border_top_end;
static uint mode_v_fb_end;
static uint mode_v_front_porch;
static uintptr_t mode_fb_start;
static bool dvi_audio_enabled;
static uint prev_audio_count;
static uint audio_packets_per_frame;
static uint audio_samples_per_line_24;
static uint acr_packets_per_line_24;

static void dma_irq_handler() {

    irq_count++;

    // dma_pong indicates the channel that just finished, which is the one
    // we're about to reload.
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    static uintptr_t fb_ptr = (uintptr_t)&dvi_framebuf;
    static uint repeat = 0;
    static uint32_t line_count = 0;

    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;

    if (vactive_cmdlist_posted){
        hw_clear_bits(&ch->al1_ctrl, 0x3 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);   //DMA_SIZE_8
        ch->read_addr = fb_ptr;
        ch->transfer_count = fb_mode_transfers;
        vactive_cmdlist_posted = false;
        if(++repeat >= dvi_mode->scale_y){
            fb_ptr += DVI_FB_WIDTH;
            repeat = 0;
        }
    } else if (v_scanline < dvi_mode->v_front_porch) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);        
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (v_scanline < mode_v_sync_end) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < mode_v_blank_end) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (v_scanline < mode_v_border_top_end || v_scanline >= mode_v_fb_end) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);
        ch->read_addr = (uintptr_t)vborder_line;
        ch->transfer_count = count_of(vborder_line);
    } else {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);  
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    }

    if (!vactive_cmdlist_posted) {
        line_count++;
        if(++v_scanline >= mode_v_total_lines){
            v_scanline = 0;
            fb_ptr = mode_fb_start;
            frame_count++;
            if(switch_dvi_mode){
                dvi_set_modeline(next_dvi_mode);
                switch_dvi_mode = false;
            }
        }
    }
}

static void __scratch_y("") dma_irq_handler_audio() {

    static uintptr_t fb_ptr = (uintptr_t)&dvi_framebuf;
    static uint repeat = 0;
    static bool send_sample = false;
    static uint32_t acr_pos;
    static bool send_acr = false; 

    // dma_pong indicates the channel that just finished, which is the one
    // we're about to reload.

    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;

    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;

    if (vactive_cmdlist_posted){
        hw_clear_bits(&ch->al1_ctrl, 0x3 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);   //DMA_SIZE_8
        ch->read_addr = fb_ptr;
        ch->transfer_count = fb_mode_transfers;
        vactive_cmdlist_posted = false;
        if(++repeat >= dvi_mode->scale_y){
            fb_ptr += DVI_FB_WIDTH;
            repeat = 0;
        }
    } else if (v_scanline == 0){
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);        
        if(frame_count & 1)
            dvi_audio_cpy_di(ptr_di_vsync_off, &di_avi);
        else
            dvi_audio_cpy_di(ptr_di_vsync_off, &di_aud);

        ch->read_addr = (uintptr_t)vblank_line_vsync_off_di;
        ch->transfer_count = count_of(vblank_line_vsync_off_di);
    } else if (v_scanline < mode_v_front_porch) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB); 
        if(false){
            if(dvi_audio_pop_di(ptr_di_vsync_off)){
                audio_count++;
            }  
            ch->read_addr = (uintptr_t)vblank_line_vsync_off_di;
            ch->transfer_count = count_of(vblank_line_vsync_off_di);
        }else if(send_acr){
            dvi_audio_cpy_di(ptr_di_vsync_off, &di_acr);
            ch->read_addr = (uintptr_t)vblank_line_vsync_off_di;
            ch->transfer_count = count_of(vblank_line_vsync_off_di);
            acr_pos -= (1<<24);
            acr_count++;
        }else{
            ch->read_addr = (uintptr_t)vblank_line_vsync_off;
            ch->transfer_count = count_of(vblank_line_vsync_off);
        }
    } else if (v_scanline < mode_v_sync_end) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < mode_v_blank_end) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);
        if(false){
            if(dvi_audio_pop_di(ptr_di_vsync_off)){
                audio_count++;
            }  
            ch->read_addr = (uintptr_t)vblank_line_vsync_off_di;
            ch->transfer_count = count_of(vblank_line_vsync_off_di);
        }else if(send_acr){
            dvi_audio_cpy_di(ptr_di_vsync_off, &di_acr);
            ch->read_addr = (uintptr_t)vblank_line_vsync_off_di;
            ch->transfer_count = count_of(vblank_line_vsync_off_di);
            acr_pos -= (1<<24);
            acr_count++;

        }else{
            ch->read_addr = (uintptr_t)vblank_line_vsync_off;
            ch->transfer_count = count_of(vblank_line_vsync_off);
        }
    } else if (v_scanline < mode_v_border_top_end || v_scanline >= mode_v_fb_end) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);
        if(send_sample){
            if(dvi_audio_pop_di(ptr_di_border[v_scanline & 1])){
                audio_count++;
            }  
            ch->read_addr = (uintptr_t)vborder_line_di[v_scanline & 1];
            ch->transfer_count = count_of(vborder_line_di[0]);
        }else if(send_acr){
            dvi_audio_cpy_di(ptr_di_border[v_scanline & 1], &di_acr);
            ch->read_addr = (uintptr_t)vborder_line_di[v_scanline & 1];
            ch->transfer_count = count_of(vborder_line_di[0]);
            acr_pos -= (1<<24);
            acr_count++;
        }else{
            ch->read_addr = (uintptr_t)vborder_line_gb;
            ch->transfer_count = count_of(vborder_line_gb);
        }
    } else {//} if (!vactive_cmdlist_posted) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB); 
        if(send_sample){
            if(dvi_audio_pop_di(ptr_di_active)){
                audio_count++;
            }  
            ch->read_addr = (uintptr_t)vactive_line_di;
            ch->transfer_count = count_of(vactive_line_di);
        }else if(send_acr){
            dvi_audio_cpy_di(ptr_di_active, &di_acr);
            ch->read_addr = (uintptr_t)vactive_line_di;
            ch->transfer_count = count_of(vactive_line_di);
            acr_pos -= (1<<24);
            acr_count++;
        }else{
            ch->read_addr = (uintptr_t)vactive_line_gb;
            ch->transfer_count = count_of(vactive_line_gb);
        }
        vactive_cmdlist_posted = true;
    }

    if (!vactive_cmdlist_posted) {
        send_sample = !dvi_audio_di_buf_is_empty();
        acr_pos += acr_packets_per_line_24;
        send_acr = (acr_pos >> 24);

        if(++v_scanline >= mode_v_total_lines){
            v_scanline = 0;
            fb_ptr = mode_fb_start;
            frame_count++;
            audio_packets_per_frame = audio_count - prev_audio_count;
            prev_audio_count = audio_count;
            if(switch_dvi_mode){
                dvi_set_modeline(next_dvi_mode);
                switch_dvi_mode = false;
            }
        }
    }
    irq_count++;
}

void dvi_fb_clear(void){
    memset((void*)dvi_framebuf, 0x00, sizeof(dvi_framebuf));
}

void dvi_init(void){
    // Configure HSTX's TMDS encoder for RGB332
    hstx_ctrl_hw->expand_tmds =
        2  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
        0  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
        2  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
        1  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
        26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

    dvi_set_modeline(dvi_mode);

    // Serial output config: clock period of 5 cycles, pop from command
    // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // HSTX outputs 0 through 7 appear on GPIO 12 through 19.

    // Assign expanded/raw data bits to TMDS lanes on HSTX pins:
    #define hstx_map_lane_p(lane) ( (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB | (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB )
    #define hstx_map_lane_n(lane) ( hstx_map_lane_p(lane) | HSTX_CTRL_BIT0_INV_BITS )

    // HSTX mapping
    //   GP12 CK-  GP13 CK+
    //   GP14 D0-  GP15 D0+
    //   GP16 D2+  GP17 D2-
    //   GP18 D1+  DP19 D1+
    hstx_ctrl_hw->bit[0] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    hstx_ctrl_hw->bit[1] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[2] = hstx_map_lane_n(0);
    hstx_ctrl_hw->bit[3] = hstx_map_lane_p(0);
    hstx_ctrl_hw->bit[4] = hstx_map_lane_p(2);
    hstx_ctrl_hw->bit[5] = hstx_map_lane_n(2);
    hstx_ctrl_hw->bit[6] = hstx_map_lane_p(1);
    hstx_ctrl_hw->bit[7] = hstx_map_lane_n(1);

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, GPIO_FUNC_HSTX); // HSTX
    }

    dvi_audio_enabled = cfg_get_dvi_audio() != 0;
    uint32_t pixel_clk = clock_get_hz(clk_hstx) / 5;
    acr_cts = (uint32_t)((uint64_t)pixel_clk * DVI_AUDIO_ACR_N / (128ULL * DVI_AUDIO_FS));
    audio_samples_per_line_24 = ((uint64_t)DVI_AUDIO_FS * (1u<<24) * mode_h_total_pixels) / (clock_get_hz(clk_hstx) / 5);
    acr_packets_per_line_24 = ((uint64_t)(1000 * (1ULL<<24) * mode_h_total_pixels) / pixel_clk); //Assuming standard 1000Hz ACR rate
    hstx_packet_t p;
    bool vpol, hpol;
    dvi_get_modeline_polarity(&vpol, &hpol);
    hstx_packet_set_acr(&p, DVI_AUDIO_ACR_N, acr_cts);
    hstx_encode_data_island(&di_acr,&p,vpol,hpol);

    hstx_packet_set_audio_infoframe(&p, DVI_AUDIO_FS, 2, 16);
    hstx_encode_data_island(&di_aud,&p,vpol,hpol);

    hstx_packet_set_avi_infoframe(&p, dvi_get_modeline_vic(), 0);
    hstx_encode_data_island(&di_avi,&p,vpol,hpol);

    // Both channels are set up identically, to transfer a whole scanline and
    // then chain to the opposite channel. Each time a channel finishes, we
    // reconfigure the one that just finished, meanwhile the opposite channel
    // is already making progress.
    DMACH_PING = dma_claim_unused_channel(true);
    DMACH_PONG = dma_claim_unused_channel(true);
    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(
        DMACH_PING,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(
        DMACH_PONG,
        &c,
        &hstx_fifo_hw->fifo,
        vblank_line_vsync_off,
        count_of(vblank_line_vsync_off),
        false
    );

    dma_hw->ints1 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte1 = (1u << DMACH_PING) | (1u << DMACH_PONG);

    if(cfg_get_dvi_audio()){
        irq_add_shared_handler(DMA_IRQ_1, dma_irq_handler_audio, PICO_SHARED_IRQ_HANDLER_HIGHEST_ORDER_PRIORITY);
    }else{
        irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    }
    irq_set_enabled(DMA_IRQ_1, true);

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    dvi_fb_clear();

    dma_channel_start(DMACH_PING);
}

void dvi_task(void){
    // static absolute_time_t timer = 0;
    // if(timer < get_absolute_time()){
    //     printf("aud samples per frame %d\n", audio_packets_per_frame*4);
    //     timer = delayed_by_ms(get_absolute_time(), 1000);
    // }
}

void dvi_set_modeline(dvi_modeline_t *ml){
    dvi_mode = ml;
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, 
        clock_get_hz(clk_sys), clock_get_hz(clk_sys)/ml->hstx_div);

    hstx_ctrl_hw->expand_shift =
        dvi_mode->scale_x << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    mode_h_total_pixels = 
        dvi_mode->h_front_porch + 
        dvi_mode->h_sync_width +
        dvi_mode->h_back_porch +
        dvi_mode->h_active_pixels;

    mode_v_total_lines = 
        dvi_mode->v_front_porch +
        dvi_mode->v_sync_width +
        dvi_mode->v_back_porch +
        dvi_mode->v_active_lines;

    dvi_build_hstx_lists(dvi_mode);

    switch(dvi_mode->scale_x){
        case(2):
            fb_mode_transfers = dvi_mode->h_active_pixels / 2;
            break;
        case(3):
            fb_mode_transfers = (dvi_mode->h_active_pixels + 2) / 3;
            break;
        case(4):
            fb_mode_transfers = dvi_mode->h_active_pixels / 4;
            break;
        case(5):
            fb_mode_transfers = (dvi_mode->h_active_pixels + 4) / 5;
            break;
        case(6):
            fb_mode_transfers = (dvi_mode->h_active_pixels + 6) / 6;
            break;
        default:
            printf("DVI mode scale X = %d not supported\n", dvi_mode->scale_x);
            break;
    }

    fb_mode_offset = (dvi_mode->offset_y - (mode_v_total_lines - dvi_mode->v_active_lines));

    mode_v_sync_end = 
        dvi_mode->v_front_porch +
        dvi_mode->v_sync_width;

    mode_v_blank_end = mode_v_total_lines - dvi_mode->v_active_lines;
    mode_v_border_top_end = (dvi_mode->offset_y > 0) ? 0 : (mode_v_blank_end - (dvi_mode->offset_y * dvi_mode->scale_y));
    mode_v_fb_end = mode_v_blank_end + (DVI_FB_HEIGHT * dvi_mode->scale_y) - dvi_mode->offset_y;
    mode_fb_start = (uintptr_t)&(dvi_framebuf[(dvi_mode->offset_y > 0 ) ? dvi_mode->offset_y : 0][dvi_mode->offset_x]);
    mode_v_front_porch = dvi_mode->v_front_porch;
    if(dvi_mode->offset_x < 0){
        mode_v_border_top_end += 1;
        mode_fb_start += DVI_FB_WIDTH;
    }
}

void dvi_print_modeline(dvi_modeline_t *ml){
    uint dotclk = clock_get_hz(clk_sys) / (ml->hstx_div * 5); // Dot clk is DDR 10 bit per pixel 
    uint htot = ml->h_active_pixels + ml->h_front_porch + ml->h_sync_width + ml->h_back_porch;
    uint vtot = ml->v_active_lines + ml->v_front_porch + ml->v_sync_width + ml->v_back_porch;
    printf(" %dx%d @ %.2f scale %dx%d\n", 
        ml->h_active_pixels, ml->v_active_lines, 
        dotclk / (htot * vtot * 1.0),
        ml->scale_x, ml->scale_y
    );
}

void dvi_get_modeline_polarity(bool *vsync, bool *hsync){
    *vsync = dvi_mode->sync_polarity == vpos_hneg || dvi_mode->sync_polarity == vpos_hpos; 
    *hsync = dvi_mode->sync_polarity == vneg_hpos || dvi_mode->sync_polarity == vpos_hpos; 
}

uint8_t dvi_get_modeline_vic(void){
    return dvi_mode->vic;
}


void dvi_print_hstx_packet(hstx_packet_t *p){
    puts("HSTX packet");
    printf(" Hdr: %02x %02x %02x %02x", p->header[0], p->header[1], p->header[2], p->header[3]);
    for(int i=0; i<4; i++){
        printf("\n SP%d:", i);
        for(int j=0; j<8; j++){
            printf(" %02x", p->subpacket[i][j]);
        }
        printf(" %s", &p->subpacket[i][i+1]);
    }
    puts("");

    printf("vblank_line_vsync_off_size %d %d\n", vblank_line_vsync_off_size*4, sizeof(vblank_line_vsync_off));
    printf("vblank_line_vsync_off_audio_size %d %d\n", vblank_line_vsync_off_di_size*4, sizeof(vblank_line_vsync_off_di));
    printf("vblank_line_vsync_on_size %d %d\n", vblank_line_vsync_on_size*4, sizeof(vblank_line_vsync_on));
    printf("vblank_line_vsync_on_di_size %d %d\n", vblank_line_vsync_on_di_size*4, sizeof(vblank_line_vsync_on_di));
    printf("vblank_line_vsync_off_di_size %d %d\n", vblank_line_vsync_on_di_size*4, sizeof(vblank_line_vsync_on_di));
    printf("vactive_line_size %d %d\n", vactive_line_size*4, sizeof(vactive_line));
    printf("vactive_line_gb_size %d %d\n", vactive_line_gb_size*4, sizeof(vactive_line_gb));
    printf("vactive_line_di_size %d %d\n", vactive_line_di_size*4, sizeof(vactive_line_di));
    printf("vborder_line_size %d %d\n", vborder_line_size*4, sizeof(vborder_line));
    printf("vborder_line_gb_size %d %d\n", vborder_line_gb_size*4, sizeof(vborder_line_gb));
    printf("vborder_line_di_size %d %d\n", vborder_line_di_size*4, sizeof(vborder_line_di));

}


void dvi_print_status(void){
    printf("DVI status\n PING:%08x\n PONG:%08x\n", dma_hw->ch[DMACH_PING].al1_ctrl, dma_hw->ch[DMACH_PONG].al1_ctrl);
    printf(" HSTX clock:%ld\r\n", clock_get_hz(clk_hstx));
    printf(" HSTX stat:%08x\n", hstx_fifo_hw->stat);
    printf(" IRQ count:%08x\n", irq_count);
    printf(" fb_mode_transfers:%d\n", fb_mode_transfers);
    printf(" audio_count_per_frame:%d / %d = %f \n", audio_count, frame_count, (float)audio_count/frame_count);
    printf(" acr_count_per_frame:%d / %d = %d \n", acr_count, frame_count, acr_count/frame_count);
    printf(" audio acr cts:%d n:%d %d\n", acr_cts, DVI_AUDIO_ACR_N, acr_line_incr);
    printf(" audio samples per line %f\n", (float)audio_samples_per_line_24 / (1<<24));
    printf(" ACR packets per line %f\n", (float)acr_packets_per_line_24 / (1<<24));
    dvi_print_modeline(dvi_mode);
    dvi_audio_print_status();
    // dvi_print_hstx_packet(&debug_packet);
}

bool parse_polarity(const char **args, size_t *len, dvi_sync_polarity_t *polarity)
{
    size_t i;
    for (i = 0; i < *len; i++)
    {
        if ((*args)[i] != ' ')
            break;
    }
    if (i == *len)
        return false;

    if((*args)[i]=='-' && (*args)[i+1]=='-'){
        *polarity = vneg_hneg;
    }else if((*args)[i]=='-' && (*args)[i+1]=='+'){
        *polarity = vpos_hneg;
    }else if((*args)[i]=='+' && (*args)[i+1]=='-'){
        *polarity = vneg_hpos;
    }else if((*args)[i]=='+' && (*args)[i+1]=='+'){
        *polarity = vpos_hpos;
    }else{
        return false;
    }
    i+=2;
    *len -= i;
    *args += i;
    return true;
}

void dvi_mon_modeline(const char *args, size_t len){
   uint32_t hactive;
   uint32_t hsync_start;
   uint32_t hsync_end;
   uint32_t htotal;
   uint32_t vactive;
   uint32_t vsync_start;
   uint32_t vsync_end;
   uint32_t vtotal;
   dvi_sync_polarity_t hvpolarity;

   if (len) {
       if (
            parse_uint32(&args, &len, &hactive) &&
            parse_uint32(&args, &len, &hsync_start) &&
            parse_uint32(&args, &len, &hsync_end) &&
            parse_uint32(&args, &len, &htotal) &&
            parse_uint32(&args, &len, &vactive) &&
            parse_uint32(&args, &len, &vsync_start) &&
            parse_uint32(&args, &len, &vsync_end) &&
            parse_uint32(&args, &len, &vtotal) &&
            parse_polarity(&args, &len, &hvpolarity) &&
            parse_end(args, len))
       {
            local_mode.h_active_pixels = hactive;
            local_mode.v_active_lines = vactive;

            local_mode.h_front_porch = hsync_start - hactive;
            local_mode.h_sync_width = hsync_end - hsync_start;
            local_mode.h_back_porch = htotal - hsync_end;

            local_mode.v_front_porch = vsync_start - vactive;
            local_mode.v_sync_width = vsync_end - vsync_start;
            local_mode.v_back_porch = vtotal - vsync_end;

            local_mode.sync_polarity = hvpolarity;

            //Take non modeline parameters from current active mode
            //TODO auto calculate?
            local_mode.hstx_div = dvi_mode->hstx_div;
            local_mode.offset_x = dvi_mode->offset_x;
            local_mode.offset_y = dvi_mode->offset_y;
            local_mode.scale_x = dvi_mode->scale_x;
            local_mode.scale_y = dvi_mode->scale_y;

            dvi_print_modeline(&local_mode);
            next_dvi_mode = &local_mode;
            switch_dvi_mode = true;
        }else{
            printf("?invalid argument\n");
            return;
        }
    }else{
        printf("modeline %d %d %d %d %d %d %d %d %c%c\n",
            dvi_mode->h_active_pixels,
            dvi_mode->h_active_pixels + dvi_mode->h_front_porch,
            dvi_mode->h_active_pixels + dvi_mode->h_front_porch + dvi_mode->h_sync_width,
            dvi_mode->h_active_pixels + dvi_mode->h_front_porch + dvi_mode->h_sync_width + dvi_mode->h_back_porch,
            dvi_mode->v_active_lines,
            dvi_mode->v_active_lines + dvi_mode->v_front_porch,
            dvi_mode->v_active_lines + dvi_mode->v_front_porch + dvi_mode->v_sync_width,
            dvi_mode->v_active_lines + dvi_mode->v_front_porch + dvi_mode->v_sync_width + dvi_mode->v_back_porch,
            (dvi_mode->sync_polarity == vneg_hneg || dvi_mode->sync_polarity == vpos_hneg) ? '-' : '+',
            (dvi_mode->sync_polarity == vneg_hneg || dvi_mode->sync_polarity == vneg_hpos) ? '-' : '+'
        );
    }
}
