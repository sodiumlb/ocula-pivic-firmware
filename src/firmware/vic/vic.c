/*
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
//#include "sys/ria.h"
#include "vic/vic.h"
#include "sys/mem.h"
#include "vic.pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>

void core1_entry(void) {

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

    // DEBUGGING variables
    uint32_t frames = 0;

    // Hard coded control registers for now (from default PAL VIC).
    uint8_t screenOriginX = 12;         // 7-bit horiz counter value to match for left of video matrix
    uint8_t screenOriginY = 38 * 2;     // 8-bit vert counter value (x 2) to match for top of video matrix
    uint8_t numOfColumns = 22;          // 7-bit number of video matrix columns
    uint8_t numOfRows = 23;             // 6-bit number of video matrix rows
    uint8_t lastCellLine = 7;           // Last Cell Depth Counter value. Depends on double height mode.

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
    bool inMatrix = false;

    // Matrix Line:
    // - Used by Cell Depth Counter increment logic. Only increments if line was a matrix line.
    bool matrixLine = false;

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
        while (!pio_interrupt_get(VIC_PIO, 0)) {
            tight_loop_contents();
        }

        // IMPORTANT NOTE: THE SEQUENCE OF ALL THESE CODE BLOCKS BELOW IS IMPORTANT.

        // *************************** PHASE 1 (F1) *****************************

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
                    printf(".|");
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

        // At the end of F1, if 'In Matrix', then the data read from memory arrives.
        if (inMatrix) {

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

            }
        }

        // TODO: Top bit of pixel shift register comes out immediately.



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

        // Sync level output is triggered in three scenarios:
        // - When hsync is true and vblank is not true (i.e. hsync doesn't happen during vertical blanking)
        // - When vblank is true and vblankPulse is not true (short syncs during lines 1-3 and 7-9)
        // - When vsync is true and vblankPulse is also true (long syncs during lines 4-6)
        sync = (hsync || (vblank && !vblankPulse) || vsyncPulse);

        // Blanking is much simpler by comparison.
        blanking = (vblank || hblank);

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
            inMatrix = false;
        }
        // Otherwise 'In Matrix' set on horiz counter matching screen origin X, also 'In Matrix Y'
        else if (inMatrixY && screenXComp) {
            inMatrix = true;
            matrixLine = true;
        }

        // Screen origin X comparator:
        // - We do screen origin X comparator after 'In Matrix' checks because of a 1 cycle gap 
        //   between the comparison result and internal changes to 'In Matrix' state.
        screenXComp = (horizontalCounter == screenOriginX);
        


        // TODO: Shift out two more pixels somewhere around here.
        // TODO: If 'In Matrix', calculate address to fetch in next F1.


        // TODO: Should this be cleared at end of loop? Or immediately after polling?
        pio_interrupt_clear(VIC_PIO, 0);
    }
}

void vic_init(void) {
    multicore_launch_core1(core1_entry);
}

void vic_task(void) {
    // TODO: This is where core0 would process something from core1, if required.
}
