/*
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "cvbs_pal.h"
#include "vic/aud.h"
#include "vic/vic.h"
#include "vic/vic_pal.h"
#include "sys/dvi.h"
#include "sys/mem.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
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
uint32_t pal_palette_o[16] = {
    PAL_BLACK,
    PAL_WHITE,
    PAL_RED_O,
    PAL_CYAN_O,
    PAL_PURPLE_O,
    PAL_GREEN_O,
    PAL_BLUE_O,
    PAL_YELLOW_O,
    PAL_ORANGE_O,
    PAL_LORANGE_O,
    PAL_PINK_O,
    PAL_LCYAN_O,
    PAL_LPURPLE_O,
    PAL_LGREEN_O,
    PAL_LBLUE_O,
    PAL_LYELLOW_O
};

uint32_t pal_palette_e[16] = {
    PAL_BLACK,
    PAL_WHITE,
    PAL_RED_E,
    PAL_CYAN_E,
    PAL_PURPLE_E,
    PAL_GREEN_E,
    PAL_BLUE_E,
    PAL_YELLOW_E,
    PAL_ORANGE_E,
    PAL_LORANGE_E,
    PAL_PINK_E,
    PAL_LCYAN_E,
    PAL_LPURPLE_E,
    PAL_LGREEN_E,
    PAL_LBLUE_E,
    PAL_LYELLOW_E
};

uint32_t pal_trunc_palette_o[16] = {
    PAL_TRUNC_BLACK,
    PAL_TRUNC_WHITE,
    PAL_TRUNC_RED_O,
    PAL_TRUNC_CYAN_O,
    PAL_TRUNC_PURPLE_O,
    PAL_TRUNC_GREEN_O,
    PAL_TRUNC_BLUE_O,
    PAL_TRUNC_YELLOW_O,
    PAL_TRUNC_ORANGE_O,
    PAL_TRUNC_LORANGE_O,
    PAL_TRUNC_PINK_O,
    PAL_TRUNC_LCYAN_O,
    PAL_TRUNC_LPURPLE_O,
    PAL_TRUNC_LGREEN_O,
    PAL_TRUNC_LBLUE_O,
    PAL_TRUNC_LYELLOW_O
};

uint32_t pal_trunc_palette_e[16] = {
    PAL_TRUNC_BLACK,
    PAL_TRUNC_WHITE,
    PAL_TRUNC_RED_E,
    PAL_TRUNC_CYAN_E,
    PAL_TRUNC_PURPLE_E,
    PAL_TRUNC_GREEN_E,
    PAL_TRUNC_BLUE_E,
    PAL_TRUNC_YELLOW_E,
    PAL_TRUNC_ORANGE_E,
    PAL_TRUNC_LORANGE_E,
    PAL_TRUNC_PINK_E,
    PAL_TRUNC_LCYAN_E,
    PAL_TRUNC_LPURPLE_E,
    PAL_TRUNC_LGREEN_E,
    PAL_TRUNC_LBLUE_E,
    PAL_TRUNC_LYELLOW_E
};
//For DVI output
//TODO This is currently using NTSC based colours - needs to be adjusted
static const uint8_t pal_palette_rgb332[16] = {
    0x00,   //Black
    0xff,   //White
    0x84,   //Red
    0x9f,   //Cyan
    0x66,   //Purple
    0x75,   //Green
    0x22,   //Blue
    0xfd,   //Yellow
    0x88,   //Orange
    0xfa,   //LOrange
    0xd2,   //Pink
    0xdf,   //LCyan
    0xf7,   //LPurple
    0xbe,   //LGreen
    0xb7,   //LBlue
    0xfe    //LYellow
};

extern volatile uint32_t overruns;

/**
 * Core 1 entry function for PAL 6561 VIC emulation.
 */
