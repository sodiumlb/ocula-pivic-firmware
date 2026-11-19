/*
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "vic/aud.h"
#include "vic/cvbs.h"
#include "vic/cvbs_vic44_pal.h"
#include "vic/pen.h"
#include "vic/vic.h"
#include "vic/vic44_pal.h"
#include "sys/mem.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/m33.h"
#include <string.h>
#include <stdio.h>

// Constants related to video timing for PAL and NTSC.
#define PAL_HBLANK_END        12
#define PAL_HBLANK_START      70
#define PAL_VBLANK_START      1
#define PAL_VSYNC_START       4
#define PAL_VSYNC_END         6
#define PAL_VBLANK_END        9
#define PAL_LAST_LINE         311

// Colour command defines in cvbs_pal.h
// Palette structure loaded in cvbs.c
// In VIC44 mode each pixel is a half period of the colour carrier, 
typedef uint32_t palette_slice[16];

// Even/odd CVBS command pair for one colour (one pixel pair at fixed palette phases).
typedef struct { uint32_t e, o; } cmd_pair_t;

// Output one whole character (8 commands) from char data CD and colour data COL.
// FULL == 0 outputs only the last pair (HC=12, where the first 3 "pixels" are in hblank).
#define VIC44_OUT_CHAR(CD, COL, FULL) do {                                         \
    uint32_t _crf = vic_crf;                       /* one read: bg, border, rev */ \
    uint32_t _key = _crf | ((uint32_t)vic_cre << 8);                               \
    if (_key != multiKey) {                        /* bg/border/aux changed */     \
        multiKey = _key;                                                           \
        multiPairs[0] = pp[_crf >> 4];                                             \
        multiPairs[1] = pp[_crf & 7];                                              \
        multiPairs[3] = pp[(_key >> 12) & 0xF];                                    \
    }                                                                              \
    uint32_t _cd = (CD), _col = (COL);                                             \
    const cmd_pair_t *_t = multiPairs;                                             \
    if (_col & 0x08) {                             /* multicolour */               \
        multiPairs[2] = pp[_col & 7];                                              \
    } else {                                       /* hires */                     \
        if ((_crf & 0x08) == 0) _cd ^= 0xFF;       /* reverse */                   \
        _t = hiresPairs[pset][_crf >> 4][_col & 7];                                \
    }                                                                              \
    uint32_t _raw = cvbs_fifo_idx;                 /* free-running index */        \
    uint32_t _idx = _raw & (CVBS_FIFO_LEN - 1);    /* position in the ring */      \
    if (FULL) {                                                                    \
        if (_idx <= CVBS_FIFO_LEN - 8) {           /* no wrap: 4 pair copies */    \
            cmd_pair_t *_d = (cmd_pair_t *)(uintptr_t)&cvbs_fifo[_idx];            \
            _d[0] = _t[(_cd >> 6) & 3];                                            \
            _d[1] = _t[(_cd >> 4) & 3];                                            \
            _d[2] = _t[(_cd >> 2) & 3];                                            \
            _d[3] = _t[_cd & 3];                                                   \
        } else {                                   /* wraps: one word at a time */ \
            for (int _i = 6; _i >= 0; _i -= 2) {                                   \
                cmd_pair_t _p = _t[(_cd >> _i) & 3];                               \
                cvbs_fifo[_idx] = _p.e; _idx = (_idx + 1) & (CVBS_FIFO_LEN - 1);   \
                cvbs_fifo[_idx] = _p.o; _idx = (_idx + 1) & (CVBS_FIFO_LEN - 1);   \
            }                                                                      \
        }                                                                          \
        cvbs_fifo_idx = _raw + 8;                                                  \
    } else {                                                                       \
        cmd_pair_t _p = _t[_cd & 3];                                               \
        cvbs_fifo[_idx] = _p.e;                                                    \
        cvbs_fifo[(_idx + 1) & (CVBS_FIFO_LEN - 1)] = _p.o;                        \
        cvbs_fifo_idx = _raw + 2;                                                  \
    }                                                                              \
} while (0)

palette_slice *pal_palette_o = (palette_slice*)&cvbs_palette[0];    //[0]=phase1  [1]=phase2
palette_slice *pal_palette_e = (palette_slice*)&cvbs_palette[2];    //[2]=phase1 [3]=phase2

// Hires pairs for every background/foreground combination:
// [palette set: 0 = odd, 1 = even][bg][fg][bit pair b1b0]. 8 KB static RAM.
static cmd_pair_t hiresPairs[2][16][8][4];

static void vic44_build_hires_pairs(void) {
    for (int set = 0; set < 2; set++) {
        palette_slice *pal = set ? pal_palette_e : pal_palette_o;
        for (int bg = 0; bg < 16; bg++) {
            for (int fg = 0; fg < 8; fg++) {
                cmd_pair_t *t = hiresPairs[set][bg][fg];
                t[0].e = pal[0][bg]; t[0].o = pal[1][bg];   // 00: bg, bg
                t[1].e = pal[0][bg]; t[1].o = pal[1][fg];   // 01: bg, fg
                t[2].e = pal[0][fg]; t[2].o = pal[1][bg];   // 10: fg, bg
                t[3].e = pal[0][fg]; t[3].o = pal[1][fg];   // 11: fg, fg
            }
        }
    }
}

