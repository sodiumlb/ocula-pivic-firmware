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

// VIC chip control registers.
#define vic_cr0 xram[0x9000]    // ABBBBBBB A=Interlace B=Screen Origin X (4 pixels granularity)
#define vic_cr1 xram[0x9001]    // CCCCCCCC C=Screen Origin Y (2 pixel granularity)
#define vic_cr2 xram[0x9002]    // HDDDDDDD D=Number of Columns
#define vic_cr3 xram[0x9003]    // GEEEEEEF E=Number of Rows F=Double Size Chars
#define vic_cr4 xram[0x9004]    // GGGGGGGG G=Raster Line
#define vic_cr5 xram[0x9005]    // HHHHIIII H=Screen Mem Addr I=Char Mem Addr
#define vic_cr6 xram[0x9006]    // JJJJJJJJ Light pen X
#define vic_cr7 xram[0x9007]    // KKKKKKKK Light pen Y
#define vic_cr8 xram[0x9008]    // LLLLLLLL Paddle X
#define vic_cr9 xram[0x9009]    // MMMMMMMM Paddle Y
#define vic_cra xram[0x900a]    // NRRRRRRR Sound voice 1
#define vic_crb xram[0x900b]    // OSSSSSSS Sound voice 2
#define vic_crc xram[0x900c]    // PTTTTTTT Sound voice 3
#define vic_crd xram[0x900d]    // QUUUUUUU Noise voice
#define vic_cre xram[0x900e]    // WWWWVVVV W=Auxiliary colour V=Volume control
#define vic_crf xram[0x900f]    // XXXXYZZZ X=Background colour Y=Reverse Z=Border colour

// Expressions to access different parts of control registers.
#define brd_cr (vic_crf & 0x07)
#define bck_cr (vic_crf >> 4)
#define aux_cr (vic_cre >> 4)
#define rev_cr (vic_crf & 0x08)
#define sox_cr (vic_cr0 & 0x7F)
#define soy_cr vic_cr1
#define col_cr (vic_cr2 & 0x7F)
#define row_cr ((vic_cr3 & 0x7E) >> 1)
#define dbl_cr (vic_cr3 & 0x01)
#define chr_cr (vic_cr5 & 0x0F)
#define scn_cr (((vic_cr5 & 0xF0) >> 3) | ((vic_cr2 & 0x80) >> 7))

// A lookup table for determining the start of video memory.
const uint16_t videoMemoryTable[32] = { 
    0x8000, 0x8200, 0x8400, 0x8600, 0x8800, 0x8A00, 0x8C00, 0x8E00, 
    0x9000, 0x9200, 0x9400, 0x9600, 0x9800, 0x9A00, 0x9C00, 0x9E00, 
    0x0000, 0x0200, 0x0400, 0x0600, 0x0800, 0x0A00, 0x0C00, 0x0E00, 
    0x1000, 0x1200, 0x1400, 0x1600, 0x1800, 0x1A00, 0x1C00, 0x1E00 
};

// A lookup table for determining the start of character memory.
const uint16_t charMemoryTable[16] = { 
    0x8000, 0x8400, 0x8800, 0x8C00, 0x9000, 0x9400, 0x9800, 0x9C00, 
    0x0000, 0x0400, 0x0800, 0x0C00, 0x1000, 0x1400, 0x1800, 0x1C00 
};

#define screen_memory videoMemoryTable[scn_cr]
#define char_memory   charMemoryTable[chr_cr]

// These are the actual VIC 20 memory addresses, but note that the VIC chip sees memory a bit differently.
// $8000-$83FF: 1 KB uppercase/glyphs
// $8400-$87FF: 1 KB uppercase/lowercase
// $8800-$8BFF: 1 KB inverse uppercase/glyphs
// $8C00-$8FFF: 1 KB inverse uppercase/lowercase
#define ADDR_UPPERCASE_GLYPHS_CHRSET            0x8000
#define ADDR_UPPERCASE_LOWERCASE_CHRSET         0x8400
#define ADDR_INVERSE_UPPERCASE_GLYPHS_CHRSET    0x8800
#define ADDR_INVERSE_UPPERCASE_LOWERCASE_CHRSET 0x8C00

// These are the 'standard' screen memory locations in the VIC 20 memory map, but in practice the 
// programmer can set screen memory to several other locations.
#define ADDR_UNEXPANDED_SCR  0x1E00
#define ADDR_8KPLUS_EXP_SCR  0x1000

#define ADDR_COLOUR_RAM      0x9600

// Constants for the fetch state of the optimised version of core1_loop.
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
// Front Porch - From: 70.5  To: 1.5  Len: 2 cycles
// Horiz Sync - From: 1.5  To: 6.5  Len: 5 cycles
// Breezeway - From: 6.5  To: 7.5  Len: 1 cycle
// Colour Burst - From: 7.5  To: 11  Len: 3.5 cycles
// Back Porch - From: 11  To: 12  Len: 1 cycle
#define PAL_FRONTPORCH   CVBS_CMD(18,18,18, 0, 8)
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

