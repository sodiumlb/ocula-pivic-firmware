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
void vic_core1_loop_pal(void) {

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
    pio_sm_put(CVBS_PIO,CVBS_SM,CVBS_CMD_PAL_DC_RUN(18,38)); 

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

        switch (horizontalCounter) {
      
            // HC = 0 is handled in a single block for ALL lines.
            case 0: 
              
                // Reset pixel output buffer to be all border colour at start of line.
                pixel1 = pixel2 = pixel3 = pixel4 = pixel5 = pixel6 = pixel7 = pixel8 = 1;
                hiresMode = false;

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
                        fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                        break;
                    case FETCH_CHAR_DATA:
                        videoMatrixCounter++;
                        fetchState = FETCH_SCREEN_CODE;
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
                    }
                }

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
                        fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                        break;
                    case FETCH_CHAR_DATA:
                        videoMatrixCounter++;
                        fetchState = FETCH_SCREEN_CODE;
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
                        fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                        break;
                    case FETCH_CHAR_DATA:
                        videoMatrixCounter++;
                        fetchState = FETCH_SCREEN_CODE;
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
                if ((verticalCounter == 0) || (verticalCounter > PAL_VBLANK_END)) {
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
                                }
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_trunc_palette[borderColour]);
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                break;

                            case FETCH_MATRIX_LINE:
                                // Look up latest background, border and auxiliary colours.
                                multiColourTable[0] = background_colour_index;
                                multiColourTable[1] = border_colour_index;
                                multiColourTable[3] = auxiliary_colour_index;

                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel2]]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel3]]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel4]]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_trunc_palette[multiColourTable[pixel5]]);
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel2]];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel3]];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel4]];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel5]];
                                break;
                                
                            case FETCH_MATRIX_DLY_1:
                            case FETCH_MATRIX_DLY_2:
                            case FETCH_MATRIX_DLY_3:
                                fetchState++;
                                __attribute__((fallthrough));
                            case FETCH_IN_MATRIX_Y:
                                borderColour = border_colour_index;
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_trunc_palette[borderColour]);
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                break;
                                
                            case FETCH_SCREEN_CODE:
                                // Look up latest background, border and auxiliary colours.
                                multiColourTable[0] = background_colour_index;
                                multiColourTable[1] = border_colour_index;
                                multiColourTable[3] = auxiliary_colour_index;
                                
                                // First 3 wholes pixels are from end of current character.
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel6]]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel7]]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel8]]);
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel6]];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel7]];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel8]];

                                // Update operating hires state and char data immediately prior to
                                // shifting out new character pixel.
                                hiresMode = ((colourData & 0x08) == 0);
                                charData = charDataLatch;
                              
                                // We only next 1st pixel in this case. Hblanking about to start.
                                if (hiresMode) {
                                    if (non_reverse_mode != 0) {
                                        pixel1 = ((charData & 0x80) > 0? 2 : 0);
                                    } else {
                                        pixel1 = ((charData & 0x80) > 0? 0 : 2);
                                    }
                                } else {
                                    // Multicolour graphics.
                                    pixel1 = ((charData >> 6) & 0x03);
                                }
                              
                                // Look up foreground colour before outputting first pixel.
                                multiColourTable[2] = (colourData & 0x07);
                                
                                // The 4th pixel is partial before horiz blanking kicks in.
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_trunc_palette[multiColourTable[pixel1]]);
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel1]];
                                
                                fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                                break;
                          
                            case FETCH_CHAR_DATA:
                                // Look up latest background, border and auxiliary colours.
                                multiColourTable[0] = background_colour_index;
                                multiColourTable[1] = border_colour_index;
                                multiColourTable[3] = auxiliary_colour_index;
                                
                                // Output the three whole pixels.
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel2]]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel3]]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel4]]);
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel2]];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel3]];
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel4]];

                                // The 4th pixel is a partial pixel before horizontal blanking kicks in.
                                pio_sm_put(CVBS_PIO, CVBS_SM, pal_trunc_palette[multiColourTable[pixel5]]);
                                dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel5]];
                                
                                // If the matrix hasn't yet closed, then in the FETCH_CHAR_DATA 
                                // state, we need to keep incrementing the video matrix counter
                                // until it is closed, which at the latest could be HC=1 on the
                                // next line.
                                videoMatrixCounter++;
                                
                                fetchState = FETCH_SCREEN_CODE;
                                break;
                        }
                        
                        // After the 3.66 visible pixels, we now output the start of horiz blanking.
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_FRONTPORCH_1);
                        
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
                                }
                                if (horizontalCounter >= PAL_HBLANK_END) {
                                    // Output four border pixels.
                                    borderColour = border_colour_index;

                                    // Output only one visible border pixel for HC=12, as first three "pixels"
                                    // are part of the horizontal blanking. Note that the third one is due
                                    // to the switch delay in hblank turning off.
                                    if (horizontalCounter > PAL_HBLANK_END) {
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                    }
                                    pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                    dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                }
                                break;
                                
                            case FETCH_IN_MATRIX_Y:
                            case FETCH_MATRIX_LINE:
                                if (horizontalCounter >= PAL_HBLANK_END) {
                                    // Look up very latest background, border and auxiliary colour values. This
                                    // should not include an update to the foreground colour, as that will not 
                                    // have changed.
                                    multiColourTable[0] = background_colour_index;
                                    multiColourTable[1] = border_colour_index;
                                    multiColourTable[3] = auxiliary_colour_index;
                                    
                                    // Last three pixels of previous char data, or border pixels. 4th pixel always border.
                                    if (horizontalCounter > PAL_HBLANK_END) {
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel2]]);
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel3]]);
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel4]]);
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel2]];
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel3]];
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel4]];
                                    }
                                    pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel5]]);
                                    dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel5]];
                                    
                                    if (hiresMode) {
                                        if (non_reverse_mode != 0) {
                                            pixel6 = ((charData & 0x04) > 0? 2 : 0);
                                            pixel7 = ((charData & 0x02) > 0? 2 : 0);
                                            pixel8 = ((charData & 0x01) > 0? 2 : 0);
                                        } else {
                                            pixel6 = ((charData & 0x04) > 0? 0 : 2);
                                            pixel7 = ((charData & 0x02) > 0? 0 : 2);
                                            pixel8 = ((charData & 0x01) > 0? 0 : 2);
                                        }
                                    } else {
                                        // Multicolour graphics.
                                        pixel6 = ((charData >> 2) & 0x03);
                                        pixel7 = pixel8 = (charData & 0x03);
                                    }
                                  
                                    // Rotate pixels so that the other 3 remaining char pixels are output
                                    // and then border colours takes over after that.
                                    pixel2 = pixel6;
                                    pixel3 = pixel7;
                                    pixel4 = pixel8;
                                    pixel5 = pixel6 = pixel7 = pixel8 = pixel1 = 1;

                                    if (prevHorizontalCounter == screen_origin_x) {
                                        // Last 4 pixels before first char renders are still border.
                                        fetchState = FETCH_MATRIX_DLY_1;
                                    }
                                }
                                else if (prevHorizontalCounter == screen_origin_x) {
                                    // Still in horizontal blanking, but we still need to prepare for the case
                                    // where the next cycle isn't in horiz blanking, i.e. when HC=11 this cycle.
                                    fetchState = FETCH_MATRIX_DLY_1;
                                }
                                hiresMode = false;
                                colourData = 0x08;
                                charData = charDataLatch = 0x55;
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
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                    }
                                    pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[borderColour]);
                                    dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[borderColour];
                                }
                                else {
                                    pixel2 = pixel3 = pixel4 = pixel5 = pixel6 = pixel7 = pixel8 = 1;
                                }

                                // Prime the pixel output queue with border pixels in multicolour 
                                // mode. Not quite what the real chip does but is functionally equivalent.
                                hiresMode = false;
                                colourData = 0x08;
                                charDataLatch = 0x55;

                                fetchState++;
                                break;
                                
                            case FETCH_SCREEN_CODE:                                
                                 
                                // Look up very latest background, border and auxiliary colour values.
                                multiColourTable[0] = background_colour_index;
                                multiColourTable[1] = border_colour_index;
                                multiColourTable[3] = auxiliary_colour_index;
                            
                                // Output last 3 pixels of the last character. These had already left 
                                // the shift register but in the delay path to the colour lookup.
                                if (horizontalCounter > PAL_HBLANK_END) {
                                    // Note: These 3 pixels are not output for HC=12, as first three "pixels"
                                    // are part of the horizontal blanking. Note that the third one is due
                                    // to the switch delay in hblank turning off.
                                    pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel6]]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel7]]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel8]]);
                                    dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel6]];
                                    dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel7]];
                                    dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel8]];
                                }

                                // Update operating hires state and char data immediately prior to
                                // shifting out new character. Note that when we first enter this state,
                                // these variables are primed to initially output border pixels while 
                                // the process of fetching the first real character is taking place, 
                                // which happens over the first two cycles.
                                hiresMode = ((colourData & 0x08) == 0);
                                charData = charDataLatch;
                              
                                if (hiresMode) {
                                    if (non_reverse_mode != 0) {
                                        pixel1 = ((charData & 0x80) > 0? 2 : 0);
                                        pixel2 = ((charData & 0x40) > 0? 2 : 0);
                                        pixel3 = ((charData & 0x20) > 0? 2 : 0);
                                        pixel4 = ((charData & 0x10) > 0? 2 : 0);
                                        pixel5 = ((charData & 0x08) > 0? 2 : 0);
                                    } else {
                                        pixel1 = ((charData & 0x80) > 0? 0 : 2);
                                        pixel2 = ((charData & 0x40) > 0? 0 : 2);
                                        pixel3 = ((charData & 0x20) > 0? 0 : 2);
                                        pixel4 = ((charData & 0x10) > 0? 0 : 2);
                                        pixel5 = ((charData & 0x10) > 0? 0 : 2);
                                    }
                                } else {
                                    // Multicolour graphics.
                                    pixel1 = pixel2 = ((charData >> 6) & 0x03);
                                    pixel3 = pixel4 = ((charData >> 4) & 0x03);
                                    pixel5 = ((charData >> 2) & 0x03);
                                }
                              
                                // Look up foreground colour before outputting first pixel.
                                multiColourTable[2] = (colourData & 0x07);

                                // Calculate address within video memory and fetch cell index.
                                cellIndex = xram[screen_mem_start + videoMatrixCounter];

                                // Due to the way the colour memory is wired up, the above fetch of the cell index
                                // also happens to automatically fetch the foreground colour from the Colour Matrix
                                // via the top 4 lines of the data bus (DB8-DB11), which are wired directly from 
                                // colour RAM in to the VIC chip.
                                colourData = xram[colour_mem_start + videoMatrixCounter];

                                // Output the 1st pixel of next character. Note that this is not the character
                                // that relates to the cell index and colour data fetched above.
                                if (horizontalCounter >= PAL_HBLANK_END) {
                                    pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel1]]);
                                    dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel1]];
                                }

                                // Toggle fetch state. Close matrix if HCC hits zero.
                                fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                                break;
                                
                            case FETCH_CHAR_DATA:
                                // Look up very latest background, border and auxiliary colour values.
                                multiColourTable[0] = background_colour_index;
                                multiColourTable[1] = border_colour_index;
                                multiColourTable[3] = auxiliary_colour_index;
                                
                                if (horizontalCounter >= PAL_HBLANK_END) {
                                    // Output only one visible pixel for HC=12, as first three "pixels"
                                    // are part of the horizontal blanking. Note that the third one is due
                                    // to the switch delay in hblank turning off. This is why we skip these
                                    // pixels for HC=12.
                                    if (horizontalCounter > PAL_HBLANK_END) {
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel2]]);
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel3]]);
                                        pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel4]]);
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel2]];
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel3]];
                                        dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel4]];
                                    }

                                    pio_sm_put(CVBS_PIO, CVBS_SM, pal_palette[multiColourTable[pixel5]]);
                                    dvi_framebuf[dvi_line][dvi_pixel++] = pal_palette_rgb332[multiColourTable[pixel5]];
                                }
                                
                                // Calculate offset of data.
                                charDataOffset = char_mem_start + (cellIndex << char_size_shift) + cellDepthCounter;
                                
                                // Fetch cell data.  It can wrap around, which is why we & with 0x3FFF.
                                // Initially latched to the side until it is needed.
                                charDataLatch = xram[(charDataOffset & 0x3FFF)];

                                // Determine next character pixels.
                                if (hiresMode) {
                                    if (non_reverse_mode != 0) {
                                        pixel6 = ((charData & 0x04) > 0? 2 : 0);
                                        pixel7 = ((charData & 0x02) > 0? 2 : 0);
                                        pixel8 = ((charData & 0x01) > 0? 2 : 0);
                                    } else {
                                        pixel6 = ((charData & 0x04) > 0? 0 : 2);
                                        pixel7 = ((charData & 0x02) > 0? 0 : 2);
                                        pixel8 = ((charData & 0x01) > 0? 0 : 2);
                                    }
                                } else {
                                    // Multicolour graphics.
                                    pixel6 = ((charData >> 2) & 0x03);
                                    pixel7 = pixel8 = (charData & 0x03);
                                }
                                
                                // Increment the video matrix counter to next cell.
                                videoMatrixCounter++;
                                
                                // Toggle fetch state. For efficiency, HCC deliberately not checked here.
                                fetchState = FETCH_SCREEN_CODE;
                                break;
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
                            }
                            break;
                        case FETCH_IN_MATRIX_Y:
                        case FETCH_MATRIX_LINE:
                            if (horizontalCounter == screen_origin_x) {
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
        
        // DEBUG: Temporary check to see if we've overshot the 120 cycle allowance.
        if (pio_interrupt_get(VIC_PIO, 1)) {
            overruns = 1;
        }
    }
}
