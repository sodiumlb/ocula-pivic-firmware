/*
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
//#include "sys/ria.h"
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


// EXPERIMENTAL TEST DEFINES:
// TODO: Decide where to put these.
#define CVBS_DELAY_CONST_POST (15-3)
#define CVBS_CMD(L0,L1,DC,delay,count) \
         ((((CVBS_DELAY_CONST_POST-delay)&0xF)<<23) |  ((L1&0x1F)<<18) | (((count-1)&0x1FF)<<9) |((L0&0x1F)<<4) | ((delay&0x0F)))
#define CVBS_REP(cmd,count) ((cmd & ~(0x1FF<<9)) | ((count-1) & 0x1FF)<<9)
// As an experiement, everything is currently set to repeat of 3 (i.e. duration of one F1/F2 cycle)
#define PAL_SYNC        CVBS_CMD( 0, 0, 0, 0,1)
#define PAL_BLANK       CVBS_CMD(18,18,18, 0,1)
#define PAL_BURST_O     CVBS_CMD(6,12,9,11,1)
#define PAL_BURST_E     CVBS_CMD(12,6,9,4,1)
#define PAL_BLACK       CVBS_CMD(18,18,9,0,1)
#define PAL_WHITE       CVBS_CMD(23,23,29,0,1)
// TESTING: Experimenting with a black and while screen for now, so tweaking the above in testing.
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

// END OF EXPERIMENTAL DEFINES>

#define CVBS_TX  CVBS_PIO->txf[CVBS_SM]

volatile uint32_t overruns = 0;


static void inline __attribute__((always_inline)) outputPixels(
        uint16_t verticalCounter, uint8_t horizontalCounter, 
        bool sync, bool colourBurst, bool blanking, bool pixelOutputEnabled,
        bool hsync, bool hblank, bool vblank, bool vblankPulse, bool vsyncPulse,
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
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SYNC);
            //CVBS_TX = PAL_SYNC;
            pixel1 = pixel2 = PAL_SYNC;
        }
        else if (colourBurst) {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BURST_O);
            //CVBS_TX = PAL_BURST_O;
            pixel1 = pixel2 = PAL_BURST_O;
        }
        else if (blanking) {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BLANK); 
            //CVBS_TX = PAL_BLANK;
            pixel1 = pixel2 = PAL_BLANK;
        }
        else if (pixelOutputEnabled) {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BLACK);
            //CVBS_TX = PAL_BLACK;
            //pixel1 = pixel2 = pal_palette_o[horizontalCounter & 0xF];

            // Normal graphics for now (i.e. no reverse and multicolour)
            pixel1 = pal_palette_o[((charData & 0x80) == 0 ? backgroundColour : colourData)];
            pixel2 = pal_palette_o[((charData & 0x40) == 0 ? backgroundColour : colourData)];

        }
        else {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_WHITE);
            //CVBS_TX = PAL_WHITE;
            //pixel1 = pixel2 = PAL_WHITE;
            pixel1 = pixel2 = pal_palette_o[borderColour];
        }
    } else {
        // Even
        if (sync) {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_SYNC);
            //CVBS_TX = PAL_SYNC;
            pixel1 = pixel2 = PAL_SYNC;
        }
        else if (colourBurst) {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BURST_E);
            //CVBS_TX = PAL_BURST_E;
            pixel1 = pixel2 = PAL_BURST_E;
        }
        else if (blanking) {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BLANK);
            //CVBS_TX = PAL_BLANK;
            pixel1 = pixel2 = PAL_BLANK;
        }
        else if (pixelOutputEnabled) {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_BLACK);
            //CVBS_TX = PAL_BLACK;
            //pixel1 = pixel2 = pal_palette_e[horizontalCounter & 0xF];

            pixel1 = pal_palette_e[((charData & 0x80) == 0 ? backgroundColour : colourData)];
            pixel2 = pal_palette_e[((charData & 0x40) == 0 ? backgroundColour : colourData)];
        }
        else {
            //pio_sm_put(CVBS_PIO, CVBS_SM, PAL_WHITE);
            //CVBS_TX = PAL_WHITE;
            //pixel1 = pixel2 = PAL_WHITE;
            pixel1 = pixel2 = pal_palette_e[borderColour];
        }
    }

    pio_sm_put(CVBS_PIO, CVBS_SM, pixel1);
    pio_sm_put(CVBS_PIO, CVBS_SM, pixel2);
}

void core1_entry(void) {

    // TODO: Decide where it makes sense to initialise the CVBS PIO.
    cvbs_init();

    // Set up a test page for the screen memory.
    memcpy((void*)(&xram[ADDR_UPPERCASE_GLYPHS_CHRSET]), (void*)vic_char_rom, sizeof(vic_char_rom));
    memset((void*)&xram[ADDR_UNEXPANDED_SCR], 0x20, 1024);
    sprintf((char*)(&xram[ADDR_UNEXPANDED_SCR]), "PIVIC TEST " __DATE__);
    sprintf((char*)(&xram[ADDR_UNEXPANDED_SCR] + 22), "0123456789012345678901");

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

    uint32_t pixel = 0;
    uint32_t pixel1 = 0;
    uint32_t pixel2 = 0;

    // DEBUGGING variables
    uint32_t frames = 0;

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

    uint16_t videoMatrixCounter = 0;     // 12-bit video matrix counter (VMC)
    uint16_t videoMatrixLatch = 0;       // 12-bit latch that VMC is stored to and loaded from
    uint16_t verticalCounter = 0;        // 9-bit vertical counter (i.e. raster lines)
    uint8_t  horizontalCounter = 0;      // 8-bit horizontal counter (although 1 bit isn't used)
    uint8_t  horizontalCellCounter = 0;  // 8-bit horizontal cell counter (down counter)
    uint8_t  verticalCellCounter = 0;    // 6-bit vertical cell counter (down counter)
    uint8_t  cellDepthCounter = 0;       // 4-bit cell depth counter (sounds either from 0-7, or 0-15)
    
    uint8_t  pixelShiftRegister = 0;     // 8-bit shift register for shifting out pixels.

    // Values normally fetched externally, from screen mem and char mem.
    uint8_t  cellIndex = 0;
    uint8_t  charData = 0;
    uint8_t  colourData = 0;

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
    bool colourEnable = false;
    bool blackOrWhite = false;

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

                case 8:
                    // We're now leaving the matrix. It happens immediately. HCC/VMC counters stop.
                    inMatrix = false;
                    break;
                case 9:
                    // Address Output disabled a cycle later.
                    addressOutputEnabled = false;
                    break;
                case 10:
                    // Additional 1 cycle delay to allow pixel shift register to unload last 4 bits.
                    break;
                case 11:
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
                vblankPulse = false;
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

                // DEBUG: Should print every second, due to PAL 50Hz.
                frames++;
                if (frames == 50) {
                    frames = 0;
                }

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
            videoMatrixCounter++;
        }

        // If pixel output is enabled, then we shift out two pixels in F1. We do 
        // this before fetching the data below, because char data fetched in F1
        // doesn't get output until start of F2.
        // if (pixelOutputEnabled) {

        // }

        outputPixels(verticalCounter, horizontalCounter, sync, colourBurst, blanking, pixelOutputEnabled, 
            hsync, hblank, vblank, vblankPulse, vsyncPulse, charData, colourData, backgroundColour,
            borderColour, auxiliaryColour, pixel1, pixel2);

        // At the end of F1, if 'In Matrix', then the data read from memory arrives.
        if (addressOutputEnabled) {

            // TODO: Shift out two pixels somewhere around here.

            if (horizontalCellCounter & 1) {
                // TODO: This is where we read the cell index from VIC PIO.
                // TODO: We will instead read from local RAM, which has a copy of what the CPU put into the external RAM.
                cellIndex = 0;
                colourData = 3;
            }
            else {
                // TODO: This is where we read the char data from VIC PIO.
                // TODO: We will instead read from local RAM, which has a copy of what the CPU put into the external RAM.
                charData = 0b10101011;

                // TODO: Load the pixel shift register.
                // TODO: In the real chip, the shift register is loaded at the exact start of F2.
                pixelShiftRegister = charData;
            }
        }


        // TODO: Should we poll here for phase 2 via another interrupt? e.g. irq 1


        // ***************************** PHASE 2 (F2) *******************************
        // Everything calculated below this line can only happen in F2 and are static during F1.

        // Re-calculated in F2 of every cycle (not F1). The values are however tested in F1 multiple times.
        newLine = (horizontalCounter == 0);
        lastLine = (verticalCounter == 311);
        cdcLastValue = (cellDepthCounter == lastCellLine);

        // X Decoder - Phase 2 updates:
        // - Since horiz counter only exposes a new value on F2, then most X decoder values update in F2.
        // - This isn't true for all cases, as some include F1 in the NOR, so don't change until F1.
        // - This is why some X decoding is also done at the start of F1.
        switch (horizontalCounter) {
            case 1:
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

        // Y Decoder - Phase 2 updates
        // -
        if (newLine) {
            switch (verticalCounter) {
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
            }

            // Screen Origin Y comparator is also here for efficency.
            screenYComp = (verticalCounter == screenOriginY);
        }

        // Clears video matrix latch, on each of the long syncs during vsync (see code above)
        vsyncPulse = (vsync && vblankPulse);

        // TODO: Remove at some point, as this has been moved into the outputPixels inline function.
        // Sync level output is triggered in three scenarios:
        // - When hsync is true and vblank is not true (i.e. hsync doesn't happen during vertical blanking)
        // - When vblank is true and vblankPulse is not true (short syncs during lines 1-3 and 7-9)
        // - When vsync is true and vblankPulse is also true (long syncs during lines 4-6)
        //sync = (hsync || (vblank && !vblankPulse) || vsyncPulse);

        // TODO: Remove at some point, as this has been moved into the outputPixels inline function.
        // Blanking is much simpler by comparison.
        //blanking = (vblank || hblank);

        // Colour Enable controls whether chroma output pin outputs anything.
        // - Chroma output is not enabled for B/W and blanking, unless colour burst is active.
        // TODO: Not entirely sure we need this one. If we know the colour, we just lookup the CVBS data.
        //colourEnable = ((blackOrWhite || blanking) && !colourBurst);


        // Screen origin X/Y comparators are done as part of In Matrix calculations below.
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
        


        // TODO: Shift out two more pixels somewhere around here.

        outputPixels(verticalCounter, horizontalCounter, sync, colourBurst, blanking, pixelOutputEnabled, 
            hsync, hblank, vblank, vblankPulse, vsyncPulse, charData, colourData, backgroundColour,
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