const uint32_t pal_palette_o[16] = {
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

const uint32_t pal_palette_e[16] = {
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


static uint8_t inline __attribute__((always_inline)) outputPixels(
        uint16_t verticalCounter, uint8_t horizontalCounter, 
        bool sync, bool colourBurst, bool blanking, bool pixelOutputEnabled,
        bool hsync, bool hblank, bool vsync, bool vblank, bool vblankPulse, bool vsyncPulse,
        uint8_t charData, uint8_t colourData, uint8_t backgroundColour,
        uint8_t borderColour, uint8_t auxiliaryColour,
        uint32_t pixel1, uint32_t pixel2) {

    // Sync level output is triggered in three scenarios:
    // - When hsync is true and vblank is not true (i.e. hsync doesn't happen during vertical blanking)
    // - When vblank is true and vblankPulse is not true (short syncs during lines 1-3 and 7-9)
    // - When vsync is true and vblankPulse is also true (long syncs during lines 4-6)
    sync = (hsync || (vblank && !vblankPulse) || vsyncPulse);

    // Blanking is much simpler by comparison.
    blanking = (vblank || hblank);

    if (verticalCounter & 1) {
        // Odd
        if (sync) {
            pixel1 = pixel2 = PAL_SYNC;
        }
        else if (colourBurst) {
            pixel1 = pixel2 = PAL_BURST_O;
        }
        else if (blanking) {
            pixel1 = pixel2 = PAL_BLANK;
        }
        else if (pixelOutputEnabled) {
            // Normal graphics for now (i.e. no reverse and multicolour)
            pixel1 = pal_palette_o[((charData & 0x80) == 0 ? backgroundColour : colourData)];
            pixel2 = pal_palette_o[((charData & 0x40) == 0 ? backgroundColour : colourData)];
        }
        else {
            pixel1 = pixel2 = pal_palette_o[borderColour];
        }
    } else {
        // Even
        if (sync) {
            pixel1 = pixel2 = PAL_SYNC;
        }
        else if (colourBurst) {
            pixel1 = pixel2 = PAL_BURST_E;
        }
        else if (blanking) {
            pixel1 = pixel2 = PAL_BLANK;
        }
        else if (pixelOutputEnabled) {
            pixel1 = pal_palette_e[((charData & 0x80) == 0 ? backgroundColour : colourData)];
            pixel2 = pal_palette_e[((charData & 0x40) == 0 ? backgroundColour : colourData)];
        }
        else {
            pixel1 = pixel2 = pal_palette_e[borderColour];
        }
    }

    pio_sm_put(CVBS_PIO, CVBS_SM, pixel1);
    pio_sm_put(CVBS_PIO, CVBS_SM, pixel2);

    // Shift pixel shift register by 2, since we just output 2 pixels.
    return (charData << 2);
}

void vic_pio_init(void) {
    // Set up VIC PIO.
    pio_set_gpio_base(VIC_PIO, VIC_PIN_BANK);
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
    memcpy((void*)(&xram[ADDR_UPPERCASE_GLYPHS_CHRSET]), (void*)vic_char_rom, sizeof(vic_char_rom));

    // Set up a test page for the screen memory.
    memset((void*)&xram[ADDR_UNEXPANDED_SCR], 0x20, 1024);

    // First row
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

    // Second row
    xram[ADDR_UNEXPANDED_SCR + 22] = 16;  // P
    xram[ADDR_UNEXPANDED_SCR + 23] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 24] = 22;  // V
    xram[ADDR_UNEXPANDED_SCR + 25] = 9;   // I
    xram[ADDR_UNEXPANDED_SCR + 26] = 3;   // C
    xram[ADDR_COLOUR_RAM + 22] = 7;
    xram[ADDR_COLOUR_RAM + 23] = 0;
    xram[ADDR_COLOUR_RAM + 24] = 2;
    xram[ADDR_COLOUR_RAM + 25] = 3;
    xram[ADDR_COLOUR_RAM + 26] = 4;
}

// This implementation is more like a traditional software based VIC 20 emulator, ignoring some of
// the finer details about what is happening within the physical VIC chip, taking shortcuts where
// possible to make things more efficient.
void core1_entry_new(void) {

    cvbs_init();
    vic_pio_init();
    vic_memory_init();

    //
    // START OF VIC CHIP STATE
    //

    // Hard coded control registers for now (from default PAL VIC).
    uint8_t  screenOriginX = 12;         // 7-bit horiz counter value to match for left of video matrix
    uint8_t  screenOriginY = 38 * 2;     // 8-bit vert counter value (x 2) to match for top of video matrix
    uint8_t  numOfColumns = 22;          // 7-bit number of video matrix columns
    uint8_t  numOfRows = 23;             // 6-bit number of video matrix rows
    uint8_t  lastCellLine = 7;           // Last Cell Depth Counter value. Depends on double height mode.
    uint8_t  characterSizeShift = 3;     // Number of bits the Cell Depth Counter counts over.
    uint8_t  backgroundColourIndex = 1;  // 4-bit background colour index
    uint8_t  borderColourIndex = 11;     // 3-bit border colour index
    uint8_t  auxiliaryColourIndex = 0;   // 4-bit auxiliary colour index
    uint8_t  reverse = 0;                // 1-bit reverse state
    uint16_t videoMemoryStart = 0;       // Decoded starting address for screen memory.
    uint16_t colourMemoryStart = 0;      // Decoded starting address for colour memory.
    uint16_t characterMemoryStart = 0;   // Decoded starting address for character memory.

    // Counters.
    uint16_t videoMatrixCounter = 0;     // 12-bit video matrix counter (VMC)
    uint16_t videoMatrixLatch = 0;       // 12-bit latch that VMC is stored to and loaded from
    uint16_t verticalCounter = 0;        // 9-bit vertical counter (i.e. raster lines)
    uint8_t  horizontalCounter = 0;      // 8-bit horizontal counter (although top bit isn't used)
    uint8_t  horizontalCellCounter = 0;  // 8-bit horizontal cell counter (down counter)
    uint8_t  verticalCellCounter = 0;    // 6-bit vertical cell counter (down counter)
    uint8_t  cellDepthCounter = 0;       // 4-bit cell depth counter (sounds either from 0-7, or 0-15)

    // Values normally fetched externally, from screen mem, colour RAM and char mem.
    uint8_t  cellIndex = 0;              // 8 bits fetched from screen memory.
    uint8_t  charData = 0;               // 8 bits of bitmap data fetched from character memory.
    uint8_t  colourData = 0;             // 4 bits fetched from colour memory (top bit multi/hires mode)
    uint8_t  colourDataLatch = 0;        // 4 bits latched from colour memory during cell index fetch.

    // Holds the colour commands for border, background and auxiliary.
    uint32_t foregroundColour = 0;       // CVBS command for the foreground colour.
    uint32_t borderColour = 0;           // Points to either odd or even border colour.
    uint32_t borderColourOdd = 0;        // CVBS command for border colour on odd line.
    uint32_t borderColourEven = 0;       // CVBS command for border colour on even line.
    uint32_t backgroundColour = 0;       // Points to either odd or even background colour.
    uint32_t backgroundColourOdd = 0;    // CVBS command for background colour on odd line.
    uint32_t backgroundColourEven = 0;   // CVBS command for background colour on even line.
    uint32_t auxiliaryColour = 0;        // Points to odd or even auxiliary colour.
    uint32_t auxiliaryColourOdd = 0;     // CVBS command for auxiliary colour on odd line.
    uint32_t auxiliaryColourEven = 0;    // CVBS command for auxiliary colour on even line.

    // Holds the colour commands for each multi colour colour for odd and even lines.
    uint32_t multiColourTableOdd[4] = { 0, 0, 0, 0};
    uint32_t multiColourTableEven[4] = { 0, 0, 0, 0};
    uint32_t *multiColourTable = multiColourTableEven;

    // Every cpu cycle, we output four pixels. The values are temporarily stored in these vars.
    uint32_t pixel1 = 0;
    uint32_t pixel2 = 0;
    uint32_t pixel3 = 0;
    uint32_t pixel4 = 0;
    uint32_t pixel5 = 0;
    uint32_t pixel6 = 0;
    uint32_t pixel7 = 0;
    uint32_t pixel8 = 0;

    // Optimisation to represent "in matrix", "address output enabled", and "pixel output enabled" 
    // all with one simple state variable. It might not be 100% accurate but should work for most 
    // cases.
    uint8_t fetchState = FETCH_OUTSIDE_MATRIX;
    
    //
    // END OF VIC CHIP STATE
    //


    // Temporary variables, not a core part of the state.
    uint16_t charDataOffset = 0;

    // TESTING: Remove after integration with control register changes.
    videoMemoryStart = ADDR_UNEXPANDED_SCR;
    colourMemoryStart = ADDR_COLOUR_RAM;
    characterMemoryStart = ADDR_UPPERCASE_GLYPHS_CHRSET;

    // TESTING: Example of variables set up for certain border/background/aux colours.
    backgroundColourIndex = 1;
    backgroundColourEven = pal_palette_e[backgroundColourIndex];
    backgroundColourOdd = pal_palette_o[backgroundColourIndex];
    backgroundColour = backgroundColourEven;
    borderColourIndex = 11;
    borderColourEven = pal_palette_e[borderColourIndex];
    borderColourOdd = pal_palette_o[borderColourIndex];
    borderColour = borderColourEven;
    auxiliaryColourIndex = 0;
    auxiliaryColourEven = pal_palette_e[auxiliaryColourIndex];
    auxiliaryColourOdd = pal_palette_o[auxiliaryColourIndex];
    auxiliaryColour = auxiliaryColourEven;
    multiColourTableEven[0] = backgroundColourEven;
    multiColourTableEven[1] = borderColourEven;
    multiColourTableEven[3] = auxiliaryColourEven;
    multiColourTableOdd[0] = backgroundColourOdd;
    multiColourTableOdd[1] = borderColourOdd;
    multiColourTableOdd[3] = auxiliaryColourOdd;

    while (1) {
        // Poll for PIO IRQ 0. This is the rising edge of F1.
        while (!pio_interrupt_get(VIC_PIO, 1)) {
            tight_loop_contents();
        }

        // Clear the IRQ flag immediately for now. 
        pio_interrupt_clear(VIC_PIO, 1);

        // VERTICAL TIMINGS:
        // Lines 1-9:    Vertical blanking
        // Lines 4-6:    Vertical sync
        // Lines 10-311: Normal visible lines.
        // Line 0:       Last visible line of a frame (yes, this is actually true)

        // Line 0, and Lines after 9, are "visible", i.e. not within the vertical blanking.
        if ((verticalCounter == 0) || (verticalCounter > PAL_VBLANK_END)) {
            
            if (horizontalCounter == PAL_HBLANK_START) {
                // Horizontal blanking doesn't actually start until 2 pixels in.
                pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);

                // Start of horizontal blanking. Let's send all blanking CVBS commands up front.
                // Horiz Blanking - From: 70.5  To: 12  Len: 12.5 cycles
                // Front Porch - From: 70.5  To: 1.5  Len: 2 cycles
                // Horiz Sync - From: 1.5  To: 6.5  Len: 5 cycles
                // Breezeway - From: 6.5  To: 7.5  Len: 1 cycle
                // Colour Burst - From: 7.5  To: 11  Len: 3.5 cycles
                // Back Porch - From: 11  To: 12  Len: 1 cycle
                pio_sm_put(CVBS_PIO, CVBS_SM, PAL_FRONTPORCH);
                pio_sm_put(CVBS_PIO, CVBS_SM, PAL_HSYNC);
                pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BREEZEWAY);
                // As this is meant for the next line, odd/even logic is reversed.
                if (verticalCounter & 1) {
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_COLBURST_E);
                } else {
                    pio_sm_put(CVBS_PIO, CVBS_SM, PAL_COLBURST_O);
                }
                pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BACKPORCH);

                // Nothing else to do at this point, so reset HC and skip rest of loop.
                horizontalCounter = 0;
            }
            else if (horizontalCounter == 0) {
                if (verticalCounter == PAL_LAST_LINE) {
                    // Last line. This is when VCC loads number of rows.
                    verticalCounter = 0;
                    verticalCellCounter = numOfRows;
                    fetchState = FETCH_OUTSIDE_MATRIX;
                } else {
                    verticalCounter++;

                    // Check for Cell Depth Counter reset.
                    if (cellDepthCounter == lastCellLine) {
                        cellDepthCounter = 0;
                        
                        // Vertical Cell Counter decrements when CDC resets, unless its the last line.
                        verticalCellCounter--;

                        if (verticalCellCounter == 0) {
                            // If all text rows rendered, then we're outside the matrix again.
                            fetchState = FETCH_OUTSIDE_MATRIX;
                        }
                    }
                    else if (fetchState == FETCH_MATRIX_LINE) {
                        // If the line that just ended was a video matrix line, then increment CDC.
                        cellDepthCounter++;
                    }
                    else if (verticalCounter == screenOriginY) {
                        // This is the line the video matrix starts on.
                        fetchState = FETCH_IN_MATRIX_Y;
                    }

                    // Switch the CVBS commands at the start of each line.
                    if (verticalCounter & 1) {
                        // Odd line.
                        borderColour = borderColourOdd;
                        backgroundColour = backgroundColourOdd;
                        auxiliaryColour = auxiliaryColourOdd;
                        multiColourTable = multiColourTableOdd;
                    } else {
                        // Even line.
                        borderColour = borderColourEven;
                        backgroundColour = backgroundColourEven;
                        auxiliaryColour = auxiliaryColourEven;
                        multiColourTable = multiColourTableEven;
                    }
                }

                // Video Matrix Counter (VMC) reloaded from latch at start of line.
                videoMatrixCounter = videoMatrixLatch;
                
                // Horizontal Cell Counter is loaded at the start of each line with num of columns.
                horizontalCellCounter = (numOfColumns << 1);

                // In theory, nothing else to do. Pixels already written in HC=70 cycle.
                horizontalCounter++;
            }
            else {
                switch (fetchState) {
                    case FETCH_OUTSIDE_MATRIX:
                        if (horizontalCounter > PAL_HBLANK_END) {
                            // Output four border pixels.
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
                            pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                            pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                            pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                            pio_sm_put(CVBS_PIO, CVBS_SM, borderColour);
                        }
                        if (horizontalCounter == screenOriginX) {
                            // Last 4 pixels before first char renders are still border.
                            pixel5 = pixel6 = pixel7 = pixel8 = borderColour;
                            fetchState = FETCH_SCREEN_CODE;
                        }
                        break;

                    case FETCH_SCREEN_CODE:
                        // Calculate address within video memory and fetch cell index.
                        cellIndex = xram[videoMemoryStart + videoMatrixCounter];

                        // Due to the way the colour memory is wired up, the above fetch of the cell index
                        // also happens to automatically fetch the foreground colour from the Colour Matrix
                        // via the top 4 lines of the data bus (DB8-DB11), which are wired directly from 
                        // colour RAM in to the VIC chip. It gets latched at this point, but not made available
                        // to the pixel colour selection logic until the char data is fetched.
                        colourDataLatch = xram[colourMemoryStart + videoMatrixCounter];

                        // Increment the video matrix counter.
                        videoMatrixCounter++;

                        // Output the 4 pixels for this cycle (usually second 4 pixels of a character).
                        if (horizontalCounter > PAL_HBLANK_END) {
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel5);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel6);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel7);
                            pio_sm_put(CVBS_PIO, CVBS_SM, pixel8);
                        }

                        // Toggle fetch state. Close matrix if HCC hits zero.
                        fetchState = (--horizontalCellCounter? FETCH_SCREEN_CODE : FETCH_MATRIX_LINE);
                        break;

                    case FETCH_CHAR_DATA:
                        // Calculate offset of data.
                        charDataOffset = characterMemoryStart + (cellIndex << characterSizeShift) + cellDepthCounter;

                        // Adjust offset for memory wrap around (due to diff in VIC 20 mem map vs VIC chip mem map)
                        if ((characterMemoryStart < 8192) && (charDataOffset >= 8192)) {
                            charDataOffset += 24576;
                        }

                        // Fetch cell data.
                        charData = xram[charDataOffset];

                        // Now that the char data is available, we can let the colour data "in".
                        colourData = colourDataLatch;
                        foregroundColour = (verticalCounter & 1? 
                            pal_palette_o[colourData] : 
                            pal_palette_e[colourData]);

                        // Determinen character pixels.
                        if ((colourData & 0x08) == 0) {
                            // Hires mode.
                            if (reverse == 0) {
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
                            multiColourTable[2] = foregroundColour;
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

                        // Toggle fetch state. Close matrix if HCC hits zero.
                        fetchState = (--horizontalCellCounter? FETCH_SCREEN_CODE : FETCH_MATRIX_LINE);
                        break;
                }
            }
        } else {
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

        // TODO: Update raster line in VIC control registers 3 & 4.

        // DEBUG: Temporary check to see if we've overshot the 120 cycle allowance.
        if (pio_interrupt_get(VIC_PIO, 1)) {
            overruns = 1;
        }
    }
}

