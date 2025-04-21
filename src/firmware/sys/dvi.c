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

volatile uint8_t framebuf[640];// xram

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
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        //ch->read_addr = (uintptr_t)&framebuf[(v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES)) * MODE_H_ACTIVE_PIXELS];
        ch->read_addr = (uintptr_t)&framebuf[0]; //Testing - repeat one line
        ch->transfer_count = MODE_H_ACTIVE_PIXELS / sizeof(uint32_t);
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
    }
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

    // Pixels (TMDS) come in 4 8-bit chunks. Control symbols (RAW) are an
    // entire 32-bit word.
    hstx_ctrl_hw->expand_shift =
        4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

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

    // OCULA HSTX mapping
    //   GP12 D1-  GP13 D1+
    //   GP14 D2+  GP15 D2-
    //   GP16 CK+  GP17 CK-
    //   GP18 D0-  DP19 D0+
#ifdef OCULA 
    hstx_ctrl_hw->bit[0] = hstx_map_lane_n(1);
    hstx_ctrl_hw->bit[1] = hstx_map_lane_p(1);
    hstx_ctrl_hw->bit[2] = hstx_map_lane_p(2);
    hstx_ctrl_hw->bit[3] = hstx_map_lane_n(2);
    hstx_ctrl_hw->bit[4] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[5] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    hstx_ctrl_hw->bit[6] = hstx_map_lane_n(0);
    hstx_ctrl_hw->bit[7] = hstx_map_lane_p(0);
#endif
    // PIVIC HSTX mapping
    //   GP12 CK-  GP13 CK+
    //   GP14 D0-  GP15 D0+
    //   GP16 D2+  GP17 D2-
    //   GP18 D1+  DP19 D1+
#ifdef PIVIC 
    hstx_ctrl_hw->bit[0] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    hstx_ctrl_hw->bit[1] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[2] = hstx_map_lane_n(0);
    hstx_ctrl_hw->bit[3] = hstx_map_lane_p(0);
    hstx_ctrl_hw->bit[4] = hstx_map_lane_p(2);
    hstx_ctrl_hw->bit[5] = hstx_map_lane_n(2);
    hstx_ctrl_hw->bit[6] = hstx_map_lane_p(1);
    hstx_ctrl_hw->bit[7] = hstx_map_lane_n(1);
#endif

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

    dma_channel_start(DMACH_PING);
}

void dvi_task(void){
    static uint16_t x = 0;

    framebuf[x] = x & 0xFF; 
    if(++x > 640){
        x = 0;
    }
}