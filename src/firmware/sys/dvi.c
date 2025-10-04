/*
 * Copyright (c) 2023 Rumbledethumps
 * Copyright (c) 2024 Raspberry Pi (Trading) Ltd.
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
    Using dvi_out_hstx_encoder from pico-examples as starting point, fitted in an RP6502 like display system 
*/

#include "main.h"
#include "sys/dvi.h"
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
#include <stdio.h>
#include <string.h>

volatile uint8_t dvi_framebuf[DVI_FB_HEIGHT][DVI_FB_WIDTH];

dvi_mode_t default_mode = {
    .pixel_format = dvi_4_rgb332,
    .scale_x = 3,
    .scale_y = 2,
    .offset_x = 0,
    .offset_y = 0

};
dvi_mode_t *dvi_mode = &default_mode;
int32_t fb_mode_offset;
uint32_t fb_mode_transfers;
uint8_t fb_mode_vshift;

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

#define MODE_H_SYNC_POLARITY 0
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS 640

#define MODE_V_SYNC_POLARITY 0
#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#define MODE_V_ACTIVE_LINES  480

#define MODE_H_TOTAL_PIXELS ( \
    MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + \
    MODE_H_BACK_PORCH  + MODE_H_ACTIVE_PIXELS \
)
#define MODE_V_TOTAL_LINES  ( \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + \
    MODE_V_BACK_PORCH  + MODE_V_ACTIVE_LINES \
)

#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

// ----------------------------------------------------------------------------
// HSTX command lists

// Lists are padded with NOPs to be >= HSTX FIFO size, to avoid DMA rapidly
// pingponging and tripping up the IRQs.

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V1_H1,
    HSTX_CMD_NOP
};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V0_H1,
    HSTX_CMD_NOP
};

static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_TMDS       | MODE_H_ACTIVE_PIXELS
};

void dvi_set_display(dvi_display_t display){
    //TODO
}

// ----------------------------------------------------------------------------
// DMA logic

volatile uint32_t irq_count = 0;

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

static void dma_irq_handler() {

    irq_count++;

    // dma_pong indicates the channel that just finished, which is the one
    // we're about to reload.
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;

    if (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);        
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);        
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        hw_set_bits(&ch->al1_ctrl, DMA_SIZE_32 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);        
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        hw_clear_bits(&ch->al1_ctrl, 0x3 << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB);   //DMA_SIZE_8
        ch->read_addr = (uintptr_t)&(dvi_framebuf[(v_scanline + fb_mode_offset)>>fb_mode_vshift]) + dvi_mode->offset_x;
        ch->transfer_count = fb_mode_transfers;//
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        if(++v_scanline >= MODE_V_TOTAL_LINES){
            v_scanline = 0;
        }
    }
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

    dvi_set_mode(&default_mode);

    // Serial output config: clock period of 5 cycles, pop from command
    // expander every 5 cycles, shift the output shiftreg by 2 every cycle.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // Note we are leaving the HSTX clock at the SDK default of 125 MHz; since
    // we shift out two bits per HSTX clock cycle, this gives us an output of
    // 250 Mbps, which is very close to the bit clock for 480p 60Hz (252 MHz).
    // If we want the exact rate then we'll have to reconfigure PLLs.

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

    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    dvi_fb_clear();

    dma_channel_start(DMACH_PING);
}

void dvi_task(void){
}

void dvi_set_mode(dvi_mode_t *mode){
    dvi_mode = mode;
    hstx_ctrl_hw->expand_shift =
        dvi_mode->scale_x << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    switch(dvi_mode->scale_x){
        case(2):
            fb_mode_transfers = MODE_H_ACTIVE_PIXELS / 2;
            break;
        case(3):
            fb_mode_transfers = (MODE_H_ACTIVE_PIXELS / 3) + 1;
            break;
        default:
            printf("DVI mode scale X = %d not supported\n", dvi_mode->scale_x);
            break;
    }
    switch(dvi_mode->scale_y){
        case(1):
            fb_mode_vshift = 0;
            break;
        case(2):
            fb_mode_vshift = 1;
            break;
        default:
            printf("DVI mode scale Y = %d not supported\n", dvi_mode->scale_y);
            break;
    }
    fb_mode_offset = (dvi_mode->offset_y - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES));
}

void dvi_print_status(void){
    printf("DVI status\n PING:%08x\n PONG:%08x\n", dma_hw->ch[DMACH_PING].al1_ctrl, dma_hw->ch[DMACH_PONG].al1_ctrl);
    printf(" HSTX clock:%ld\r\n", clock_get_hz(clk_hstx));
    printf(" HSTX stat:%08x\n", hstx_fifo_hw->stat);
    printf(" IRQ count:%08x\n", irq_count);
}