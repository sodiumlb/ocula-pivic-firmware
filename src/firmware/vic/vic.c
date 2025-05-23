/*
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "vic/aud.h"
#include "vic/vic.h"
#include "vic/vic_ntsc.h"
#include "vic/char_rom.h"
#include "vic/cvbs.h"
#include "vic/cvbs_ntsc.h"
#include "vic/cvbs_pal.h"
#include "sys/cfg.h"
#include "sys/mem.h"
#include "vic.pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>


// These are the actual VIC 20 memory addresses, but note that the VIC chip sees memory a bit differently.
// $8000-$83FF: 1 KB uppercase/glyphs
// $8400-$87FF: 1 KB uppercase/lowercase
// $8800-$8BFF: 1 KB inverse uppercase/glyphs
// $8C00-$8FFF: 1 KB inverse uppercase/lowercase
// From the VIC chip's perspective, the following are the corresponding addresses:
#define ADDR_UPPERCASE_GLYPHS_CHRSET            0x0000
#define ADDR_UPPERCASE_LOWERCASE_CHRSET         0x0400
#define ADDR_INVERSE_UPPERCASE_GLYPHS_CHRSET    0x0800
#define ADDR_INVERSE_UPPERCASE_LOWERCASE_CHRSET 0x0C00

// These are the 'standard' screen memory locations in the VIC 20 memory map, but in practice the 
// programmer can set screen memory to several other locations.
#define ADDR_UNEXPANDED_SCR  0x3E00    // Equivalent to $1E00 in the VIC 20.
#define ADDR_8KPLUS_EXP_SCR  0x3000    // Equivalent to $1000 in the VIC 20.

// Colour RAM addresses.
#define ADDR_COLOUR_RAM      0x1600    // Equivalent to $9600 in the VIC 20
#define ADDR_ALT_COLOUR_RAM  0x1400    // Equivalent to $9400 in the VIC 20

// Constants related to video timing for PAL and NTSC.
#define PAL_HBLANK_END        11
#define PAL_HBLANK_START      70
#define PAL_VBLANK_START      1
#define PAL_VSYNC_START       4
#define PAL_VSYNC_END         6
#define PAL_VBLANK_END        9
#define PAL_LAST_LINE         311

//Colour command defines in cvbs_pal.h
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


volatile uint32_t overruns = 0;


void vic_pio_init(void) {
    //Make PHI PIN possible to also sample as input
    gpio_init(PHI_PIN);
    gpio_set_input_enabled(PHI_PIN, true);

    // Set up VIC PIO.
    pio_set_gpio_base(VIC_PIO, VIC_PIN_OFFS);
    // TODO: We might add the second output clock in the future.
    pio_gpio_init(VIC_PIO, VIC_PIN_BASE);
    // TODO: Check if the drive strength is appropriate for the clock.
    gpio_set_drive_strength(VIC_PIN_BASE, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(VIC_PIO, VIC_SM, VIC_PIN_BASE, 1, true);
    uint offset;
    pio_sm_config config;
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
            offset = pio_add_program(VIC_PIO, &clkgen_ntsc_program);
            config = clkgen_ntsc_program_get_default_config(offset);
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
            offset = pio_add_program(VIC_PIO, &clkgen_pal_program);
            config = clkgen_pal_program_get_default_config(offset);
            break;
        }
    sm_config_set_sideset_pin_base(&config, VIC_PIN_BASE);
    pio_sm_init(VIC_PIO, VIC_SM, offset, &config);
    pio_sm_set_enabled(VIC_PIO, VIC_SM, true);   
    printf("VIC init done\n");
}

void vic_memory_init() {
    // Load the VIC 20 Character ROM into internal PIVIC RAM.
    memcpy((void*)(&xram[ADDR_UPPERCASE_GLYPHS_CHRSET]), (void*)vic_char_rom, sizeof(vic_char_rom));

    // Set up hard coded control registers for now (from default PAL VIC).
    //xram[0x1000] = 0x0C;    // Screen Origin X = 12 (PAL)
    xram[0x1000] = 0x05;    // Screen Origin X = 5 (NTSC)
    //xram[0x1001] = 0x26;    // Screen Origin Y = 38 (PAL)
    xram[0x1001] = 0x19;    // Screen Origin Y = 25 (NTSC)
    xram[0x1002] = 0x96;    // Number of Columns = 22 (bits 0-6) Video Mem Start (bit 7)
    xram[0x1003] = 0x2E;    // Number of Rows = 23 (bits 1-6)
    xram[0x1005] = 0xF0;    // Video Mem Start = 0x3E00 (bits 4-7), Char Mem Start = 0x0000 (bits 0-3)
    xram[0x100e] = 0;       // Black auxiliary colour (bits 4-7)
    xram[0x100f] = 0x1B;    // White background (bits 4-7), Cyan border (bits 0-2), Reverse OFF (bit 3)

    // Set up a test page for the screen memory.
    memset((void*)&xram[ADDR_UNEXPANDED_SCR], 0x20, 1024);

    // First row - Reserved for name etc.
    xram[ADDR_UNEXPANDED_SCR + 0] = 16;  // P
    xram[ADDR_UNEXPANDED_SCR + 1] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 2] = 22;  // V
    xram[ADDR_UNEXPANDED_SCR + 3] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 4] = 3;   // C
    xram[ADDR_COLOUR_RAM + 0] = 2;
    xram[ADDR_COLOUR_RAM + 1] = 3;
    xram[ADDR_COLOUR_RAM + 2] = 4;
    xram[ADDR_COLOUR_RAM + 3] = 5;
    xram[ADDR_COLOUR_RAM + 4] = 6;

    // Add some coloured blocks to the end of the first line.
    int i;
    for (i=6; i<14; i++) {
        xram[ADDR_UNEXPANDED_SCR + i] = 0x66;
        xram[ADDR_UNEXPANDED_SCR + i + 8] = 0xA0;
        xram[ADDR_COLOUR_RAM + i] = (i - 6);
        xram[ADDR_COLOUR_RAM + i + 8] = (i - 6);
    }

    // Second row onwards - Test characters
    for (i=0; i<256; i++) {
        xram[ADDR_UNEXPANDED_SCR + i + 22] = (i & 0xFF);
        xram[ADDR_COLOUR_RAM + i + 22] = (i & 0x07);
    }
    for (i=256; i<484; i++) {
        xram[ADDR_UNEXPANDED_SCR + i + 22] = (i & 0xFF);
        xram[ADDR_COLOUR_RAM + i + 22] = ((i & 0x07) | 0x08);
    }

    // Last line.
    xram[ADDR_UNEXPANDED_SCR + 484 + 0] = 16;  // P
    xram[ADDR_UNEXPANDED_SCR + 484 + 1] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 484 + 2] = 22;  // V
    xram[ADDR_UNEXPANDED_SCR + 484 + 3] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 484 + 4] = 3;   // C
    xram[ADDR_COLOUR_RAM + 484 + 0] = 2;
    xram[ADDR_COLOUR_RAM + 484 + 1] = 3;
    xram[ADDR_COLOUR_RAM + 484 + 2] = 4;
    xram[ADDR_COLOUR_RAM + 484 + 3] = 5;
    xram[ADDR_COLOUR_RAM + 484 + 4] = 6;
}

void vic_core1_loop_pal(void) {


    //
    // START OF VIC CHIP STATE
    //

    // Counters.
    uint16_t videoMatrixCounter = 0;     // 12-bit video matrix counter (VMC)
    uint16_t videoMatrixLatch = 0;       // 12-bit latch that VMC is stored to and loaded from
    uint16_t verticalCounter = 0;        // 9-bit vertical counter (i.e. raster lines)
    uint8_t  horizontalCounter = 0;      // 8-bit horizontal counter (although top bit isn't used)
    uint8_t  horizontalCellCounter = 0;  // 8-bit horizontal cell counter (down counter)
    uint8_t  verticalCellCounter = 0;    // 6-bit vertical cell counter (down counter)
    uint8_t  cellDepthCounter = 0;       // 4-bit cell depth counter (counts either from 0-7, or 0-15)

    // Values normally fetched externally, from screen mem, colour RAM and char mem.
    uint8_t  cellIndex = 0;              // 8 bits fetched from screen memory.
    uint8_t  charData = 0;               // 8 bits of bitmap data fetched from character memory.
    uint8_t  colourData = 0;             // 4 bits fetched from colour memory (top bit multi/hires mode)

    // CVBS command for the current border colour.
    uint32_t borderColour = 0;

    // Holds the colour commands for each of the current multi colour colours.
    uint32_t multiColourTable[4] = { 0, 0, 0, 0};

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

    // Optimisation to represent "in matrix", "address output enabled", and "pixel output enabled" 
    // all with one simple state variable. It might not be 100% accurate but should work for most 
    // cases.
    uint8_t fetchState = FETCH_OUTSIDE_MATRIX;
    
    //
    // END OF VIC CHIP STATE
    //


    // Temporary variables, not a core part of the state.
    uint16_t charDataOffset = 0;

    // Slight hack so that VC increments to 0 on first iteration.
    // TODO: After adding new loop code, check if this is still required.
    verticalCounter = 0xFFFF;

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
      
            // HC = 0 is handled in a single block for ALL lines. This is the cycle during
            // which we queue the horiz blanking, horiz sync, colour burst, vertical blanking
            // and vsync, all up front for efficiency reasons.
            case 0: 
              
                // Reset pixel output buffer to be all border colour at start of line.
                pixel1 = pixel2 = pixel3 = pixel4 = pixel5 = pixel6 = pixel7 = pixel8 = 1;
              
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

                // Simplified state updates for HC=0. Counters and states still need to 
                // change as appropriate, regardless of it being during blanking.
                switch (fetchState) {
                    case FETCH_OUTSIDE_MATRIX:
                        if (horizontalCounter == screen_origin_x) {
                            // This registers the match with HC=0 in case screen origin Y 
                            // matches in the next cycle.
                            fetchState = FETCH_IN_MATRIX_X;
                        }
                        break;
                    case FETCH_IN_MATRIX_Y:
                    case FETCH_MATRIX_LINE:
                        if (horizontalCounter == screen_origin_x) {
                            // Last line was in the matrix, so start the in matrix delay.
                            fetchState = FETCH_MATRIX_DLY_1;
                        }
                        break;
                    case FETCH_MATRIX_DLY_1:
                    case FETCH_MATRIX_DLY_2:
                    case FETCH_MATRIX_DLY_3:
                    case FETCH_MATRIX_DLY_4:
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

                if ((verticalCounter == 0) || (verticalCounter > PAL_VBLANK_END)) {
                    // In HC=0 for visible lines, we start with output the full sequence of CVBS
                    // commands for horizontal blanking, including the hsync and colour burst.
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_FRONTPORCH_2);
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_HSYNC);
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BREEZEWAY);
                    if (verticalCounter & 1) {
                        // Odd line. Switch colour palettes.
                        pal_palette = pal_palette_o;
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_COLBURST_O);
                    } else {
                        // Even line. Switch colour palettes.
                        pal_palette = pal_palette_e;
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

                horizontalCounter++;
                break;
        
            // HC = 1 is another special case, handled in a single block for ALL lines. This
            // is when the "new line" signal is seen by most components.
            case 1:
          
                // Due to the "new line" signal being generated by the Horizontal Counter Reset
                // logic, and the pass transistors used within it delaying the propagation of 
                // that signal, this signal doesn't get seen by components such as the Cell Depth
                // Counter Reset logic, the "In Matrix" status logic, and Video Matrix Latch 
                // until HC = 1.
              
                // The "new line" signal closes the matrix, if it is still open.
                if (fetchState >= FETCH_SCREEN_CODE) {
                    fetchState = FETCH_MATRIX_LINE;
                }
              
                // Check for Cell Depth Counter reset.
                if (cellDepthCounter == last_line_of_cell) {
                    // Reset CDC.
                    cellDepthCounter = 0;
                    
                    // If last line was the last line of the character cell, then we latch
                    // the current VMC value ready for the next character row.
                    videoMatrixLatch = videoMatrixCounter;
                    
                    // Vertical Cell Counter decrements when CDC resets, unless its the first line, 
                    // since it was loaded instead (see VC reset logic in HC=2).
                    if (verticalCounter > 0) {
                        verticalCellCounter--;
    
                        if (verticalCellCounter == 0) {
                            // If all text rows rendered, then we're outside the matrix again.
                            fetchState = FETCH_OUTSIDE_MATRIX;
                        }
                    }
                }
                else if (fetchState >= FETCH_MATRIX_LINE) {
                    // If the line that just ended was a video matrix line, then increment CDC.
                    cellDepthCounter++;
                }
                else if (verticalCounter == screen_origin_y) {
                    // This is the line the video matrix starts on. As in the real chip, we use
                    // a different state for the first part of the first video matrix line.
                    if (fetchState == FETCH_IN_MATRIX_X) {
                        fetchState = FETCH_MATRIX_DLY_2;
                    } else {
                        fetchState = FETCH_IN_MATRIX_Y;
                    }
                }
              
                // Reset screen origin X match from HC=0 if screen origin Y didn't match above.
                if (fetchState == FETCH_IN_MATRIX_X) {
                    fetchState = FETCH_OUTSIDE_MATRIX;
                }
                // Covers special case when Screen Origin X is set to 1.
                else if ((fetchState > FETCH_OUTSIDE_MATRIX) && (horizontalCounter == screen_origin_x)) {
                    fetchState = FETCH_MATRIX_DLY_1;
                }
                else if (fetchState == FETCH_MATRIX_DLY_1) {
                    fetchState++;
                }
              
                // Horizontal Cell Counter (HCC) is reloaded on "new line" signal.
                horizontalCellCounter = num_of_columns;
              
                // Video Matrix Counter (VMC) is reloaded from latch on "new line" signal.
                videoMatrixCounter = videoMatrixLatch;
                
                horizontalCounter++;
                break;
            
            // HC = 2 is yet another special case, handled in a single block for ALL 
            // lines. This is when the vertical cell counter is loaded.
            case 2:
              
                // Simplified state changes. We're in hblank, so its just the bare minimum.
                switch (fetchState) {
                    case FETCH_OUTSIDE_MATRIX:
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
                    case FETCH_MATRIX_DLY_4:
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
              
                // Vertical Cell Counter is loaded at 2 cycles into Line 0.
                if (verticalCounter == 0) {
                    verticalCellCounter = num_of_rows;
                }
                
                horizontalCounter++;
                break;

            // Covers HC=3 and above, up to HC=HBLANKSTART (e.g. HC=70 for PAL)
            default:

                // Line 0, and Lines after 9, are "visible", i.e. not within the vertical blanking.
                if ((verticalCounter == 0) || (verticalCounter > PAL_VBLANK_END)) {
                    // Is the visible part of the line ending now and horizontal blanking starting?
                    if (horizontalCounter == PAL_HBLANK_START) {
                        // Horizontal blanking doesn't start until 2 pixels in. What exactly those
                        // two pixels are depends on the fetch state.
                        switch (fetchState) {
                            case FETCH_OUTSIDE_MATRIX:
                            case FETCH_IN_MATRIX_Y:
                            case FETCH_MATRIX_LINE:
                                borderColour = pal_palette[border_colour_index];
                                pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                break;
                                
                            case FETCH_MATRIX_DLY_1:
                            case FETCH_MATRIX_DLY_2:
                            case FETCH_MATRIX_DLY_3:
                            case FETCH_MATRIX_DLY_4:
                                borderColour = pal_palette[border_colour_index];
                                pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                fetchState++;
                                break;
                                
                            case FETCH_SCREEN_CODE:
                                // Look up latest background, border and auxiliary colours.
                                multiColourTable[0] = pal_palette[background_colour_index];
                                multiColourTable[1] = pal_palette[border_colour_index];
                                multiColourTable[3] = pal_palette[auxiliary_colour_index];
                                
                                pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel2]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel3]);
                                
                                fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                                break;
                          
                            case FETCH_CHAR_DATA:
                                // Look up latest background, border and auxiliary colours.
                                multiColourTable[0] = pal_palette[background_colour_index];
                                multiColourTable[1] = pal_palette[border_colour_index];
                                multiColourTable[3] = pal_palette[auxiliary_colour_index];
                                
                                pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel6]);
                                pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel7]);
                                
                                // If the matrix hasn't yet closed, then in the FETCH_CHAR_DATA 
                                // state, we need to keep incrementing the video matrix counter
                                // until it is closed, which at the latest could be HC=1 on the
                                // next line.
                                videoMatrixCounter++;
                                
                                fetchState = FETCH_SCREEN_CODE;
                                break;
                        }
                        
                        // After the two visible pixels, we now output the start of horiz blanking.
                        pio_sm_put(CVBS_PIO, CVBS_SM, PAL_FRONTPORCH_1);
                        
                        // Reset HC to start a new line.
                        horizontalCounter = 0;
                    }
                    else {
                        // Covers visible line cycles from HC=3 to 1 cycle before HC=HBLANKSTART (e.g. HC=70 for PAL)
                        switch (fetchState) {
                            case FETCH_OUTSIDE_MATRIX:
                                if (horizontalCounter > PAL_HBLANK_END) {
                                    // Output four border pixels.
                                    borderColour = pal_palette[border_colour_index];
                                    pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                }
                                // Nothing to do otherwise. Still in blanking if below 12.
                                break;
                                
                            case FETCH_IN_MATRIX_Y:
                            case FETCH_MATRIX_LINE:
                                if (horizontalCounter > PAL_HBLANK_END) {
                                    // Look up very latest background, border and auxiliary colour values. This
                                    // should not include an update to the foreground colour, as that will not 
                                    // have changed.
                                    multiColourTable[0] = pal_palette[background_colour_index];
                                    multiColourTable[1] = pal_palette[border_colour_index];
                                    multiColourTable[3] = pal_palette[auxiliary_colour_index];
                                    
                                    // Last three pixels of previous char data, or border pixels. 4th pixel always border.
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel6]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel7]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel8]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[1]);
                                    
                                    if (horizontalCounter == screen_origin_x) {
                                        // Last 4 pixels before first char renders are still border.
                                        fetchState = FETCH_MATRIX_DLY_1;
                                    }
                                }
                                else if (horizontalCounter == screen_origin_x) {
                                    // Still in horizontal blanking, but we still need to prepare for the case
                                    // where the next cycle isn't in horiz blanking, i.e. when HC=11 this cycle.
                                    fetchState = FETCH_MATRIX_DLY_1;
                                }
                                pixel1 = pixel2 = pixel3 = pixel4 = pixel5 = pixel6 = pixel7 = pixel8 = 1;
                                break;
                                
                            case FETCH_MATRIX_DLY_1:
                            case FETCH_MATRIX_DLY_2:
                            case FETCH_MATRIX_DLY_3:
                            case FETCH_MATRIX_DLY_4:
                                if (horizontalCounter > PAL_HBLANK_END) {
                                    // Output four border pixels.
                                    borderColour = pal_palette[border_colour_index];
                                    pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                                }
                                else {
                                    pixel2 = pixel3 = pixel4 = pixel5 = pixel6 = pixel7 = pixel8 = 1;
                                }
                                fetchState++;
                                break;
                                
                            case FETCH_SCREEN_CODE:
                                // Calculate address within video memory and fetch cell index.
                                cellIndex = xram[screen_mem_start + videoMatrixCounter];
                                
                                // Due to the way the colour memory is wired up, the above fetch of the cell index
                                // also happens to automatically fetch the foreground colour from the Colour Matrix
                                // via the top 4 lines of the data bus (DB8-DB11), which are wired directly from 
                                // colour RAM in to the VIC chip.
                                colourData = xram[colour_mem_start + videoMatrixCounter];
                                
                                // Look up very latest background, border and auxiliary colour values.
                                multiColourTable[0] = pal_palette[background_colour_index];
                                multiColourTable[1] = pal_palette[border_colour_index];
                                multiColourTable[3] = pal_palette[auxiliary_colour_index];
                            
                                // Output the 4 pixels for this cycle (usually second 4 pixels of a character).
                                if (horizontalCounter > PAL_HBLANK_END) {
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel2]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel3]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel4]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel5]);
                                }
                                
                                // Toggle fetch state. Close matrix if HCC hits zero.
                                fetchState = ((horizontalCellCounter-- > 0)? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                                break;
                                
                            case FETCH_CHAR_DATA:
                                // Look up very latest background, border and auxiliary colour values.
                                multiColourTable[0] = pal_palette[background_colour_index];
                                multiColourTable[1] = pal_palette[border_colour_index];
                                multiColourTable[3] = pal_palette[auxiliary_colour_index];
                                
                                if (horizontalCounter > PAL_HBLANK_END) {
                                    // Last three pixels of previous char data, or border pixels.
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel6]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel7]);
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel8]);
                                }
                                
                                // Calculate offset of data.
                                charDataOffset = char_mem_start + (cellIndex << char_size_shift) + cellDepthCounter;
                                
                                // Fetch cell data. It can wrap around, which is why we & with 0x3FFF.
                                charData = xram[(charDataOffset & 0x3FFF)];
                                
                                // Determine character pixels.
                                if ((colourData & 0x08) == 0) {
                                    // Hires mode.
                                    if (non_reverse_mode != 0) {
                                        // Normal unreversed graphics.
                                        pixel1 = ((charData & 0x80) > 0? 2 : 0);
                                        pixel2 = ((charData & 0x40) > 0? 2 : 0);
                                        pixel3 = ((charData & 0x20) > 0? 2 : 0);
                                        pixel4 = ((charData & 0x10) > 0? 2 : 0);
                                        pixel5 = ((charData & 0x08) > 0? 2 : 0);
                                        pixel6 = ((charData & 0x04) > 0? 2 : 0);
                                        pixel7 = ((charData & 0x02) > 0? 2 : 0);
                                        pixel8 = ((charData & 0x01) > 0? 2 : 0);
                                    } else {
                                        // Reversed graphics.
                                        pixel1 = ((charData & 0x80) > 0? 0 : 2);
                                        pixel2 = ((charData & 0x40) > 0? 0 : 2);
                                        pixel3 = ((charData & 0x20) > 0? 0 : 2);
                                        pixel4 = ((charData & 0x10) > 0? 0 : 2);
                                        pixel5 = ((charData & 0x08) > 0? 0 : 2);
                                        pixel6 = ((charData & 0x04) > 0? 0 : 2);
                                        pixel7 = ((charData & 0x02) > 0? 0 : 2);
                                        pixel8 = ((charData & 0x01) > 0? 0 : 2);
                                    }
                                } else {
                                    // Multicolour graphics.
                                    pixel1 = pixel2 = ((charData >> 6) & 0x03);
                                    pixel3 = pixel4 = ((charData >> 4) & 0x03);
                                    pixel5 = pixel6 = ((charData >> 2) & 0x03);
                                    pixel7 = pixel8 = (charData & 0x03);
                                }
                                
                                // Look up foreground colour before first pixel.
                                multiColourTable[2] = pal_palette[colourData & 0x07];
                                
                                // Output the first 4 pixels of the character.
                                if (horizontalCounter > PAL_HBLANK_END) {
                                    // New character starts 3 dot clock cycles after char data load. 
                                    pio_sm_put(CVBS_PIO, CVBS_SM, multiColourTable[pixel1]);
                                }
                                
                                // Increment the video matrix counter to next cell.
                                videoMatrixCounter++;
                                
                                // Toggle fetch state. For efficiency, HCC deliberately not checked here.
                                fetchState = FETCH_SCREEN_CODE;
                                break;
                        }
                        
                        horizontalCounter++;
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
                        case FETCH_MATRIX_DLY_4:
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
    
                    if (horizontalCounter == PAL_HBLANK_START) {
                        horizontalCounter = 0;
                    } else {
                        horizontalCounter++;
                    }
                }
                break;
        }
        aud_tick_inline();
        // DEBUG: Temporary check to see if we've overshot the 120 cycle allowance.
        if (pio_interrupt_get(VIC_PIO, 1)) {
            overruns = 1;
        }
    }
}

void vic_init(void) {
    // Initialisation.
    vic_pio_init();
    if(cfg_get_splash())
        vic_memory_init();
    switch(cfg_get_mode()){
        case(VIC_MODE_PAL):
            multicore_launch_core1(vic_core1_loop_pal);
            break;
        case(VIC_MODE_NTSC):
            multicore_launch_core1(vic_core1_loop_ntsc);
            break;
        default:    //Ignore test modes
            break;
    }
}

void vic_task(void) {
    if (overruns > 0) {
        printf("X.");
        overruns = 0;
    }
}

void vic_print_status(void){
    printf("VIC registers\n");
        printf(" CR0 %02x %d X-Orig %s\n", vic_cr0, vic_cr0 & 0x7F, (vic_cr0 & 0x80 ? "(intl)" : "" ));
        printf(" CR1 %02x %d Y-Orig\n", vic_cr1, vic_cr1);
        printf(" CR2 %02x %d Columns\n", vic_cr2, vic_cr2 & 0x7F);
        printf(" CR3 %02x %d Rows D:%d\n", vic_cr3, (vic_cr3 >> 1) & 0x3F, vic_cr3 & 1u);
        printf(" CR4 %02x %d Raster\n", vic_cr4, (vic_cr4 << 1) | (vic_cr3 >> 7));
        printf(" CR5 %02x BV:%04x BC:%04x\n", vic_cr5, (vic_cr5 & 0xF0) << (10-4), (vic_cr5 & 0x0F) << 10);
        printf(" CR6 %02x %d LP X\n", vic_cr6, vic_cr6);
        printf(" CR7 %02x %d LP Y\n", vic_cr7, vic_cr7);
        printf(" CR8 %02x %d POT X\n", vic_cr8, vic_cr8);
        printf(" CR9 %02x %d POT Y\n", vic_cr9, vic_cr9);
        printf(" CRA %02x %d V1 E:%d\n", vic_cra, vic_cra & 0x7F, (vic_cra >> 7));
        printf(" CRB %02x %d V2 E:%d\n", vic_crb, vic_crb & 0x7F, (vic_crb >> 7));
        printf(" CRC %02x %d V3 E:%d\n", vic_crc, vic_crc & 0x7F, (vic_crc >> 7));
        printf(" CRD %02x %d No E:%d\n", vic_crd, vic_crd & 0x7F, (vic_crd >> 7));
        printf(" CRE %02x %d Vol CA:%d\n", vic_cre, vic_cre & 0x0F, (vic_cre >> 4));
        printf(" CRF %02x CB:%d R:%d CE:%d\n", vic_crf, (vic_crf >> 4), (vic_crf >> 3) & 1u, (vic_crf & 0x7));
}