void vic_core1_loop_pal_new(void) {

    //
    // START OF VIC CHIP STATE
    //

    // Counters.
    uint16_t videoMatrixCounter = 0;     // 12-bit video matrix counter (VMC)
    uint16_t videoMatrixLatch = 0;       // 12-bit latch that VMC is stored to and loaded from
    uint16_t verticalCounter = 0;        // 9-bit vertical counter (i.e. raster lines)
    uint8_t  horizontalCounter = 0;      // 8-bit horizontal counter (although top bit isn't used)
    uint8_t  prevHorizontalCounter = 0;  // 8-bit previous value of horizontal counter.
    uint8_t  horizontalCellCounter = 0;  // 8-bit horizontal cell counter (down counter)
    uint8_t  verticalCellCounter = 0;    // 6-bit vertical cell counter (down counter)
    uint8_t  cellDepthCounter = 0;       // 4-bit cell depth counter (counts either from 0-7, or 0-15)

    uint16_t dvi_pixel = 0;              // DVI pixel output index
    uint16_t dvi_line = 0;               // DVI line output index

    // Values normally fetched externally, from screen mem, colour RAM and char mem.
    uint8_t  cellIndex = 0;              // 8 bits fetched from screen memory.
    uint8_t  charData = 0;               // 8 bits of bitmap data fetched from character memory.
    uint8_t  charDataLatch = 0;          // 8 bits of bitmap data fetched from character memory (latched)
    uint8_t  colourData = 0;             // 4 bits fetched from colour memory (top bit multi/hires mode)
    uint8_t  hiresMode = 0;

    // Palette index for the current border colour.
    uint8_t borderColour = 0;

    // Holds the palette index for each of the current multi colour colours.
    uint8_t multiColourTable[4] = { 0, 0, 0, 0};

    // Every cpu cycle, we output four pixels. The values are temporarily stored in these vars.
    uint8_t pixel1 = 0;
    uint8_t pixel2 = 0;
    uint8_t pixel3 = 0;
    uint8_t pixel4 = 0;
    uint8_t pixel5 = 0;
    uint8_t pixel6 = 0;
    uint8_t pixel7 = 0;
    uint8_t pixel8 = 0;

    // Pointer that alternates on each line between even and odd PAL palettes.
    uint32_t *pal_palette = pal_palette_e;
    uint32_t *pal_trunc_palette = pal_trunc_palette_e;

    // Optimisation to represent "in matrix", "address output enabled", and "pixel output enabled" 
    // all with one simple state variable. It might not be 100% accurate but should work for most 
    // cases.
    uint8_t fetchState = FETCH_OUTSIDE_MATRIX;
    
    //
    // END OF VIC CHIP STATE
    //


    // Temporary variables, not a core part of the state.
    uint16_t charDataOffset = 0;

    //FIFO Back pressure. Preemtively added - uncomment and adjust if chroma stretching issues show up
    pio_sm_put(CVBS_PIO,CVBS_SM,CVBS_CMD_PAL_DC_RUN( 9,38)); 

    while (1) {
        // Poll for PIO IRQ 1. This is the rising edge of F1.
        while (!pio_interrupt_get(VIC_PIO, 1)) {
            tight_loop_contents();
        }
        
        // Clear the IRQ 1 flag immediately for now. 
        pio_interrupt_clear(VIC_PIO, 1);

        // VERTICAL TIMINGS:
        // Lines 1-9:    Vertical blanking
        // Lines 4-6:    Vertical sync
        // Lines 10-311: Normal visible lines.
        // Line 0:       Last visible line of a frame (yes, this is actually true)

        // TODO: Not sure if the fetch state processing should happen first or the HC value processing.
        switch (fetchState) {
            case FETCH_OUTSIDE_MATRIX:
                if ((verticalCounter >> 1) == screen_origin_y) {
                    // This is the line the video matrix starts on. As in the real chip, we use
                    // a different state for the first part of the first video matrix line.
                    fetchState = FETCH_IN_MATRIX_Y;
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

            case FETCH_SCREEN_CODE:
                fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                break;

            case FETCH_CHAR_DATA:
                videoMatrixCounter++;
                fetchState = FETCH_SCREEN_CODE;
                break;
        }

        switch (horizontalCounter) {

            case 0:
                // Reset pixel output buffer to be all border colour at start of line.
                pixel1 = pixel2 = pixel3 = pixel4 = pixel5 = pixel6 = pixel7 = pixel8 = 1;
                hiresMode = false;
                colourData = 0x08;
                charData = charDataLatch = 0x55;
                break;

            case 1:
                // The Vertical Counter is incremented during HC=1, due to a deliberate 1 cycle
                // delay between the HC reset and the VC increment.
                if (verticalCounter == PAL_LAST_LINE) {
                    // Previous cycle was end of last line, so reset VC.
                    verticalCounter = 0;
                    fetchState = FETCH_OUTSIDE_MATRIX;
                    cellDepthCounter = 0;
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

                if ((verticalCounter == 0) || (verticalCounter > PAL_VBLANK_END)) {
                    // In HC=1 for visible lines, we start with output the full sequence of CVBS
                    // commands for horizontal blanking, including the hsync and colour burst.
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_FRONTPORCH_2);
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_HSYNC);
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BREEZEWAY);
                    if (verticalCounter & 1) {
                        // Odd line. Switch colour palettes.
                        pal_palette = pal_palette_o;
                        pal_trunc_palette = pal_trunc_palette_o;
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_COLBURST_O);
                    } else {
                        // Even line. Switch colour palettes.
                        pal_palette = pal_palette_e;
                        pal_trunc_palette = pal_trunc_palette_e;
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_COLBURST_E);
                    }
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BACKPORCH);
                }
                else {
                    // Vertical blanking and sync - Lines 1-9.
                    if (verticalCounter < PAL_VSYNC_START) {
                        // Lines 1, 2, 3.
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SHORT_SYNC_L);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SHORT_SYNC_H);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SHORT_SYNC_L);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SHORT_SYNC_H);
                    }
                    else if (verticalCounter <= PAL_VSYNC_END) {
                        // Vertical sync, lines 4, 5, 6.
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_LONG_SYNC_L);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_LONG_SYNC_H);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_LONG_SYNC_L);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_LONG_SYNC_H);

                        // Vertical sync is what resets the video matrix latch.
                        videoMatrixLatch = videoMatrixCounter = 0;
                    }
                    else {
                        // Lines 7, 8, 9.
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SHORT_SYNC_L);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SHORT_SYNC_H);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SHORT_SYNC_L);
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SHORT_SYNC_H);
                    }
                }
                dvi_line = verticalCounter;
                dvi_pixel = 0;

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
                break;

            case 2:
                // Video Matrix Counter (VMC) is reloaded from latch on "new line" signal.
                videoMatrixCounter = videoMatrixLatch;
                
                // Horizontal Cell Counter (HCC) is reloaded on "new line" signal.
                horizontalCellCounter = num_of_columns;
                break;

            case 3:
                // Vertical Cell Counter is loaded 2 cycles after the VC resets.
                if (verticalCounter == 0) {
                    verticalCellCounter = num_of_rows;
                }
                break;

            default:
                // Line 0, and Lines after 9, are "visible", i.e. not within the vertical blanking.
                if ((verticalCounter == 0) || (verticalCounter > PAL_VBLANK_END)) {

                    

                }
                else {
                    // Verticaly blanking, so nothing to do, as fetch state already processed and
                    // CVBS commands already sent.
                }
                break;

        }

        prevHorizontalCounter = horizontalCounter;
        if (horizontalCounter == PAL_HBLANK_START) {
            horizontalCounter = 0;
        } else {
            horizontalCounter++;
        }

        aud_tick_inline((uint32_t*)&vic_cra);
        
        // DEBUG: Temporary check to see if we've overshot the 120 cycle allowance.
        if (pio_interrupt_get(VIC_PIO, 1)) {
            overruns = 1;
        }
    }
}