// Interleaved even/odd palettes for odd and even lines.
static cmd_pair_t palPairO[16], palPairE[16];

void vic44_pal_build_tables(void) {
    for (int c = 0; c < 16; c++) {
        palPairO[c].e = pal_palette_o[0][c]; palPairO[c].o = pal_palette_o[1][c];
        palPairE[c].e = pal_palette_e[0][c]; palPairE[c].o = pal_palette_e[1][c];
    }
    vic44_build_hires_pairs();
}

// 1K banks 4-7 and 9-11 are not connected on the VIC bus.
#define VIC44_UNCONNECTED_BANKS  0x0EF0u   // bits 4,5,6,7,9,10,11

static inline __attribute__((always_inline)) uint32_t vic44_bus_read(uint32_t bank, uint32_t index) {
    if (__builtin_expect((VIC44_UNCONNECTED_BANKS >> bank) & 1u, 0)) {
        return (uint8_t)XUNCON_REG;
    }
    return xram[index];
}

extern volatile uint32_t overruns;
volatile uint32_t vic44_max_cycles[128];   // read and print from core 0, e.g. in vic_task()

/**
 * Core 1 entry function for PAL VIC44 emulation.
 */
void vic44_core1_loop_pal(void) {

    // Once, at the start of vic44_core1_loop_pal() (DWT is per core, so this must run on core 1):
    m33_hw->demcr     |= M33_DEMCR_TRCENA_BITS;
    m33_hw->dwt_cyccnt = 0;
    m33_hw->dwt_ctrl  |= M33_DWT_CTRL_CYCCNTENA_BITS;

    //
    // START OF VIC CHIP STATE
    //

    // Counters.
    uint32_t videoMatrixCounter = 0;     // 12-bit video matrix counter (VMC)
    uint32_t videoMatrixLatch = 0;       // 12-bit latch that VMC is stored to and loaded from
    uint32_t verticalCounter = 0;        // 9-bit vertical counter (i.e. raster lines)
    uint32_t horizontalCounter = 0;      // 8-bit horizontal counter (although top bit isn't used)
    uint32_t prevHorizontalCounter = 0;  // 8-bit previous value of horizontal counter.
    uint32_t horizontalCellCounter = 0;  // 8-bit horizontal cell counter (down counter)
    uint32_t verticalCellCounter = 0;    // 6-bit vertical cell counter (down counter)
    uint32_t cellDepthCounter = 0;       // 4-bit cell depth counter (counts either from 0-7, or 0-15)

    // Values normally fetched externally, from screen mem, colour RAM and char mem.
    uint32_t cellIndex = 0;              // 8 bits fetched from screen memory.
    uint32_t charDataLatch = 0;          // 8 bits of bitmap data fetched from character memory (latched)
    uint32_t colourData = 0;             // 4 bits fetched from colour memory (top bit multi/hires mode)

    // Palette index for the current border colour.
    uint32_t borderColour = 0;

    // Pointer that alternates on each line between even and odd PAL palettes.
    palette_slice *pal_palette = pal_palette_e;

    // Optimisation to represent "in matrix", "address output enabled", and "pixel output enabled" 
    // all with one simple state variable. It might not be 100% accurate but should work for most 
    // cases.
    uint32_t fetchState = FETCH_OUTSIDE_MATRIX;
    
    //
    // END OF VIC CHIP STATE
    //


    // Temporary variables, not a core part of the state.
    uint32_t charDataOffset = 0;

    uint32_t lineVisible = 1;             // set at HC=1: 1 if this line is outside vblank

    const cmd_pair_t *pp = palPairE;      // current line's pair palette (matches pal_palette)
    cmd_pair_t multiPairs[4];             // pairs for codes 0..3: bg, border, fg, aux
    uint32_t multiKey = 0xFFFFFFFFu;      // colour cache key; invalid forces a rebuild
    uint32_t pset = 1;                    // hiresPairs palette set: 0 = odd line, 1 = even line (matches pp).
    vic44_pal_build_tables();

    //FIFO Back pressure. Preemtively added - uncomment and adjust if chroma stretching issues show up
    cvbs_push_cmd(CVBS_CMD_PAL_DC_RUN( 9,6)); 
    cvbs_push_cmd(CVBS_CMD_PAL_DC_RUN( 9,6)); 
    cvbs_push_cmd(CVBS_CMD_PAL_DC_RUN( 9,6)); 
    cvbs_push_cmd(CVBS_CMD_PAL_DC_RUN( 9,6)); 
    cvbs_fifo_enable();

    while (1) {
        // Poll for PIO IRQ 1. This is the rising edge of F1.
        while (!pio_interrupt_get(VIC_PIO, 1)) {
            tight_loop_contents();
        }

        const uint32_t t0 = m33_hw->dwt_cyccnt;
        // Clear the IRQ 1 flag immediately for now. 
        pio_interrupt_clear(VIC_PIO, 1);
        const uint32_t hc = horizontalCounter;   // HC this iteration emulates

        // VERTICAL TIMINGS:
        // Lines 1-9:    Vertical blanking
        // Lines 4-6:    Vertical sync
        // Lines 10-311: Normal visible lines.
        // Line 0:       Last visible line of a frame (yes, this is actually true)

        switch (horizontalCounter) {
      
            // HC = 0 is handled in a single block for ALL lines.
            case 0: 

                // Reset light pen counter
                // 15:8=VC/2 7:0=0 
                *pen_xy = (verticalCounter & 0xFC) << 7;
              
                // Reset pixel output to be all border colour at start of line.
                colourData = 0x08;      // multicolour
                charDataLatch = 0x55;   // all code 1 = border colour

                // Simplified state updates for HC=0. Counters and states still need to 
                // change as appropriate, regardless of it being during blanking.
                switch (fetchState) {
                    case FETCH_OUTSIDE_MATRIX:
                        // In HC=0, it is not possible to match screen origin x, if the last
                        // line was not a matrix line. This is as per the real chip. It is 
                        // possible to match screen origin y though.
                        if ((verticalCounter >> 1) == screen_origin_y) {
                            // This is the line the video matrix starts on. As in the real chip, we use
                            // a different state for the first part of the first video matrix line.
                            fetchState = FETCH_IN_MATRIX_Y;

                            // Screen origin X can match in the same cycle as Y.
                            if (prevHorizontalCounter == screen_origin_x) {
                                fetchState = FETCH_MATRIX_DLY_1;
                            }
                        }
                        break;
                    case FETCH_IN_MATRIX_Y:
                        // Since we are now looking at prev HC, this behaves same as FETCH_MATRIX_LINE.
                    case FETCH_MATRIX_LINE:
                        // NOTE: Due to comparison being prev HC, this is matching HC=70.
                        if (prevHorizontalCounter == screen_origin_x) {
                            fetchState = FETCH_MATRIX_DLY_1;
                        }
                        break;
                    case FETCH_MATRIX_DLY_1:
                    case FETCH_MATRIX_DLY_2:
                    case FETCH_MATRIX_DLY_3:
                        fetchState++;
                        break;
                    case FETCH_SCREEN_CODE:
                        fetchState = ((horizontalCellCounter-- > 0) ? FETCH_CHAR_DATA : FETCH_MATRIX_END);
                        break;
                    case FETCH_CHAR_DATA:
                        videoMatrixCounter++;
                        fetchState = FETCH_SCREEN_CODE;
                        break;
                    case FETCH_MATRIX_END:
                        fetchState = FETCH_MATRIX_LINE;
                        break;
                }

                prevHorizontalCounter = horizontalCounter++;
                break;
        
            // HC = 1 is another special case, handled in a single block for ALL lines. This
            // is when the "new line" signal is seen by most components. It is also the cycle 
            // during which we queue the horiz blanking, horiz sync, colour burst, vertical 
            // blanking and vsync, all up front for efficiency reasons.
            case 1:
          
                // This needs to be checked before the vertical counter is updated.
                if (fetchState == FETCH_OUTSIDE_MATRIX) {
                    if ((verticalCounter >> 1) == screen_origin_y) {
                        // This is the line the video matrix starts on. As in the real chip, we use
                        // a different state for the first part of the first video matrix line.
                        fetchState = FETCH_IN_MATRIX_Y;

                        // Screen origin X can match in the same cycle as Y.
                        if (prevHorizontalCounter == screen_origin_x) {
                            fetchState = FETCH_MATRIX_DLY_1;
                        }
                    }
                }

                // The Vertical Counter is incremented during HC=1, due to a deliberate 1 cycle
                // delay between the HC reset and the VC increment.

                if (verticalCounter == PAL_LAST_LINE) {
                    // Previous cycle was end of last line, so reset VC.
                    verticalCounter = 0;
                    fetchState = FETCH_OUTSIDE_MATRIX;
                    cellDepthCounter = 0;
                    // Reset Pen latch
                    *pen_dma_trans_reg = 1;
                } else {
                    // Otherwise increment line counter.
                    verticalCounter++;
                }
              
                // Update the raster line value stored in the VIC registers.
                vic_cr4 = (verticalCounter >> 1);
                if ((verticalCounter & 0x01) == 0) {
                    vic_cr3 &= 0x7F;
                } else {
                    vic_cr3 |= 0x80;
                }

                lineVisible = (verticalCounter == 0) || (verticalCounter > PAL_VBLANK_END);
                if (lineVisible) {
                    // In HC=1 for visible lines, we start with output the full sequence of CVBS
                    // commands for horizontal blanking, including the hsync and colour burst.
                    cvbs_push_cmd(VIC44_PAL_FRONTPORCH_2);
                    cvbs_push_cmd(VIC44_PAL_HSYNC_4);
                    cvbs_push_cmd(VIC44_PAL_HSYNC_4);
                    cvbs_push_cmd(VIC44_PAL_HSYNC_4);
                    cvbs_push_cmd(VIC44_PAL_HSYNC_4);
                    cvbs_push_cmd(VIC44_PAL_BREEZEWAY);
                    if (verticalCounter & 1) {
                        // Odd line. Switch colour palettes.
                        pal_palette = pal_palette_o;
                        pp = palPairO;
                        multiKey = 0xFFFFFFFFu;
                        pset = 0;
                        cvbs_push_cmd(cvbs_burst_cmd_odd);
                        cvbs_push_cmd(VIC44_PAL_BACKPORCH_O);
                    } else {
                        // Even line. Switch colour palettes.
                        pal_palette = pal_palette_e;
                        pp = palPairE;
                        multiKey = 0xFFFFFFFFu;
                        pset = 1;
                        cvbs_push_cmd(cvbs_burst_cmd_even);
                        cvbs_push_cmd(VIC44_PAL_BACKPORCH_E);
                    }
                }
                else {
                    // Vertical blanking and sync - Lines 1-9.
                    if (verticalCounter < PAL_VSYNC_START) {
                        // Lines 1, 2, 3.
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_L);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H1);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H2);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H3);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_L);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H1);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H2);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H3);
                    }
                    else if (verticalCounter <= PAL_VSYNC_END) {
                        // Vertical sync, lines 4, 5, 6.
                        cvbs_push_cmd(VIC44_PAL_LONG_SYNC_L1);
                        cvbs_push_cmd(VIC44_PAL_LONG_SYNC_L2);
                        cvbs_push_cmd(VIC44_PAL_LONG_SYNC_L3);
                        cvbs_push_cmd(VIC44_PAL_LONG_SYNC_H);
                        cvbs_push_cmd(VIC44_PAL_LONG_SYNC_L1);
                        cvbs_push_cmd(VIC44_PAL_LONG_SYNC_L2);
                        cvbs_push_cmd(VIC44_PAL_LONG_SYNC_L3);
                        cvbs_push_cmd(VIC44_PAL_LONG_SYNC_H);

                        // Vertical sync is what resets the video matrix latch.
                        videoMatrixLatch = videoMatrixCounter = 0;
                    }
                    else {
                        // Lines 7, 8, 9.
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_L);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H1);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H2);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H3);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_L);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H1);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H2);
                        cvbs_push_cmd(VIC44_PAL_SHORT_SYNC_H3);
                    }
                }

                // Due to the "new line" signal being generated by the Horizontal Counter Reset
                // logic, and the pass transistors used within it delaying the propagation of 
                // that signal, this signal doesn't get seen by components such as the Cell Depth
                // Counter Reset logic, the "In Matrix" status logic, and Video Matrix Latch 
                // until HC = 1.
              
                if (fetchState >= FETCH_MATRIX_DLY_1) {
                    // The real chip appears to have another increment in this cycle, if the 
                    // state is FETCH_CHAR_DATA. Not 100% clear though, since distortion ensues
                    // when setting the registers such that the matrix closes here.
                    if (fetchState == FETCH_CHAR_DATA) {
                        videoMatrixCounter++;
                    }
                    fetchState = FETCH_MATRIX_LINE;
                }
              
                // Check for Cell Depth Counter reset.
                if ((cellDepthCounter == last_line_of_cell) || (cellDepthCounter == 0xF)) {
                    // Reset CDC.
                    cellDepthCounter = 0;
                    
                    // If last line was the last line of the character cell, then we latch
                    // the current VMC value ready for the next character row.
                    videoMatrixLatch = videoMatrixCounter;
                    
                    // Vertical Cell Counter decrements when CDC resets, unless its the first line, 
                    // since it was loaded instead (see VC reset logic in HC=2).
                    if (verticalCounter > 0) {
                        verticalCellCounter--;
    
                        if ((verticalCellCounter == 0) && (screen_origin_x > 0)) {
                            // If all text rows rendered, then we're outside the matrix again.
                            fetchState = FETCH_OUTSIDE_MATRIX;
                        } else {
                            // NOTE: Due to comparison being prev HC, this is match HC=0.
                            if (prevHorizontalCounter == screen_origin_x) {
                                // Last line was in the matrix, so start the in matrix delay.
                                fetchState = FETCH_MATRIX_DLY_1;
                            }
                        }
                    }
                }
                else if (fetchState >= FETCH_MATRIX_LINE) {
                    // If the line that just ended was a video matrix line, then increment CDC,
                    // unless the VCC is 0, in which case close the matrix.
                    if (verticalCellCounter > 0) {
                        cellDepthCounter++;
                      
                        // NOTE: Due to comparison being prev HC, this is match HC=0.
                        if (prevHorizontalCounter == screen_origin_x) {
                            // Last line was in the matrix, so start the in matrix delay.
                            fetchState = FETCH_MATRIX_DLY_1;
                        }
                    } else {
                        fetchState = FETCH_OUTSIDE_MATRIX;
                    }
                }
                else if (fetchState == FETCH_IN_MATRIX_Y) {
                    // BUG: This logic is wrong, due to early screen origin y check in this cycle.
                    // IDEA: Might need to introduce a prevFetchState.
                    // NOTE: Bug doesn't affect NTSC version.

                    // If fetchState is FETCH_IN_MATRIX_Y at this point, it means that the
                    // last line matched the screen origin Y but not X. This results in the
                    // matrix being rendered one line lower if X now matches, as per real chip.
                    if (prevHorizontalCounter == screen_origin_x) {
                        fetchState = FETCH_IN_MATRIX_X;
                    }
                }
              
                prevHorizontalCounter = horizontalCounter++;
                break;
            
            // HC = 2 is yet another special case, handled in a single block for ALL 
            // lines. This is when the horizontal cell counter is loaded.
            case 2:
              
                // Simplified state changes. We're in hblank, so its just the bare minimum.
                switch (fetchState) {
                    case FETCH_OUTSIDE_MATRIX:
                        if ((verticalCounter >> 1) == screen_origin_y) {
                            // This is the line the video matrix starts on. As in the real chip, we use
                            // a different state for the first part of the first video matrix line.
                            fetchState = FETCH_IN_MATRIX_Y;

                            // Screen origin X can match in the same cycle as Y.
                            if (prevHorizontalCounter == screen_origin_x) {
                                fetchState = FETCH_MATRIX_DLY_1;
                            }
                        }
                        break;
                    case FETCH_IN_MATRIX_X:
                        // If screen origin x matched during HC=1, which can only mean that the screen 
                        // origin y matched on the previous line, then we move to second matrix delay 
                        // state, since the match happened in the previous cycle.
                        fetchState = FETCH_MATRIX_DLY_2;
                        break;
                    case FETCH_IN_MATRIX_Y:
                    case FETCH_MATRIX_LINE:
                        if (prevHorizontalCounter == screen_origin_x) {
                            fetchState = FETCH_MATRIX_DLY_1;
                        }
                        break;
                    case FETCH_MATRIX_DLY_1:
                    case FETCH_MATRIX_DLY_2:
                    case FETCH_MATRIX_DLY_3:
                        fetchState++;
                        break;
                    // In theory, the following states should not be possible at this point.
                    case FETCH_SCREEN_CODE:
                        fetchState = ((horizontalCellCounter-- > 0) ? FETCH_CHAR_DATA : FETCH_MATRIX_END);
                        break;
                    case FETCH_CHAR_DATA:
                        videoMatrixCounter++;
                        fetchState = FETCH_SCREEN_CODE;
                        break;
                    case FETCH_MATRIX_END:
                        fetchState = FETCH_MATRIX_LINE;
                        break;
                }
              
                // Video Matrix Counter (VMC) is reloaded from latch on "new line" signal.
                videoMatrixCounter = videoMatrixLatch;
                
                // Horizontal Cell Counter (HCC) is reloaded on "new line" signal.
                horizontalCellCounter = num_of_columns;
                prevHorizontalCounter = horizontalCounter++;
                break;

            // HC = 3 is yet another special case, handled in a single block for ALL 
            // lines. This is when the vertical cell counter is loaded
            case 3:
              
                // Simplified state changes. We're in hblank, so its just the bare minimum.
                switch (fetchState) {
                    case FETCH_OUTSIDE_MATRIX:
                        if ((verticalCounter >> 1) == screen_origin_y) {
                            // This is the line the video matrix starts on. As in the real chip, we use
                            // a different state for the first part of the first video matrix line.
                            fetchState = FETCH_IN_MATRIX_Y;

                            // Screen origin X can match in the same cycle as Y.
                            if (prevHorizontalCounter == screen_origin_x) {
                                fetchState = FETCH_MATRIX_DLY_1;
                            }
                        }
                        break;
                    case FETCH_IN_MATRIX_Y:
                    case FETCH_MATRIX_LINE:
                        if (prevHorizontalCounter == screen_origin_x) {
                            fetchState = FETCH_MATRIX_DLY_1;
                        }
                        break;
                    case FETCH_MATRIX_DLY_1:
                    case FETCH_MATRIX_DLY_2:
                    case FETCH_MATRIX_DLY_3:
                        fetchState++;
                        break;
                    // In theory, the following states should not be possible at this point.
                    case FETCH_SCREEN_CODE:
                        fetchState = ((horizontalCellCounter-- > 0) ? FETCH_CHAR_DATA : FETCH_MATRIX_END);
                        break;
                    case FETCH_CHAR_DATA:
                        videoMatrixCounter++;
                        fetchState = FETCH_SCREEN_CODE;
                        break;
                    case FETCH_MATRIX_END:
                        fetchState = FETCH_MATRIX_LINE;
                        break;
                }
                
                // Vertical Cell Counter is loaded 2 cycles after the VC resets.
                if (verticalCounter == 0) {
                    verticalCellCounter = num_of_rows;
                }
                
                prevHorizontalCounter = horizontalCounter++;
                break;

            // Covers HC=4 and above, up to HC=HBLANKSTART (e.g. HC=70 for PAL)
            default:

                // Line 0, and Lines after 9, are "visible", i.e. not within the vertical blanking.
                if (lineVisible) {
                    // Is the visible part of the line ending now and horizontal blanking starting?
                    if (horizontalCounter == PAL_HBLANK_START) {
                        // Horizontal blanking doesn't start until 3.66 pixels in. What exactly those
                        // two pixels are depends on the fetch state.
                        switch (fetchState) {
                            case FETCH_OUTSIDE_MATRIX:
                                if ((verticalCounter >> 1) == screen_origin_y) {
                                    // This is the line the video matrix starts on. As in the real chip, we use
                                    // a different state for the first part of the first video matrix line.
                                    fetchState = FETCH_IN_MATRIX_Y;

                                    // Screen origin X can match in the same cycle as Y.
                                    if (prevHorizontalCounter == screen_origin_x) {
                                        fetchState = FETCH_MATRIX_DLY_1;
                                    }
                                }
                                borderColour = border_colour_index;
                                cvbs_push_cmd(pal_palette[0][borderColour]);
                                cvbs_push_cmd(pal_palette[1][borderColour]);
                                cvbs_push_cmd(pal_palette[0][borderColour]);
                                cvbs_push_cmd(pal_palette[1][borderColour]);
                                cvbs_push_cmd(pal_palette[0][borderColour]);
                                cvbs_push_cmd(pal_palette[1][borderColour]);
                                cvbs_push_cmd(pal_palette[0][borderColour]);
                                cvbs_push_cmd(pal_palette[1][borderColour]);    //TODO Trunc
                                break;

                            case FETCH_MATRIX_LINE:
                                VIC44_OUT_CHAR(charDataLatch, colourData, 1);
                                charDataLatch = 0x55;
                                colourData = 0x08;
                                break;
                                
                            case FETCH_MATRIX_DLY_1:
                            case FETCH_MATRIX_DLY_2:
                            case FETCH_MATRIX_DLY_3:
                                fetchState++;
                                __attribute__((fallthrough));
                            case FETCH_IN_MATRIX_Y:
                                borderColour = border_colour_index;
                                cvbs_push_cmd(pal_palette[0][borderColour]);
                                cvbs_push_cmd(pal_palette[1][borderColour]);
                                cvbs_push_cmd(pal_palette[0][borderColour]);
                                cvbs_push_cmd(pal_palette[1][borderColour]);
                                cvbs_push_cmd(pal_palette[0][borderColour]);
                                cvbs_push_cmd(pal_palette[1][borderColour]);
                                cvbs_push_cmd(pal_palette[0][borderColour]);
                                cvbs_push_cmd(pal_palette[1][borderColour]);    //TODO Trunc
                                break;
                                
                            case FETCH_SCREEN_CODE:
                                VIC44_OUT_CHAR(charDataLatch, colourData, 1);
                                fetchState = ((horizontalCellCounter-- > 0) ? FETCH_CHAR_DATA : FETCH_MATRIX_END);
                                __attribute__((fallthrough));

                            case FETCH_CHAR_DATA:
                            case FETCH_MATRIX_END:
                                // If the matrix hasn't yet closed, then in the FETCH_CHAR_DATA
                                // state, we need to keep incrementing the video matrix counter
                                // until it is closed, which at the latest could be HC=1 on the
                                // next line.
                                if (fetchState == FETCH_MATRIX_END) {
                                    // Leaving the matrix
                                    fetchState = FETCH_MATRIX_LINE;
                                } else {
                                    // Increment the video matrix counter to next cell.
                                    videoMatrixCounter++;

                                    // Toggle fetch state. For efficiency, HCC deliberately not checked here.
                                    fetchState = FETCH_SCREEN_CODE;
                                }
                                break;
                        }
                        
                        // After the 3.66 visible pixels, we now output the start of horiz blanking.
                        cvbs_push_cmd(VIC44_PAL_FRONTPORCH_1);
                        
                        // Reset HC to start a new line.
                        prevHorizontalCounter = horizontalCounter;
                        horizontalCounter = 0;
                    }
                    else {
                        // Covers visible line cycles from HC=4 to 1 cycle before HC=HBLANKSTART (e.g. HC=70 for PAL)
                        switch (fetchState) {
                            case FETCH_OUTSIDE_MATRIX:
                                if ((verticalCounter >> 1) == screen_origin_y) {
                                    // This is the line the video matrix starts on. As in the real chip, we use
                                    // a different state for the first part of the first video matrix line.
                                    fetchState = FETCH_IN_MATRIX_Y;

                                    // Screen origin X can match in the same cycle as Y.
                                    if (prevHorizontalCounter == screen_origin_x) {
                                        fetchState = FETCH_MATRIX_DLY_1;
                                    }
                                }
                                if (horizontalCounter >= PAL_HBLANK_END) {
                                    // Output four border pixels.
                                    borderColour = border_colour_index;

                                    // Output only one visible border pixel for HC=12, as first three "pixels"
                                    // are part of the horizontal blanking. Note that the third one is due
                                    // to the switch delay in hblank turning off.
                                    if (horizontalCounter > PAL_HBLANK_END) {
                                        cvbs_push_cmd(pal_palette[0][borderColour]);
                                        cvbs_push_cmd(pal_palette[1][borderColour]);
                                        cvbs_push_cmd(pal_palette[0][borderColour]);
                                        cvbs_push_cmd(pal_palette[1][borderColour]);
                                        cvbs_push_cmd(pal_palette[0][borderColour]);
                                        cvbs_push_cmd(pal_palette[1][borderColour]);
                                    }
                                    cvbs_push_cmd(pal_palette[0][borderColour]);
                                    cvbs_push_cmd(pal_palette[1][borderColour]);
                                }
                                break;
                                
                            case FETCH_IN_MATRIX_Y:
                            case FETCH_MATRIX_LINE:
                                if (horizontalCounter >= PAL_HBLANK_END) {
                                    // Outputs the latch, which is normally border (0x55 multicolour) here,
                                    // then resets it to border.
                                    VIC44_OUT_CHAR(charDataLatch, colourData, horizontalCounter > PAL_HBLANK_END);
                                    charDataLatch = 0x55;
                                    colourData = 0x08;
                                    if (prevHorizontalCounter == screen_origin_x) {
                                        fetchState = FETCH_MATRIX_DLY_1;
                                    }
                                }
                                else if (prevHorizontalCounter == screen_origin_x) {
                                    // Still in horizontal blanking, but we still need to prepare for the case
                                    // where the next cycle isn't in horiz blanking, i.e. when HC=11 this cycle.
                                    fetchState = FETCH_MATRIX_DLY_1;
                                }
                                break;
                                
                            case FETCH_MATRIX_DLY_1:
                            case FETCH_MATRIX_DLY_2:
                            case FETCH_MATRIX_DLY_3:
                                if (horizontalCounter >= PAL_HBLANK_END) {
                                    // Output four border pixels.
                                    borderColour = border_colour_index;

                                    // Output only one visible border pixel for HC=12, as first three "pixels"
                                    // are part of the horizontal blanking. Note that the third one is due
                                    // to the switch delay in hblank turning off.
                                    if (horizontalCounter > PAL_HBLANK_END) {
                                        cvbs_push_cmd(pal_palette[0][borderColour]);
                                        cvbs_push_cmd(pal_palette[1][borderColour]);
                                        cvbs_push_cmd(pal_palette[0][borderColour]);
                                        cvbs_push_cmd(pal_palette[1][borderColour]);
                                        cvbs_push_cmd(pal_palette[0][borderColour]);
                                        cvbs_push_cmd(pal_palette[1][borderColour]);
                                    }
                                    cvbs_push_cmd(pal_palette[0][borderColour]);
                                    cvbs_push_cmd(pal_palette[1][borderColour]);
                                }

                                // Prime the pixel output queue with border pixels in multicolour 
                                // mode. Not quite what the real chip does but is functionally equivalent.
                                colourData = 0x08;
                                charDataLatch = 0x55;

                                fetchState++;
                                break;
                                
                            case FETCH_SCREEN_CODE: {
                                // Output all 8 pixels of the character fetched in the previous cycle
                                // (charDataLatch / colourData). Colours, reverse and hires are sampled now.
                                VIC44_OUT_CHAR(charDataLatch, colourData, horizontalCounter > PAL_HBLANK_END);

                                // Read the control registers once for this cycle.
                                const uint32_t cr5 = vic_cr5;
                                const uint32_t cr2 = vic_cr2;
                                const uint32_t cr3 = vic_cr3;

                                // Calculate address within video memory and fetch cell index.
                                // Assuming 0x0---, 0x3---- and 0x20-- as connected address space
                                const uint32_t screen_addr = ((((cr5 & 0xF0) << 6) | ((cr2 & 0x80) << 2)) + videoMatrixCounter) & 0xFFFF;
                                cellIndex = vic44_bus_read((screen_addr >> 10) & 0xF, screen_addr);

                                // Due to the way the colour memory is wired up, the above fetch of the cell index
                                // also happens to automatically fetch the foreground colour from the Colour Matrix
                                // via the top 4 lines of the data bus (DB8-DB11), which are wired directly from
                                // colour RAM in to the VIC chip.
                                colourData = xram[0x1400 + (screen_addr & 0x3ff)];

                                // Toggle fetch state. Close matrix if HCC hits zero.
                                fetchState = ((horizontalCellCounter-- > 0) ? FETCH_CHAR_DATA : FETCH_MATRIX_END);

                                // Calculate offset of data.
                                charDataOffset = ((cr5 & 0x0F) << 10) + (cellIndex << (3 + (cr3 & 0x01))) + cellDepthCounter;

                                // Fetch cell data.  It can wrap around, which is why we & with 0x3FFF.
                                // Initially latched to the side until it is needed.
                                // Assuming 0x0---, 0x3---- and 0x20-- as connected address space
                                charDataLatch = vic44_bus_read((charDataOffset >> 10) & 0xF, charDataOffset & 0x3FFF);

                                if (fetchState == FETCH_MATRIX_END) {
                                    // The character fetched in the MATRIX_END cycle is never displayed.
                                    charDataLatch = 0x55;
                                    colourData = 0x08;
                                    fetchState = FETCH_MATRIX_LINE;
                                } else {
                                    // Increment the video matrix counter to next cell.
                                    videoMatrixCounter++;

                                    // Toggle fetch state. For efficiency, HCC deliberately not checked here.
                                    fetchState = FETCH_SCREEN_CODE;
                                }
                                break;
                            }

                            case FETCH_CHAR_DATA:
                            case FETCH_MATRIX_END: {
                                // Reached when a cycle starts in this state, e.g. when the matrix is still
                                // open across the end of the line, so the registers are read again here.
                                // (FETCH_SCREEN_CODE does this fetch itself and no longer falls through.)
                                const uint32_t cr5 = vic_cr5;
                                const uint32_t cr3 = vic_cr3;

                                // Calculate offset of data.
                                charDataOffset = ((cr5 & 0x0F) << 10) + (cellIndex << (3 + (cr3 & 0x01))) + cellDepthCounter;

                                // Fetch cell data.  It can wrap around, which is why we & with 0x3FFF.
                                // Initially latched to the side until it is needed.
                                // Assuming 0x0---, 0x3---- and 0x20-- as connected address space
                                charDataLatch = vic44_bus_read((charDataOffset >> 10) & 0xF, charDataOffset & 0x3FFF);

                                if (fetchState == FETCH_MATRIX_END) {
                                    // The character fetched in the MATRIX_END cycle is never displayed.
                                    charDataLatch = 0x55;
                                    colourData = 0x08;
                                    fetchState = FETCH_MATRIX_LINE;
                                } else {
                                    // Increment the video matrix counter to next cell.
                                    videoMatrixCounter++;

                                    // Toggle fetch state. For efficiency, HCC deliberately not checked here.
                                    fetchState = FETCH_SCREEN_CODE;
                                }
                                break;
                            }
                        }
                        
                        prevHorizontalCounter = horizontalCounter++;
                    }
                } else {
                    // Inside vertical blanking. The CVBS commands for each line were already sent during 
                    // HC=0. In case the screen origin Y is set within the vertical blanking lines, we 
                    // still need to update the fetch state, video matrix counter, and the horizontal cell
                    // counter, even though we're not outputting character pixels. So for the rest of the 
                    // line, it is a simplified version of the standard line, except that we don't output 
                    // any pixels.
                    switch (fetchState) {
                        case FETCH_OUTSIDE_MATRIX:
                            if ((verticalCounter >> 1) == screen_origin_y) {
                                // This is the line the video matrix starts on. As in the real chip, we use
                                // a different state for the first part of the first video matrix line.
                                fetchState = FETCH_IN_MATRIX_Y;

                                // Screen origin X can match in the same cycle as Y.
                                if (prevHorizontalCounter == screen_origin_x) {
                                    fetchState = FETCH_MATRIX_DLY_1;
                                }
                            }
                            break;
                        case FETCH_IN_MATRIX_Y:
                        case FETCH_MATRIX_LINE:
                            if (prevHorizontalCounter == screen_origin_x) {
                                fetchState = FETCH_MATRIX_DLY_1;
                            }
                            break;
                        case FETCH_MATRIX_DLY_1:
                        case FETCH_MATRIX_DLY_2:
                        case FETCH_MATRIX_DLY_3:
                            fetchState++;
                            break;
                        case FETCH_SCREEN_CODE:
                            fetchState = ((horizontalCellCounter-- > 0) ? FETCH_CHAR_DATA : FETCH_MATRIX_END);
                            break;
                        case FETCH_CHAR_DATA:
                            videoMatrixCounter++;
                            fetchState = FETCH_SCREEN_CODE;
                            break;
                        case FETCH_MATRIX_END:
                            fetchState = FETCH_MATRIX_LINE;
                            break;
                    }
    
                    prevHorizontalCounter = horizontalCounter;
                    if (horizontalCounter == PAL_HBLANK_START) {
                        horizontalCounter = 0;
                    } else {
                        horizontalCounter++;
                    }
                }
                break;
        }

        aud_tick_inline((uint32_t*)&vic_cra);

        const uint32_t dt = m33_hw->dwt_cyccnt - t0;
        if (dt > vic44_max_cycles[hc]) {
            vic44_max_cycles[hc] = dt;
        }

        if (pio_interrupt_get(VIC_PIO, 1)) {
            overruns++;   // the next F1 edge arrived before this iteration finished
        }
    }
}
