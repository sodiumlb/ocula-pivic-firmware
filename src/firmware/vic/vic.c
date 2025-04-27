/*
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "vic/vic.h"
#include "vic/char_rom.h"
#include "vic/cvbs.h"
#include "sys/mem.h"
#include "vic.pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>

// NOTE: VIC chip vs VIC 20 memory map is different. This is why we have
// the control registers appearing at $1000. The Chip Select for reading
// and writing to the VIC chip registers is when A13=A11=A10=A9=A8=0 and 
// A12=1, i.e. $10XX. Bottom 4 bits select one of the 16 registers.
//
// VIC chip addresses     VIC 20 addresses and their normal usage
//
// $0000                  $8000  Unreversed Character ROM
// $0400                  $8400  Reversed Character ROM
// $0800                  $8800  Unreversed upper/lower case ROM
// $0C00                  $8C00  Reversed upper/lower case ROM
// $1000                  $9000  VIC and VIA chips
// $1400                  $9400  Colour memory (at either $9400 or $9600)
// $1800                  $9800  Reserved for expansion (I/O #2)
// $1C00                  $9C00  Reserved for expansion (I/O #3)
// $2000                  $0000  System memory work area
// $2400                  $0400  Reserved for 1st 1K of 3K expansion
// $2800                  $0800  Reserved for 2nd 1K of 3K expansion
// $2C00                  $0C00  Reserved for 3rd 1K of 3K expansion
// $3000                  $1000  BASIC program area / Screen when using 8K+ exp
// $3400                  $1400  BASIC program area
// $3800                  $1800  BASIC program area
// $3C00                  $1C00  BASIC program area / $1E00 screen mem for unexp VIC

// VIC chip control registers, starting at $1000, as per Chip Select.
#define vic_cr0 xram[0x1000]    // ABBBBBBB A=Interlace B=Screen Origin X (4 pixels granularity)
#define vic_cr1 xram[0x1001]    // CCCCCCCC C=Screen Origin Y (2 pixel granularity)
#define vic_cr2 xram[0x1002]    // HDDDDDDD D=Number of Columns
#define vic_cr3 xram[0x1003]    // GEEEEEEF E=Number of Rows F=Double Size Chars
#define vic_cr4 xram[0x1004]    // GGGGGGGG G=Raster Line
#define vic_cr5 xram[0x1005]    // HHHHIIII H=Screen Mem Addr I=Char Mem Addr
#define vic_cr6 xram[0x1006]    // JJJJJJJJ Light pen X
#define vic_cr7 xram[0x1007]    // KKKKKKKK Light pen Y
#define vic_cr8 xram[0x1008]    // LLLLLLLL Paddle X
#define vic_cr9 xram[0x1009]    // MMMMMMMM Paddle Y
#define vic_cra xram[0x100a]    // NRRRRRRR Sound voice 1
#define vic_crb xram[0x100b]    // OSSSSSSS Sound voice 2
#define vic_crc xram[0x100c]    // PTTTTTTT Sound voice 3
#define vic_crd xram[0x100d]    // QUUUUUUU Noise voice
#define vic_cre xram[0x100e]    // WWWWVVVV W=Auxiliary colour V=Volume control
#define vic_crf xram[0x100f]    // XXXXYZZZ X=Background colour Y=Reverse Z=Border colour

// Expressions to access different parts of control registers.
#define border_colour_index      (vic_crf & 0x07)
#define background_colour_index  (vic_crf >> 4)
#define auxiliary_colour_index   (vic_cre >> 4)
#define non_reverse_mode         (vic_crf & 0x08)
#define screen_origin_x          ((vic_cr0 & 0x7F) + 6)
#define screen_origin_y          (vic_cr1 << 1)
#define num_of_columns           (vic_cr2 & 0x7F)
#define num_of_rows              ((vic_cr3 & 0x7E) >> 1)
#define double_height_mode       (vic_cr3 & 0x01)
#define last_line_of_cell        (7 | (double_height_mode << 3))
#define char_size_shift          (3 + double_height_mode)
#define screen_mem_start         (((vic_cr5 & 0xF0) << 6) | ((vic_cr2 & 0x80) << 2))
#define char_mem_start           ((vic_cr5 & 0x0F) << 10)
#define colour_mem_start         (0x1400 | ((vic_cr2 & 0x80) << 2))

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

// Constants for the fetch state of the vic_core1_loop.
#define FETCH_OUTSIDE_MATRIX  0
#define FETCH_IN_MATRIX_Y     1
#define FETCH_MATRIX_LINE     2
#define FETCH_SCREEN_CODE     3
#define FETCH_CHAR_DATA       4

// Constants related to video timing for PAL and NTSC.
#define PAL_HBLANK_END        11
#define PAL_HBLANK_START      70
#define PAL_VBLANK_START      1
#define PAL_VSYNC_START       4
#define PAL_VSYNC_END         6
#define PAL_VBLANK_END        9
#define PAL_LAST_LINE         311

#define CVBS_DELAY_CONST_POST (15-3)
#define CVBS_CMD(L0,L1,DC,delay,count) \
         ((((CVBS_DELAY_CONST_POST-delay)&0xF)<<23) |  ((L1&0x1F)<<18) | (((count-1)&0x1FF)<<9) |((L0&0x1F)<<4) | ((delay&0x0F)))
#define CVBS_REP(cmd,count) ((cmd & ~(0x1FF<<9)) | ((count-1) & 0x1FF)<<9)

// CVBS commands for optimised core1_loop that sends longer commands upfront when appropriate.
// Horiz Blanking - From: 70.5  To: 12  Len: 12.5 cycles
// Front Porch - From: 70.5  To: 1.5  Len: 2 cycles (Note: Partially split over VC lines)
// Horiz Sync - From: 1.5  To: 6.5  Len: 5 cycles
// Breezeway - From: 6.5  To: 7.5  Len: 1 cycle
// Colour Burst - From: 7.5  To: 11  Len: 3.5 cycles
// Back Porch - From: 11  To: 12  Len: 1 cycle
#define PAL_FRONTPORCH_1 CVBS_CMD(18,18,18, 0, 2)    // First two in second half of HC=70
#define PAL_FRONTPORCH_2 CVBS_CMD(18,18,18, 0, 6)    // Other six in HC=0 up to HC=1.5
#define PAL_HSYNC        CVBS_CMD( 0, 0, 0, 0,20)
#define PAL_BREEZEWAY    CVBS_CMD(18,18,18, 0, 4)
#define PAL_COLBURST_O   CVBS_CMD(6, 12, 9,11,14)
#define PAL_COLBURST_E   CVBS_CMD(12, 6, 9, 4,14)
#define PAL_BACKPORCH    CVBS_CMD(18,18,18, 0, 4)

// Vertical blanking and sync.
// TODO: From original cvbs.c code. Doesn't match VIC chip timings.
#define PAL_LONG_SYNC_L  CVBS_CMD( 0, 0, 0, 0,133)
#define PAL_LONG_SYNC_H  CVBS_CMD(18,18,18, 0,  9)
#define PAL_SHORT_SYNC_L CVBS_CMD( 0, 0, 0, 0,  9)
#define PAL_SHORT_SYNC_H CVBS_CMD(18,18,18, 0,133)

// Single pixel commands.
#define PAL_SYNC        CVBS_CMD( 0, 0, 0, 0,1)
#define PAL_BLANK       CVBS_CMD(18,18,18, 0,1)
#define PAL_BURST_O     CVBS_CMD(6,12,9,11,1)
#define PAL_BURST_E     CVBS_CMD(12,6,9,4,1)
#define PAL_BLACK       CVBS_CMD(18,18,9,0,1)
#define PAL_WHITE       CVBS_CMD(23,23,29,0,1)
#define PAL_RED_O       CVBS_CMD(5,10,15,9,1)
#define PAL_RED_E       CVBS_CMD(10,5,15,6,1)
#define PAL_CYAN_O      CVBS_CMD(9,15,24,9,1)
#define PAL_CYAN_E      CVBS_CMD(15,9,24,7,1)
#define PAL_PURPLE_O    CVBS_CMD(29,22,18,5,1)
#define PAL_PURPLE_E    CVBS_CMD(22,29,18,10,1)
#define PAL_GREEN_O     CVBS_CMD(1,7,22,5,1)
#define PAL_GREEN_E     CVBS_CMD(7,1,22,10,1)
#define PAL_BLUE_O      CVBS_CMD(9,10,14,0,1)
#define PAL_BLUE_E      CVBS_CMD(9,10,14,0,1)
#define PAL_YELLOW_O    CVBS_CMD(5,15,25,0,1)
#define PAL_YELLOW_E    CVBS_CMD(5,15,25,0,1)
#define PAL_ORANGE_O    CVBS_CMD(19,22,19,11,1)
#define PAL_ORANGE_E    CVBS_CMD(22,19,19,4,1)
#define PAL_LORANGE_O   CVBS_CMD(27,21,24,11,1)
#define PAL_LORANGE_E   CVBS_CMD(21,27,24,4,1)
#define PAL_PINK_O      CVBS_CMD(11,5,23,9,1)
#define PAL_PINK_E      CVBS_CMD(5,11,23,6,1)
#define PAL_LCYAN_O	    CVBS_CMD(3,15,27,9,1)
#define PAL_LCYAN_E     CVBS_CMD(15,3,27,7,1)
#define PAL_LPURPLE_O   CVBS_CMD(27,21,24,5,1)
#define PAL_LPURPLE_E   CVBS_CMD(21,27,24,10,1)
#define PAL_LGREEN_O    CVBS_CMD(29,23,26,5,1)
#define PAL_LGREEN_E    CVBS_CMD(23,29,26,10,1)
#define PAL_LBLUE_O     CVBS_CMD(3,5,22,0,1)
#define PAL_LBLUE_E     CVBS_CMD(3,5,22,0,1)
#define PAL_LYELLOW_O   CVBS_CMD(19,31,28,0,1)
#define PAL_LYELLOW_E   CVBS_CMD(19,31,28,0,1)

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
    // Set up VIC PIO.
    pio_set_gpio_base(VIC_PIO, VIC_PIN_OFFS);
    // TODO: We might add the second output clock in the future.
    pio_gpio_init(VIC_PIO, VIC_PIN_BASE);
    // TODO: Check if the drive strength is appropriate for the clock.
    gpio_set_drive_strength(VIC_PIN_BASE, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(VIC_PIO, VIC_SM, VIC_PIN_BASE, 1, true);
    uint offset = pio_add_program(VIC_PIO, &clkgen_program);
    pio_sm_config config = clkgen_program_get_default_config(offset);
    sm_config_set_out_pins(&config, VIC_PIN_BASE, 1);
    pio_sm_init(VIC_PIO, VIC_SM, offset, &config);
    pio_sm_set_enabled(VIC_PIO, VIC_SM, true);   
    printf("VIC init done\n");
}

void vic_memory_init() {
    // Load the VIC 20 Character ROM into internal PIVIC RAM.
    memcpy((void*)(&xram[ADDR_UPPERCASE_GLYPHS_CHRSET]), (void*)vic_char_rom, sizeof(vic_char_rom));

    // Set up hard coded control registers for now (from default PAL VIC).
    xram[0x1000] = 0x0C;    // Screen Origin X = 12
    xram[0x1001] = 0x26;    // Screen Origin Y = 38
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

void vic_core1_loop(void) {

    // Initialisation.
    cvbs_init();
    vic_pio_init();
    vic_memory_init();

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

    // Holds the CVBS colour commands for foreground, border, background and auxiliary.
    uint32_t foregroundColour = 0;       // CVBS command for the current foreground colour.
    uint32_t borderColour = 0;           // CVBS command for the current border colour.
    uint32_t backgroundColour = 0;       // CVBS command for the current background colour.
    uint32_t auxiliaryColour = 0;        // CVBS command for the current auxiliary colour.

    // Holds the colour commands for each of the current multi colour colours.
    uint32_t multiColourTable[4] = { 0, 0, 0, 0};

    // Every cpu cycle, we output four pixels. The values are temporarily stored in these vars.
    uint32_t pixel1 = 0;
    uint32_t pixel2 = 0;
    uint32_t pixel3 = 0;
    uint32_t pixel4 = 0;
    uint32_t pixel5 = 0;
    uint32_t pixel6 = 0;
    uint32_t pixel7 = 0;
    uint32_t pixel8 = 0;

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

        // HC = 0 is handled in a single block for all lines.
        if (horizontalCounter == 0) {
            if (verticalCounter == PAL_LAST_LINE) {
                // Previous cycle was end of last line. This is when VCC loads number of rows.
                verticalCounter = 0;
                verticalCellCounter = num_of_rows;
                fetchState = FETCH_OUTSIDE_MATRIX;
            } else {
                verticalCounter++;
            }

            // Check for Cell Depth Counter reset.
            if (cellDepthCounter == last_line_of_cell) {
                cellDepthCounter = 0;

                // Vertical Cell Counter decrements when CDC resets, unless its the first line, 
                // since it was loaded instead (see VC reset logic above).
                if (verticalCounter > 0) {
                    verticalCellCounter--;

                    if (verticalCellCounter == 0) {
                        // If all text rows rendered, then we're outside the matrix again.
                        fetchState = FETCH_OUTSIDE_MATRIX;
                    }
                }
            }
            else if (fetchState == FETCH_MATRIX_LINE) {
                // If the line that just ended was a video matrix line, then increment CDC.
                cellDepthCounter++;
            }
            else if (verticalCounter == screen_origin_y) {
                // This is the line the video matrix starts on. As in the real chip, we use
                // a different state for the first part of the first video matrix line.
                fetchState = FETCH_IN_MATRIX_Y;
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

            // Video Matrix Counter (VMC) reloaded from latch at start of line.
            videoMatrixCounter = videoMatrixLatch;
            
            // Horizontal Cell Counter is loaded at the start of each line with num of columns.
            horizontalCellCounter = num_of_columns;

            horizontalCounter++;
        }
        // Line 0, and Lines after 9, are "visible", i.e. not within the vertical blanking.
        else if ((verticalCounter == 0) || (verticalCounter > PAL_VBLANK_END)) {
            if (horizontalCounter == PAL_HBLANK_START) {
                // Horizontal blanking doesn't actually start until 2 pixels in.
                // TODO: Decide whether it is appropriate to always assume border colour for first 2 pixels.
                // TODO: Should it instead be the next two pixels from the char, if there are any?
                borderColour = pal_palette[border_colour_index];
                pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                pio_sm_put(CVBS_PIO, CVBS_SM, PAL_FRONTPORCH_1);
                
                // If the CDC is on the last of its lines, then we latch the VMC at the end of the line. In
                // the real chip, this actually happens every cycle of the last CDC line, but that is 
                // probably an optimisation and not needed.
                if (cellDepthCounter == last_line_of_cell) {
                    videoMatrixLatch = videoMatrixCounter;
                }

                // If the video matrix hasn't closed yet, then its still either FETCH_SCREEN_CODE 
                // or FETCH_CHAR_DATA. In that scenario, we need to close it by setting fetchState
                // to FETCH_MATRIX_LINE so that the next line starts with border as expected, i.e.
                // the "In Matrix" state ends either when HCC counts down OR the end of the line.
                if (fetchState > FETCH_MATRIX_LINE) {
                    fetchState = FETCH_MATRIX_LINE;
                }

                // Nothing else to do at this point, so reset HC and skip rest of loop.
                horizontalCounter = 0;
            }
            else {
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
                            // Output four border pixels.
                            borderColour = pal_palette[border_colour_index];
                            pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                            pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                            pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                            pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);

                            if (horizontalCounter == screen_origin_x) {
                                // Last 4 pixels before first char renders are still border.
                                pixel5 = pixel6 = pixel7 = pixel8 = borderColour;
                                fetchState = FETCH_SCREEN_CODE;
                            }
                        }
                        else if (horizontalCounter == screen_origin_x) {
                            // Still in horizontal blanking, but we still need to prepare for the case
                            // where the next cycle isn't in horiz blanking, i.e. when HC=11 this cycle.
                            borderColour = pal_palette[border_colour_index];
                            pixel5 = pixel6 = pixel7 = pixel8 = borderColour;
                            fetchState = FETCH_SCREEN_CODE;
                        }
                        break;

                    case FETCH_SCREEN_CODE:
                        // Calculate address within video memory and fetch cell index.
                        cellIndex = xram[screen_mem_start + videoMatrixCounter];

                        // Due to the way the colour memory is wired up, the above fetch of the cell index
                        // also happens to automatically fetch the foreground colour from the Colour Matrix
                        // via the top 4 lines of the data bus (DB8-DB11), which are wired directly from 
                        // colour RAM in to the VIC chip.
                        colourData = xram[colour_mem_start + videoMatrixCounter];
                        
                        // Lookup the CVBS commands for each colour in preparation for pixel output.
                        multiColourTable[0] = backgroundColour = pal_palette[background_colour_index];
                        multiColourTable[1] = borderColour = pal_palette[border_colour_index];
                        multiColourTable[2] = foregroundColour = pal_palette[colourData & 0x07];
                        multiColourTable[3] = auxiliaryColour = pal_palette[auxiliary_colour_index];

                        // Output the 4 pixels for this cycle (usually second 4 pixels of a character).
                        if (horizontalCounter > PAL_HBLANK_END) {
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel5);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel6);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel7);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel8);
                        }

                        // Toggle fetch state. Close matrix if HCC hits zero.
                        fetchState = (horizontalCellCounter--? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                        break;

                    case FETCH_CHAR_DATA:
                        // Calculate offset of data.
                        charDataOffset = char_mem_start + (cellIndex << char_size_shift) + cellDepthCounter;

                        // Fetch cell data.
                        charData = xram[charDataOffset];

                        // Determine character pixels.
                        if ((colourData & 0x08) == 0) {
                            // Hires mode.
                            if (non_reverse_mode) {
                                // Normal unreversed graphics.
                                pixel1 = (charData & 0x80? foregroundColour : backgroundColour);
                                pixel2 = (charData & 0x40? foregroundColour : backgroundColour);
                                pixel3 = (charData & 0x20? foregroundColour : backgroundColour);
                                pixel4 = (charData & 0x10? foregroundColour : backgroundColour);
                                pixel5 = (charData & 0x08? foregroundColour : backgroundColour);
                                pixel6 = (charData & 0x04? foregroundColour : backgroundColour);
                                pixel7 = (charData & 0x02? foregroundColour : backgroundColour);
                                pixel8 = (charData & 0x01? foregroundColour : backgroundColour);
                            } else {
                                // Reversed graphics.
                                pixel1 = (charData & 0x80? backgroundColour : foregroundColour);
                                pixel2 = (charData & 0x40? backgroundColour : foregroundColour);
                                pixel3 = (charData & 0x20? backgroundColour : foregroundColour);
                                pixel4 = (charData & 0x10? backgroundColour : foregroundColour);
                                pixel5 = (charData & 0x08? backgroundColour : foregroundColour);
                                pixel6 = (charData & 0x04? backgroundColour : foregroundColour);
                                pixel7 = (charData & 0x02? backgroundColour : foregroundColour);
                                pixel8 = (charData & 0x01? backgroundColour : foregroundColour);
                            }
                        } else {
                            // Multicolour graphics.
                            pixel1 = pixel2 = multiColourTable[(charData >> 6) & 0x03];
                            pixel3 = pixel4 = multiColourTable[(charData >> 4) & 0x03];
                            pixel5 = pixel6 = multiColourTable[(charData >> 2) & 0x03];
                            pixel7 = pixel8 = multiColourTable[charData & 0x03];
                        }

                        // Output the first 4 pixels of the character.
                        if (horizontalCounter > PAL_HBLANK_END) {
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel1);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel2);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel3);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel4);
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
            // HC=0. For the rest of the line, it is a simplified version of the standard line, 
            // except that we don't output any pixels.
            if (horizontalCounter == PAL_HBLANK_START) {
                horizontalCounter = 0;
                if (cellDepthCounter == last_line_of_cell) {
                    videoMatrixLatch = videoMatrixCounter;
                }
            }
            else {
                // In case the screen origin Y is set within the vertical blanking lines, we still need 
                // to update the fetch state, video matrix counter, and the horizontal cell counter, even
                // though we're not outputting character pixels.
                switch (fetchState) {
                    case FETCH_OUTSIDE_MATRIX:
                        break;
                    case FETCH_IN_MATRIX_Y:
                    case FETCH_MATRIX_LINE:
                        if (horizontalCounter == screen_origin_x) {
                            fetchState = FETCH_SCREEN_CODE;
                        }
                        break;
                    case FETCH_SCREEN_CODE:
                        fetchState = (horizontalCellCounter--? FETCH_CHAR_DATA : FETCH_MATRIX_LINE);
                        break;
                    case FETCH_CHAR_DATA:
                        videoMatrixCounter++;
                        fetchState = FETCH_SCREEN_CODE;
                        break;
                }

                horizontalCounter++;
            }
        }

        // TODO: Update raster line in VIC control registers 3 & 4.

        // DEBUG: Temporary check to see if we've overshot the 120 cycle allowance.
        if (pio_interrupt_get(VIC_PIO, 1)) {
            overruns = 1;
        }
    }
}

void vic_init(void) {
    multicore_launch_core1(vic_core1_loop);
}

void vic_task(void) {
    if (overruns > 0) {
        printf("X.");
        overruns = 0;
    }
}
