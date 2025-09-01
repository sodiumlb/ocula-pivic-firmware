/*
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "cvbs_ntsc.h"
#include "vic/aud.h"
#include "vic/vic.h"
#include "vic/vic_ntsc.h"
#include "sys/dvi.h"
#include "sys/mem.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>

// Constants related to video timing for NTSC.
#define NTSC_HBLANK_END       9
#define NTSC_HBLANK_START     59
#define NTSC_LINE_END         64
#define NTSC_VBLANK_START     1
#define NTSC_VSYNC_START      4
#define NTSC_VSYNC_END        6
#define NTSC_VBLANK_END       9
#define NTSC_NORM_LAST_LINE   261
#define NTSC_INTL_LAST_LINE   262

static const uint8_t ntsc_palette_rgb332[16] = {
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
 * Core 1 entry function for NTSC 6560 VIC emulation.
 */
void vic_core1_loop_ntsc(void) {

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
    uint8_t  halfLineCounter = 0;        // 1-bit half-line counter
    uint16_t pixelCounter = 0; // DVI pixel counter
    uint16_t lineCounter = 0;

    // Values normally fetched externally, from screen mem, colour RAM and char mem.
    uint8_t  cellIndex = 0;              // 8 bits fetched from screen memory.
    uint8_t  charData = 0;               // 8 bits of bitmap data fetched from character memory.
    uint8_t  charDataLatch = 0;          // 8 bits of bitmap data fetched from character memory (latched)
    uint8_t  colourData = 0;             // 4 bits fetched from colour memory (top bit multi/hires mode)
    uint8_t  hiresMode = 0;

    // Holds the colour index for each of the current multi colour colours.
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

    // NTSC Palette data.
    uint8_t pIndex = 0;
    uint32_t palette[8][16] = {
        NTSC_BLACK,NTSC_WHITE,NTSC_RED_0,NTSC_CYAN_0,NTSC_PURPLE_0,NTSC_GREEN_0,NTSC_BLUE_0,NTSC_YELLOW_0,NTSC_ORANGE_0,NTSC_LORANGE_0,NTSC_PINK_0,NTSC_LCYAN_0,NTSC_LPURPLE_0,NTSC_LGREEN_0,NTSC_LBLUE_0,NTSC_LYELLOW_0,
        NTSC_BLACK,NTSC_WHITE,NTSC_RED_1,NTSC_CYAN_1,NTSC_PURPLE_1,NTSC_GREEN_1,NTSC_BLUE_1,NTSC_YELLOW_1,NTSC_ORANGE_1,NTSC_LORANGE_1,NTSC_PINK_1,NTSC_LCYAN_1,NTSC_LPURPLE_1,NTSC_LGREEN_1,NTSC_LBLUE_1,NTSC_LYELLOW_1,
        NTSC_BLACK,NTSC_WHITE,NTSC_RED_2,NTSC_CYAN_2,NTSC_PURPLE_2,NTSC_GREEN_2,NTSC_BLUE_2,NTSC_YELLOW_2,NTSC_ORANGE_2,NTSC_LORANGE_2,NTSC_PINK_2,NTSC_LCYAN_2,NTSC_LPURPLE_2,NTSC_LGREEN_2,NTSC_LBLUE_2,NTSC_LYELLOW_2,
        NTSC_BLACK,NTSC_WHITE,NTSC_RED_3,NTSC_CYAN_3,NTSC_PURPLE_3,NTSC_GREEN_3,NTSC_BLUE_3,NTSC_YELLOW_3,NTSC_ORANGE_3,NTSC_LORANGE_3,NTSC_PINK_3,NTSC_LCYAN_3,NTSC_LPURPLE_3,NTSC_LGREEN_3,NTSC_LBLUE_3,NTSC_LYELLOW_3,
        NTSC_BLACK,NTSC_WHITE,NTSC_RED_4,NTSC_CYAN_4,NTSC_PURPLE_4,NTSC_GREEN_4,NTSC_BLUE_4,NTSC_YELLOW_4,NTSC_ORANGE_4,NTSC_LORANGE_4,NTSC_PINK_4,NTSC_LCYAN_4,NTSC_LPURPLE_4,NTSC_LGREEN_4,NTSC_LBLUE_4,NTSC_LYELLOW_4,
        NTSC_BLACK,NTSC_WHITE,NTSC_RED_5,NTSC_CYAN_5,NTSC_PURPLE_5,NTSC_GREEN_5,NTSC_BLUE_5,NTSC_YELLOW_5,NTSC_ORANGE_5,NTSC_LORANGE_5,NTSC_PINK_5,NTSC_LCYAN_5,NTSC_LPURPLE_5,NTSC_LGREEN_5,NTSC_LBLUE_5,NTSC_LYELLOW_5,
        NTSC_BLACK,NTSC_WHITE,NTSC_RED_6,NTSC_CYAN_6,NTSC_PURPLE_6,NTSC_GREEN_6,NTSC_BLUE_6,NTSC_YELLOW_6,NTSC_ORANGE_6,NTSC_LORANGE_6,NTSC_PINK_6,NTSC_LCYAN_6,NTSC_LPURPLE_6,NTSC_LGREEN_6,NTSC_LBLUE_6,NTSC_LYELLOW_6,
        NTSC_BLACK,NTSC_WHITE,NTSC_RED_7,NTSC_CYAN_7,NTSC_PURPLE_7,NTSC_GREEN_7,NTSC_BLUE_7,NTSC_YELLOW_7,NTSC_ORANGE_7,NTSC_LORANGE_7,NTSC_PINK_7,NTSC_LCYAN_7,NTSC_LPURPLE_7,NTSC_LGREEN_7,NTSC_LBLUE_7,NTSC_LYELLOW_7,
    };

    // Optimisation to represent "in matrix", "address output enabled", and "pixel output enabled" 
    // all with one simple state variable. It might not be 100% accurate but should work for most 
    // cases.
    uint8_t fetchState = FETCH_OUTSIDE_MATRIX;
    
    // Due to the complexity of how the NTSC vertical blanking shifts depending on state, we keep 
    // track of the vblanking state in a separate boolean, so that we know when we should be writing 
    // out visible line content. It is complex to deduce it otherwise.
    bool vblanking = false;

    //
    // END OF VIC CHIP STATE
    //


    // Temporary variables, not a core part of the state.
    uint16_t charDataOffset = 0;

    // Index of the current border colour (used temporarily when we don't want to use the define multiple times in a cycle)
    uint8_t borderColourIndex = 0;

    //FIFO Back pressure. Experimentaly adjusted
    pio_sm_put(CVBS_PIO,CVBS_SM,CVBS_CMD_DC_RUN( 9,38)); 

    while (1) {
        // Poll for PIO IRQ 1. This is the rising edge of F1.
        while (!pio_interrupt_get(VIC_PIO, 1)) {
            tight_loop_contents();
        }
        
        // Clear the IRQ 1 flag immediately for now. 
        pio_interrupt_clear(VIC_PIO, 1);

        // VERTICAL TIMINGS:
        // The definition of a line is somewhat fuzzy in the NTSC 6560 chip.
        // The vertical counter (VC) increments partway through the visible part of the raster line (at HC=29)
        // and can reset at two different points along the raster line (HC=29 or HC=62) depending on the 
        // interlaced mode and half-line counter (HLC) states.
        // So, unlike the 6561 PAL chip, the VC and raster line are NOT equivalent in the 6560.
        // Also note that things like the vblank and vsync can start/end at two different HC values half a line 
        // apart (HC=29 or HC=62), once again depending on the interlaced mode and half-line counter states.
        // Given that, then documenting what lines are vblank, vsync and visible is a little complex, as they
        // shift depending on state, and can span multiple vertical counter values. 
        // The code is the source of truth in that regard.

        // HORIZONTAL TIMINGS:
        // The horizontal timings for the NTSC 6560 are also quite strange compared to the PAL 6561.
        // There are 65 cycles per "line" (see above for comments on the obscure nature of what a line is)
        // The horizontal counter (HC) continously counts from 0 to 64, then resets back to 0.
        // 15 cycles for horizontal blanking, between HC=58.5 and HC=8.5.
        // - 4 cycles of front porch [58.5 -> 62.5]
        // - 5 cycles of hsync [62.5 -> 2.5]
        // - 0.5 cycles of breezeway [2.5 -> 3]
        // - 5 cycles colour burst [3 -> 8]
        // - 0.5 back porch [8 -> 8.5]
        // 50 cycles for visible pixels, making 200 visible pixels total [8.5 -> 58.5]
        //
        // The following are some events of note that happen for certain HC (horizontal counter) values:
        // 1: New line logic. Same as PAL.
        // 2: Vertical Cell Counter (VCC) reloaded if VC=0. Same as PAL.
        // 9: Start of visible pixels. Technically they start halfway into the cycle.
        // 29: Increments VC and HLC (half-line counter). Resets VC every second field for interlaced.
        // 59: Horiz blanking starts, for visible lines.
        // 62: Increments half-line counter. Resets VC if non-interlaced, or every second field for interlaced.
        // 64: Resets HC.

        switch (horizontalCounter) {
      
            // HC = 0. The main reason for this having its own special case block is due to the special
            // handling for screen origin X matching when HC=0.
            case 0: 
              
                // Reset pixel output buffer to be all border colour.
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
                        // NOTE: Due to comparison being prev HC, this is matching HC=64.
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
                lineCounter = verticalCounter;

                prevHorizontalCounter = horizontalCounter++;
                break;
        
            // HC = 1 is another special case, handled in a single block for ALL lines. This
            // is when the "new line" signal is seen by most components.
            case 1:

                // Due to the "new line" signal being generated by the Horizontal Counter Reset
                // logic, and the pass transistors used within it delaying the propagation of 
                // that signal, this signal doesn't get seen by components such as the Cell Depth
                // Counter Reset logic, the "In Matrix" status logic, and Video Matrix Latch, 
                // until HC = 1.
              
                // The "new line" signal closes the matrix, if it is still open.
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
                    // If fetchState is already FETCH_IN_MATRIX_Y at this point, it means that the
                    // last line matched the screen origin Y but not X. This results in the
                    // matrix being rendered one line lower if X now matches, as per real chip.
                    if (prevHorizontalCounter == screen_origin_x) {
                        fetchState = FETCH_IN_MATRIX_X;
                    }
                }
                else if (fetchState == FETCH_OUTSIDE_MATRIX) {
                    if ((verticalCounter >> 1) == screen_origin_y) {
                        // This is the line the video matrix starts on. As in the real chip, we use
                        // a different state for the first part of the first video matrix line.
                        fetchState = FETCH_IN_MATRIX_Y;
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
            // lines. This is when the vertical cell counter is loaded.
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
                // TODO: This probably needs to move for the 6560, as VC doesn't reset in HC=1.
                if (verticalCounter == 0) {
                    verticalCellCounter = num_of_rows;
                }
                
                prevHorizontalCounter = horizontalCounter++;
                break;

            // HC = 62 is one of the two increment points for the 1/2 line counter. It is also when 
            // the vertical counter resets when in non-interlaced mode, and for interlaced mode where 
            // it resets every second field. It is also one of two points where vertical blanking
            // and vertical sync can start and end.
            case 62:
                if (interlaced_mode) {
                    if ((verticalCounter == NTSC_INTL_LAST_LINE) && !halfLineCounter) {
                        // For interlaced mode, the vertical counter resets here every second field, as
                        // controlled by the half-line counter.
                        verticalCounter = 0;
                        fetchState = FETCH_OUTSIDE_MATRIX;
                        cellDepthCounter = 0;
                        halfLineCounter = 0;

                        // Update raster line CR value to be 0.
                        vic_cr4 = 0;
                        vic_cr3 &= 0x7F;
                    } else {
                        // Half line counter simply toggles between 0 and 1.
                        halfLineCounter ^= 1;
                    }
                }
                else {
                    if (verticalCounter == NTSC_NORM_LAST_LINE) {
                        // For non-interlaced mode, the vertical counter always resets at this point.
                        verticalCounter = 0;
                        fetchState = FETCH_OUTSIDE_MATRIX;
                        cellDepthCounter = 0;
                        halfLineCounter = 0;

                        // Update raster line CR value to be 0.
                        vic_cr4 = 0;
                        vic_cr3 &= 0x7F;
                    } else {
                        // Half line counter simply toggles between 0 and 1.
                        halfLineCounter ^= 1;
                    }
                }

                // Output vertical blanking or vsync, if required. If the half-line counter is 1, then 
                // vblank and vsync get delayed by half a line, i.e. to HC=29. 
                if (!halfLineCounter) {
                    if ((verticalCounter > 0) && (verticalCounter <= NTSC_VBLANK_END)) {
                        // Vertical blanking and sync - Lines 1-9.
                        vblanking = true;

                        if (verticalCounter < NTSC_VSYNC_START) {
                            // Lines 1, 2, 3.
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_H);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_H);
                        }
                        else if (verticalCounter <= NTSC_VSYNC_END) {
                            // Vertical sync, lines 4, 5, 6.
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_LONG_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_LONG_SYNC_H);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_LONG_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_LONG_SYNC_H);

                            // Vertical sync is what resets the video matrix latch.
                            videoMatrixLatch = videoMatrixCounter = 0;
                        }
                        else {
                            // Lines 7, 8, 9.
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_H);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_H);
                        }
                    }
                    else {
                        vblanking = false;
                    }
                }

                // If we're not in vertical blanking, i.e. we didn't output the CVBS commands above, 
                // then we continue horizontal blanking commands instead, including hsync and colour 
                // burst. It will end at HC=8.5
                if (!vblanking) {
                    // TODO: How does colour burst work when vblanking finishes halfway through line?
                    pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_FRONTPORCH_2);
                    pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_HSYNC);
                    pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_BREEZEWAY);
                    if (verticalCounter & 1) {
                        // Odd line. Switch palette starting offset.
                        pIndex = 2;
                        pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_COLBURST_O);
                    } else {
                        // Even line. Switch palette starting offset.
                        pIndex = 6;
                        pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_COLBURST_E);
                    }
                    pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_BACKPORCH);
                }

                //
                // IMPORTANT: THE HC=62 CASE STATEMENT DELIBERATELY FALLS THROUGH TO NEXT BLOCK.
                //

            // These HC values are always in blanking and have no special behaviour other than
            // the standard state changes common to all cycles.
            case 60:
            case 61:
            case 63:
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
            
            // HC = 64 is the last HC value, so triggers HC reset.
            case 64:
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
                    case FETCH_SCREEN_CODE:
                        fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                        break;
                    case FETCH_CHAR_DATA:
                        videoMatrixCounter++;
                        fetchState = FETCH_SCREEN_CODE;
                        break;
                } 

                // And then reset HC.
                prevHorizontalCounter = horizontalCounter;
                horizontalCounter = 0;
                pixelCounter = 0;
                break;

            // HC=29 is when the 6560 increments the vertical counter (VC). The 1/2 line counter also
            // toggles at this time. This is therefore the end of the raster line as reported by
            // the VIC registers, but is not the actual end of the raster, as that happens when the 
            // hsync occurs at HC=62.
            case 29:
                // NOTE: The VC always resets at HC=62 when in non-interlaced mode, but for interlaced,
                // it can reset in HC=29 as controlled by the half-line counter.
                if (interlaced_mode && (verticalCounter == NTSC_INTL_LAST_LINE) && !halfLineCounter) {
                    // For interlaced mode, the vertical counter resets every second field at HC=29.
                    verticalCounter = 0;
                    fetchState = FETCH_OUTSIDE_MATRIX;
                    cellDepthCounter = 0;
                    halfLineCounter = 0;
                } else {
                    // Otherwise increment vertical counter.
                    verticalCounter++;

                    // Half line counter simply toggles between 0 and 1.
                    halfLineCounter ^= 1;
                }

                // Update the raster line value stored in the VIC registers. Note that this is
                // correct for NTSC, i.e. the VIC control registers for the raster value do 
                // change at HC=29 (not at HC=1 like PAL does). It can also change at HC=62,
                // if the VC is reset to 0 during that cycle.
                vic_cr4 = (verticalCounter >> 1);
                if ((verticalCounter & 0x01) == 0) {
                    vic_cr3 &= 0x7F;
                } else {
                    vic_cr3 |= 0x80;
                }

                // Output vertical blanking or vsync, if required. If the half-line counter is 1, then 
                // vblank and vsync get delayed by half a line, i.e. to HC=62. 
                if (!halfLineCounter) {
                    if ((verticalCounter > 0) && (verticalCounter <= NTSC_VBLANK_END)) {
                        // Vertical blanking and sync - Lines 1-9.
                        vblanking = true;

                        if (verticalCounter < NTSC_VSYNC_START) {
                            // Lines 1, 2, 3.
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_H);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_H);
                        }
                        else if (verticalCounter <= NTSC_VSYNC_END) {
                            // Vertical sync, lines 4, 5, 6.
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_LONG_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_LONG_SYNC_H);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_LONG_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_LONG_SYNC_H);

                            // Vertical sync is what resets the video matrix latch.
                            videoMatrixLatch = videoMatrixCounter = 0;
                        }
                        else {
                            // Lines 7, 8, 9.
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_H);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_L);
                            pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_SHORT_SYNC_H);
                        }
                    }
                    else {
                        vblanking = false;
                    }
                }

                //
                // IMPORTANT: THE HC=29 CASE STATEMENT DELIBERATELY FALLS THROUGH TO THE DEFAULT BLOCK.
                //

            // Covers from HC=3 to HC=58.
            default:
                // Line 0, and Lines after 9, are "visible", i.e. not within the vertical blanking.
                if (!vblanking) {
                    // Is the visible part of the line ending now and horizontal blanking starting?
                    if (horizontalCounter == NTSC_HBLANK_START) {
                        // Horizontal blanking starts here. Simplified state changes, so its just the bare minimum.
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
                                
                            case FETCH_SCREEN_CODE:
                                fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                                break;
                          
                            case FETCH_CHAR_DATA:
                                // If the matrix hasn't yet closed, then in the FETCH_CHAR_DATA 
                                // state, we need to keep incrementing the video matrix counter
                                // until it is closed, which at the latest could be HC=1 on the
                                // next line.
                                videoMatrixCounter++;
                                
                                fetchState = FETCH_SCREEN_CODE;
                                break;
                        }
                        
                        // We output the start of horiz blanking here, enough of it to last to the start
                        // of HC=62, where a decision is then made as to whether it will be horizontal
                        // blanking or vertical blanking. This is why there is a part 1 and 2 of the front
                        // porch.
                        pio_sm_put(CVBS_PIO, CVBS_SM, NTSC_FRONTPORCH_1);
                        
                        // Unlike PAL, for NTSC hblank starts 6 cycles before the HC reset, so we increment.
                        prevHorizontalCounter = horizontalCounter++;
                    }
                    else {
                        // Covers visible line cycles from HC=4 to HC=57, i.e. 1 cycle before HBLANK start.
                        switch (fetchState) {
                            case FETCH_OUTSIDE_MATRIX:
                                if ((verticalCounter >> 1) == screen_origin_y) {
                                    // This is the line the video matrix starts on. As in the real chip, we use
                                    // a different state for the first part of the first video matrix line.
                                    fetchState = FETCH_IN_MATRIX_Y;
                                }
                                if (horizontalCounter >= NTSC_HBLANK_END) {
                                    borderColourIndex = border_colour_index;
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][borderColourIndex]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][borderColourIndex]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][borderColourIndex]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][borderColourIndex]);
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[borderColourIndex];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[borderColourIndex];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[borderColourIndex];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[borderColourIndex];
                                }
                                // Nothing to do otherwise. Still in blanking if below 12.
                                break;
                                
                            case FETCH_IN_MATRIX_Y:
                            case FETCH_MATRIX_LINE:
                                if (horizontalCounter >= NTSC_HBLANK_END) {
                                    // Look up very latest background, border and auxiliary colour values. This
                                    // should not include an update to the foreground colour, as that will not 
                                    // have changed.
                                    multiColourTable[0] = background_colour_index;
                                    multiColourTable[1] = border_colour_index;
                                    multiColourTable[3] = auxiliary_colour_index;
                                    
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel2]]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel3]]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel4]]);
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel2]];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel3]];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel4]];
                                    
                                    if (hiresMode) {
                                        if (non_reverse_mode != 0) {
                                            pixel5 = ((charData & 0x08) > 0? 2 : 0);
                                            pixel6 = ((charData & 0x04) > 0? 2 : 0);
                                            pixel7 = ((charData & 0x02) > 0? 2 : 0);
                                            pixel8 = ((charData & 0x01) > 0? 2 : 0);
                                        } else {
                                            pixel5 = ((charData & 0x08) > 0? 0 : 2);
                                            pixel6 = ((charData & 0x04) > 0? 0 : 2);
                                            pixel7 = ((charData & 0x02) > 0? 0 : 2);
                                            pixel8 = ((charData & 0x01) > 0? 0 : 2);
                                        }
                                    } else {
                                        // Multicolour graphics.
                                        pixel5 = pixel6 = ((charData >> 2) & 0x03);
                                        pixel7 = pixel8 = (charData & 0x03);
                                    }

                                    // Pixel 5 has to be output after the pixel var calculations above.
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel5]]);
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel5]];
                                  
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
                                    // where the next cycle isn't in horiz blanking, i.e. when HC=7 this cycle.
                                    fetchState = FETCH_MATRIX_DLY_1;
                                }
                                
                                hiresMode = false;
                                colourData = 0x08;
                                charData = charDataLatch = 0x55;
                                break;
                                
                            case FETCH_MATRIX_DLY_1:
                            case FETCH_MATRIX_DLY_2:
                            case FETCH_MATRIX_DLY_3:
                                if (horizontalCounter >= NTSC_HBLANK_END) {
                                    // Output border pixels.
                                    borderColourIndex = border_colour_index;
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][borderColourIndex]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][borderColourIndex]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][borderColourIndex]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][borderColourIndex]);
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[borderColourIndex];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[borderColourIndex];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[borderColourIndex];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[borderColourIndex];
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
                                if (horizontalCounter >= NTSC_HBLANK_END) {
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel6]]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel7]]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel8]]);
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel6]];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel7]];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel8]];
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
                                    } else {
                                        pixel1 = ((charData & 0x80) > 0? 0 : 2);
                                        pixel2 = ((charData & 0x40) > 0? 0 : 2);
                                        pixel3 = ((charData & 0x20) > 0? 0 : 2);
                                        pixel4 = ((charData & 0x10) > 0? 0 : 2);
                                    }
                                } else {
                                    // Multicolour graphics.
                                    pixel1 = pixel2 = ((charData >> 6) & 0x03);
                                    pixel3 = pixel4 = ((charData >> 4) & 0x03);
                                }
                              
                                // Look up foreground colour before outputting first pixel.
                                multiColourTable[2] = (colourData & 0x07);

                                // Calculate address within video memory and fetch cell index.
                                //Assuming 0x0---, 0x3---- and 0x20-- as connected address space
                                uint16_t screen_addr = screen_mem_start + videoMatrixCounter;
                                switch((screen_addr >> 10) & 0xF){
                                    case  4 ... 7:
                                    case  9 ... 11:
                                        cellIndex = XUNCON_REG;
                                        break;
                                    default:
                                        cellIndex = xram[screen_addr];
                                        break;
                                }

                                // Due to the way the colour memory is wired up, the above fetch of the cell index
                                // also happens to automatically fetch the foreground colour from the Colour Matrix
                                // via the top 4 lines of the data bus (DB8-DB11), which are wired directly from 
                                // colour RAM in to the VIC chip.
                                colourData = xram[colour_mem_start + videoMatrixCounter];

                                // Output the 1st pixel of next character. Note that this is not the character
                                // that relates to the cell index and colour data fetched above.
                                if (horizontalCounter >= NTSC_HBLANK_END) {
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel1]]);
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel1]];
                                }

                                // Toggle fetch state. Close matrix if HCC hits zero.
                                fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                                break;
                                
                            case FETCH_CHAR_DATA:
                                // Look up very latest background, border and auxiliary colour values.
                                multiColourTable[0] = background_colour_index;
                                multiColourTable[1] = border_colour_index;
                                multiColourTable[3] = auxiliary_colour_index;
                                
                                if (horizontalCounter >= NTSC_HBLANK_END) {
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel2]]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel3]]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel4]]);
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel2]];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel3]];
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel4]];
                                }
                                
                                // Calculate offset of data.
                                charDataOffset = char_mem_start + (cellIndex << char_size_shift) + cellDepthCounter;
                                
                                // Fetch cell data.  It can wrap around, which is why we & with 0x3FFF.
                                // Initially latched to the side until it is needed.
                                //Assuming 0x0---, 0x3---- and 0x20-- as connected address space
                                switch((charDataOffset >> 10) & 0xF ){
                                    case  4 ... 7:
                                    case  9 ... 11:
                                         charDataLatch = XUNCON_REG;
                                         break;
                                    default:
                                         charDataLatch = xram[(charDataOffset & 0x3FFF)];
                                         break;
                                }
                                
                                // Determine next character pixels.
                                if (hiresMode) {
                                    if (non_reverse_mode != 0) {
                                        pixel5 = ((charData & 0x08) > 0? 2 : 0);
                                        pixel6 = ((charData & 0x04) > 0? 2 : 0);
                                        pixel7 = ((charData & 0x02) > 0? 2 : 0);
                                        pixel8 = ((charData & 0x01) > 0? 2 : 0);
                                    } else {
                                        pixel5 = ((charData & 0x08) > 0? 0 : 2);
                                        pixel6 = ((charData & 0x04) > 0? 0 : 2);
                                        pixel7 = ((charData & 0x02) > 0? 0 : 2);
                                        pixel8 = ((charData & 0x01) > 0? 0 : 2);
                                    }
                                } else {
                                    // Multicolour graphics.
                                    pixel5 = pixel6 = ((charData >> 2) & 0x03);
                                    pixel7 = pixel8 = (charData & 0x03);
                                }
                                
                                if (horizontalCounter >= NTSC_HBLANK_END) {
                                    pio_sm_put(CVBS_PIO, CVBS_SM, palette[(pIndex++ & 0x7)][multiColourTable[pixel5]]);
                                    dvi_framebuf[lineCounter][pixelCounter++] = ntsc_palette_rgb332[multiColourTable[pixel5]];
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
                    // either HC=29 or HC=62. In case the screen origin Y is set within the vertical blanking
                    // lines, we still need to update the fetch state, video matrix counter, and the horizontal
                    // cell counter, even though we're not outputting character pixels. So for the rest of the 
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
    
                    // The horizontal counter reset always happens within the top level case 64 statement, so
                    // we only need to cater for HC increments here.
                    prevHorizontalCounter = horizontalCounter++;
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