// This implementation tries to match as closely as possible the timing of everything that happens
// within the original VIC chip.
void core1_entry(void) {

    cvbs_init();
    vic_pio_init();
    vic_memory_init();
    
    //
    // START OF VIC CHIP STATE
    //

    // Hard coded control registers for now (from default PAL VIC).
    uint8_t screenOriginX = 12;         // 7-bit horiz counter value to match for left of video matrix
    uint8_t screenOriginY = 38 * 2;     // 8-bit vert counter value (x 2) to match for top of video matrix
    uint8_t numOfColumns = 22;          // 7-bit number of video matrix columns
    uint8_t numOfRows = 23;             // 6-bit number of video matrix rows
    uint8_t lastCellLine = 7;           // Last Cell Depth Counter value. Depends on double height mode.
    uint8_t backgroundColour = 1;       // 4-bit background colour index
    uint8_t borderColour = 11;          // 3-bit border colour index
    uint8_t auxiliaryColour = 0;        // 4-bit auxiliary colour index
    uint8_t reverse = 0;                // 1-bit reverse state

    // Counters.
    uint16_t videoMatrixCounter = 0;     // 12-bit video matrix counter (VMC)
    uint16_t videoMatrixLatch = 0;       // 12-bit latch that VMC is stored to and loaded from
    uint16_t verticalCounter = 0;        // 9-bit vertical counter (i.e. raster lines)
    uint8_t  horizontalCounter = 0;      // 8-bit horizontal counter (although 1 bit isn't used)
    uint8_t  horizontalCellCounter = 0;  // 8-bit horizontal cell counter (down counter)
    uint8_t  verticalCellCounter = 0;    // 6-bit vertical cell counter (down counter)
    uint8_t  cellDepthCounter = 0;       // 4-bit cell depth counter (sounds either from 0-7, or 0-15)

    // Values normally fetched externally, from screen mem, colour RAM and char mem.
    uint8_t  cellIndex = 0;
    uint8_t  charData = 0;
    uint8_t  colourData = 0;
    uint8_t  colourDataLatch = 0;

    // New Line: Triggered when horiz counter resets during F1, but only exposed during F2, then used in next F1.
    // - Active for only that one cycle.
    // - Used by Cell Depth Counter's increment enabled and reset logic.
    // - Used by "In Matrix" calculation (i.e. are we currently within the video matrix)
    // - Used by Horizontal Cell Counter to reload value to count down from.
    bool newLine = false;

    // Last Line: Triggered when vertical counter value was 311 curing last F2.
    // - Active for the whole line.
    // - Used by "In Matrix" calculation
    // - Used by Vertical Cell Counter to reload value to count down from.
    bool lastLine = false;

    // CDC Last Value: Triggered when Cell Depth Counter contains its last value before reset.
    // - Active for the whole line.
    // - Used to store Video Matrix Counter in its internal latch.
    // - Used when resetting Cell Depth Counter, which in turn triggers Vertical Cell Counter increment.
    bool cdcLastValue = false;

    // Screen Origin X Comparator:
    // - Active for the one cycle when the horizontal counter matches the screen origin X value.
    bool screenXComp = false;

    // Screen Origin Y Comparator:
    // - Active for the whole line when the vertical counter matches the screen origin Y value.
    bool screenYComp = false;

    // In Matrix Y: 
    // - Set when Screen Origin Y matches current Vertical Counter x 2 (2 pixel granularity)
    // - Cleared when Vert Cell Counter has counted down, or its the last line.
    bool inMatrixY = false;

    // In Matrix:
    // - Set when inMatrixY is true and Screen Origin X matches Horizontal Counter
    // - Cleared when Horiz Cell Counter has counted down, or its a new line.
    // - Several cycles pass before different parts of the VIC chip become aware of a change to In Matrix.
    //   - 1st cycle: Screen origin X comparator matches horizontal counter.
    //   - 2nd cycle: Internal (i.e. not yet available to other components) "In Matrix" state changes.
    //   - 3rd cycle: Bus Available signal goes LOW (intended for special "option" version of VIC chip)
    //   - 4th cycle: 'In Matrix' state broadcast to counters, e.g. VMC, HCC.
    //   - 5th cycle: Address Output signal enabled (Could the first fetch be a throwaway bitmap fetch?)
    //   - 6th cycle: Cell Index fetched from screen memory.
    //   - 7th cycle: Character Bitmap data fetched from char memory, loaded into pixel shift register.
    // - SUMMARY: It takes 7 cycles from screen origin X match until pixels for first char are generated.
    //   This is why a screen origin X value of 5 is the first useful value, since horizonal blanking ends
    //   at 12, and 5 + 7 = 12.
    bool inMatrix = false;

    // In Matrix Delay:
    // Purpose: To reproduce the behaviour mentioned above in relation to the timing of In Matrix 
    // propagation and events either side.
    // - Set to 1 and counts up to 7 when entering the matrix. Reset to 0 when it reaches 7.
    // - Set to 8 and counts up to 11 when leaving the matrix. Reset to 0 when it reaches 11.
    uint8_t inMatrixDelay = 0;

    // Matrix Line:
    // - Used by Cell Depth Counter increment logic. Only increments if line was a matrix line.
    bool matrixLine = false;

    // Address Output Enabled: 
    // - Turned on to allow memory fetches 2 cycles before pixel output starts.
    // - Turned off 2 cycles before pixel output stops.
    bool addressOutputEnabled = false;

    // Pixel Output Enabled:
    // - Used to determine if pixels from the pixel shift register should go to video or not.
    // - If false, and blanking isn't active, then current border colour is output instead.
    bool pixelOutputEnabled = false;

    // Signals that control video events during blanking.
    bool hblank = false;
    bool hsync = false;
    bool colourBurst = false;
    bool vblank = false;
    bool vblankPulse = false;
    bool vsync = false;
    bool vsyncPulse = false;
    bool sync = false;
    bool blanking = false;

    // Every half cpu cycle, we output two pixels. The values are temporarily stored in these vars.
    uint32_t pixel1 = 0;
    uint32_t pixel2 = 0;

    //
    // END OF VIC CHIP STATE
    //


    // TODO: Does this need some latency between the PIO and this loop like the ULA has? 

    while (1) {
        // Poll for PIO IRQ 0. This is the rising edge of F1.
        while (!pio_interrupt_get(VIC_PIO, 1)) {
            tight_loop_contents();
        }

        // Clear the IRQ flag immediately for now. 
        pio_interrupt_clear(VIC_PIO, 1);

        // IMPORTANT NOTE: THE SEQUENCE OF ALL THESE CODE BLOCKS BELOW IS IMPORTANT.

        // *************************** PHASE 1 (F1) *****************************

        // The events that happens as part of entering or leaving the video matrix part
        // of the screen all take place during F1, despite being triggered in F2. We do
        // this up front, as other actions that happen during F1 depend on the timing of this.
        if (inMatrixDelay) {
            inMatrixDelay++;

            switch (inMatrixDelay) {
                case 1: break; // This is just the half cycle between inMatrixDely being set and now.
                case 2: break; // 1 cycle delay before Bus Available signal change (reason unknown).
                case 3: break; // Bus Available signal goes LOW here.
                case 4: 
                    // This is when the HCC and VMC counters are told they're now in the matrix.
                    inMatrix = true;
                    break;
                case 5: 
                    // This is when Address Output is enabled, allowing the first memory fetch for this line.
                    addressOutputEnabled = true;
                    break;
                case 6:
                    // Delay to allow time for both the cell index and char data to be fetched.
                    break;
                case 7:
                    // And finally the pixel output starts (from start of F2)
                    pixelOutputEnabled = true;
                    inMatrixDelay = 0;
                    break;

                // While we're in the matrix, and outputting pixels, nothing for inMatrixDelay to do.

                case 8: break;
                case 9: break;
                case 10: break;
                case 11:
                    // We're now leaving the matrix. It happens immediately. HCC/VMC counters stop.
                    inMatrix = false;
                    break;
                case 12:
                    // Address Output disabled a cycle later.
                    addressOutputEnabled = false;
                    break;
                case 13:
                    // Additional 1 cycle delay to allow pixel shift register to unload last 4 bits.
                    break;
                case 14:
                    // And finally, pixel output turned off.
                    // TODO: The timing of the pixel output disable doesn't seem to quite match up.
                    pixelOutputEnabled = false;
                    inMatrixDelay = 0;
                    break;
            }
        }

        // X Decoder - Phase 1 updates
        // - Some X decoder NORs include F1, so don't update until now.
        switch (horizontalCounter) {
            case 11:
                colourBurst = false;
                break;
            case 12:
                hblank = false;
                break;
            case 38:
                vsyncPulse = vblankPulse = false;
                break;
        }

        // Vertical Counter (VC):
        // - During F1, it will either retain value, reset to 0, or increment.
        // - If the value changes, it won't be visible to other components until F2.
        // - If the horiz counter at the end of the F2 just gone was 0, then we increment...
        //   ...unless the vert counter was 311, in which case we reset to 0.
        // - Vertical Counter value is only used by Y Decoder and Screen Y comparator during F2.
        if (horizontalCounter == 0) {
            if (lastLine) {
                verticalCounter = 0;
            } else {
                verticalCounter++;
            }
        }

        // Horizontal Counter (HC):
        // - During F1, horizontal counter will either increment or reset.
        // - But in both cases, the new value of HC isn't seen by other components until F2.
        // - HC reset is simple: If the HC value after the last F2 was 70, then reset HC to 0 in F1.
        // - Horiz Counter value is only used by the X Decoder and Screen X comparator.
        if (horizontalCounter == 70) {
            horizontalCounter = 0;
        } else {
            horizontalCounter++;
        }

        // Horizontal Cell Counter (HCC):
        // Purpose: To count down the columns of the video matrix.
        // - Loaded at the start of each line.
        // - On load, bit 0 is set to 0, and the top 7 bits come from Number of Columns.
        // - If in Matrix, then it decrements on F1, unless its loading.
        // - Value becomes visible to other components in F2
        // - Bit 0 of the counter (HCC0), i.e. the bit not loaded, controls cell index vs char data fetching.
        // - On reaching 0, it clears the In Matrix flag.

        // Cell Depth Counter (CDC):
        // Purpose: To count bitmap lines within a cell. Used during fetch of char bitmap data.
        // - Increments if it was a matrix line and we get a new line, i.e. at end of every matrix line.
        //   ...that is unless it is resetting.
        // - Resets when value is either 7 or 15 (depending on double height mode) and we get new line.

        if (newLine) {
            // Check for Cell Depth Counter reset
            if (cdcLastValue) {
                cellDepthCounter = 0;
                if (!lastLine) {
                    // Vertical Cell Counter decrements when CDC resets, unless its the last line.
                    verticalCellCounter--;
                }
            }
            else if (matrixLine) {
                // If the line that just ended was a video matrix line, then increment CDC.
                cellDepthCounter++;
            }

            // Horizontal Cell Counter is loaded at the start of each line with num of columns.
            horizontalCellCounter = (numOfColumns << 1);
        }
        else if (inMatrix) {
            // Horizontal Cell Counter decrements on every cycle while in matrix (unless loading).
            // Note: In Matrix turned off when horizontal cell counter hits zero. No need to check that here.
            horizontalCellCounter--;
        }

        // Vertical Cell Counter (VCC):
        // Purpose: To count down the rows of the video matrix.
        // - Loaded on every cycle of last line (311) with the number of rows value (probably an optimisation)
        // - Decrements on Cell Depth Counter reset, unless its loading (see implementation above)
        // - On reaching 0, clears In Matrix Y flag (see implementation above)
        if (lastLine) {
            verticalCellCounter = numOfRows;
        }
        
        // Video Matrix Counter (VMC):
        // Purpose: To keep track of current video matrix character position in screen memory.
        // - When In Matrix, it increments on every cycle, unless loading or resetting.
        // - Comes with an associated latch that it both stores to and loads from.
        // - Stores counter into latch on every cycle of last Cell Depth Counter line (probably an optimisation)
        // - Store takes the value of counter BEFORE increment.
        // - Counter is loaded from the latch on new line signal.
        // - Latch is cleared to 0 on each long VSYNC pulse, i.e. 2 times each on lines 4, 5 and 6.
        //   (probably an optimisation and not strictly required)
        // - Counter value is halved during address calculation, i.e. only top 11 bits used, bit-0 ignored.
        if (vsyncPulse) { 
            videoMatrixLatch = 0;
        }
        else if (newLine) {
            videoMatrixCounter = videoMatrixLatch;
        }
        else if (inMatrix) {
            // Important: Latch store happens BEFORE increment.
            if (cdcLastValue) {
                videoMatrixLatch = videoMatrixCounter;
            }
            // Always incremenets when in matrix, and is not loading from latch.
            //videoMatrixCounter++;
        }

        // At the end of F1, if 'In Matrix', then the data read from memory arrives.
        if (addressOutputEnabled) {
            if (horizontalCellCounter & 1) {
                // TODO: This is where we read the cell index from VIC PIO.
                // TODO: We will instead read from local RAM, which has a copy of what the CPU put into the external RAM.
                cellIndex = xram[ADDR_UNEXPANDED_SCR + videoMatrixCounter];
                
                // Colour data comes in at this point but not "used" until chardata comes in.
                colourDataLatch = xram[ADDR_COLOUR_RAM + videoMatrixCounter];

                // EXPERIMENTAL: Not where it happens in the real chip, but let's try this.
                videoMatrixCounter++;
            }
            else {
                // TODO: This is where we read the char data from VIC PIO.
                // TODO: We will instead read from local RAM, which has a copy of what the CPU put into the external RAM.
                // In this emulation, charData acts as the pixel shift register. We load it here, i.e. end
                // of F1 / start of F2. In the real chip, it happens at the very start of F2.
                charData = xram[ADDR_UPPERCASE_GLYPHS_CHRSET + (cellIndex << 3) + cellDepthCounter];

                // Now that the char data is available, we can let the colour data "in".
                colourData = colourDataLatch;
            }
        }

        // Shift out two pixels during F1. We do this before fetching the data below, because 
        // char data fetched in F1 doesn't get output until start of F2.
        charData = outputPixels(verticalCounter, horizontalCounter, sync, colourBurst, blanking, pixelOutputEnabled, 
            hsync, hblank, vsync, vblank, vblankPulse, vsyncPulse, charData, colourData, backgroundColour,
            borderColour, auxiliaryColour, pixel1, pixel2);

        // TODO: Should we poll here for phase 2 via another interrupt? e.g. irq 1


        // ***************************** PHASE 2 (F2) *******************************
        // Everything calculated below this line can only happen in F2 and are static during F1.

        // Re-calculated in F2 of every cycle (not F1). The values are however tested in F1 multiple times.
        cdcLastValue = (cellDepthCounter == lastCellLine);

        // X Decoder - Phase 2 updates:
        // - Since horiz counter only exposes a new value on F2, then most X decoder values update in F2.
        // - This isn't true for all cases, as some include F1 in the NOR, so don't change until F1.
        // - This is why some X decoding is also done at the start of F1.
        switch (horizontalCounter) {
            case 0:
                newLine = true;
                break;
            case 1:
                newLine = false;
                // Horizontal sync is only triggered when vertical blanking is not active.
                hsync = !vblank;
                break;
            case 2:
                vblankPulse = false;
                break;
            case 6:
                hsync = false;
                vblankPulse = true;
                break;
            case 7:
                // Colour burst is only triggered when vertical blanking is not active.
                colourBurst = !vblank;
                break;
            case 41:
                vblankPulse = true;
                break;
            case 70:
                hblank = true;
                break;
        }

        // Y Decoder - Phase 2 updates for vertical sync and blanking.
        if (newLine) {
            switch (verticalCounter) {
                case 0:
                    lastLine = false;
                    break;
                case 1:
                    vblank = true;
                    break;
                case 4:
                    vsync = true;
                    break;
                case 7:
                    vsync = false;
                    break;
                case 10:
                    vblank = false;
                    break;
                case 311:
                    lastLine = true;
                    break;
            }

            // Screen Origin Y comparator is also here for efficency.
            screenYComp = (verticalCounter == screenOriginY);
        }

        // Clears video matrix latch, on each of the long syncs during vsync (see code above)
        vsyncPulse = (vsync && vblankPulse);

        // 'In Matrix' calculations, i.e. are we within the video matrix at the moment?
        // 'In Matrix Y' cleared on either last line or vert cell counter reaching it last value.
        if (lastLine || (verticalCellCounter == 0)) {
            // Note that these two flags are subtly and deliberately different.
            inMatrixY = false;
            matrixLine = false;
        }
        // Otherwise, 'In Matrix Y' set on vertical counter matching screen origin Y.
        else if (screenYComp) {
            inMatrixY = true;
        }
        // 'In Matrix' is cleared on either a new line or on horiz cell counter reaching its last value.
        if (newLine || (horizontalCellCounter == 0)) {
            inMatrixDelay = 8;
        }
        // Otherwise 'In Matrix' set on horiz counter matching screen origin X, also 'In Matrix Y'
        else if (inMatrixY && screenXComp) {
            inMatrixDelay = 1;
            matrixLine = true;
        }

        // Screen origin X comparator:
        // - We do screen origin X comparator after 'In Matrix' checks because of a 1 cycle gap 
        //   between the comparison result and internal changes to 'In Matrix' state.
        screenXComp = (horizontalCounter == screenOriginX);
        
        // Shift out two more pixels during F2.
        charData = outputPixels(verticalCounter, horizontalCounter, sync, colourBurst, blanking, pixelOutputEnabled, 
            hsync, hblank, vsync, vblank, vblankPulse, vsyncPulse, charData, colourData, backgroundColour,
            borderColour, auxiliaryColour, pixel1, pixel2);

        // TODO: If 'In Matrix', calculate address to fetch in next F1.


        // DEBUG: Temporary check to see if we've overshot the 120 cycle allowance.
        if (pio_interrupt_get(VIC_PIO, 1)) {
           overruns = 1;
        }
    }
}

void vic_init(void) {
    multicore_launch_core1(core1_entry);
}

void vic_task(void) {
    if (overruns > 0) {
        printf("X.");
        overruns = 0;
    }
}
