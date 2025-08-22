/*
 * Copyright (c) 2025 dreamseal
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #ifndef _AUD_H_
 #define _AUD_H_

 #include "sys/mem.h"
 #include "pico/multicore.h"
 #include <stdio.h>

typedef union {
    uint32_t all;
    uint8_t ch[4];
} aud_union_t;


extern volatile aud_union_t aud_counters;
extern volatile aud_union_t aud_ticks;
extern volatile aud_union_t aud_regs;
extern volatile aud_union_t aud_sr;

 void aud_init(void);
 void aud_task(void);

 void aud_print_status(void);
 
 // Per CPU clock tick audio progress
 // Assembly version. Assumes running on other core than aud_task(), uses doorbell signaling
 void inline __attribute__((always_inline)) aud_tick_inline(uint32_t *regs){
    //Running 4 separate tick and channel counters in packed 32 bit words
    uint32_t tmp,tmp2,zero,one,upd;
    const uint32_t mask = 0x0103070F;
    //uint32_t *regs = (uint32_t*)(&xram[0x100a]);
    asm volatile (
        "mov %[zero], #0x00000000\n\t"              //Create 4x zero vector
        "mov %[one],  #0x01010101\n\t"              //Create 4x one vector
        "uadd8 %[tick], %[tick], %[one]\n\t"        //Increment ticks
        "and %[tmp], %[tick], %[mask]\n\t"          //Mask ticks lower bits for testing
        "ssub8 %[tmp], %[zero], %[tmp]\n\t"         //Idenitify ticks of interrest, if zero result GE bits are set
        "sel %[tmp], %[one], %[zero]\n\t"           //Pick bytes from "one" if GE bit=1 else from "zero"
        "sadd8 %[cntr], %[cntr], %[tmp]\n\t"        //Increment only counters for ticks of interest (zero in lower bits) 
        "ldr %[tmp2], %[regs]\n\t"                  //Load counter reload values
        "sadd8 %[tmp], %[tmp2], %[one]\n\t"         //Counter values increment on reload. Do it here to not change GE bits needed later.
        "sadd8 %[cntr], %[cntr], %[zero]\n\t"       //First counter sadd8 doesn't sign-extend our 0x80 MSB. This does and GE bits updates, GE=0 if overlow
        "sel %[aregs], %[aregs], %[tmp2]\n\t"       //Update aud_regs snap-shot for channels where counter has overflown
        "mrs %[upd], apsr\n\t"                      //Get status bits (incl GE) in upd
        "mvn %[upd], %[upd], lsr 16\n\t"            //Get inverted GE bits in [3:0]
        "and %[tmp2], %[tmp], #0x7F7F7F7F\n\t"      //Mask out 8th bit in each counter (enable bit from registers)
        "sel %[cntr], %[cntr], %[tmp2]\n\t"         //Pick bytes from reload values if GE bit=0 else from old (unchange)
        "bic %[tmp2], %[tmp], %[sr]\n\t"            //Invert SR then AND with enable bits in MSB per byte
        "and %[tmp2], %[one], %[tmp2], lsr 7\n\t"   //Shift from MSB to LSB and mask with ones to get new SR LSBs
        "bic %[tmp], %[sr], %[one], lsl 7\n\t"      //Clear SR MSBs in preparatin for shifting
        "orr %[tmp], %[tmp2], %[tmp], lsl 1\n\t"    //Shift SR up 1 bit then OR in new SR LSBs
        "sel %[sr], %[sr], %[tmp]\n\t"              //Update SR for channels where counter has overflown
        : [tick] "+r" (aud_ticks.all),
          [cntr] "+r" (aud_counters.all),
          [aregs]"+r" (aud_regs.all),
          [sr]   "+r" (aud_sr.all),
          [tmp]  "=r" (tmp),
          [tmp2] "=r" (tmp2),
          [upd]  "=r" (upd),
          [zero] "=r" (zero),
          [one]  "=r" (one)
        : [mask] "r"  (mask),
          [regs] "m" (*(uint32_t(*))regs)
        : "cc"                                      //Conditional flags clobbered
    );
    //sio_hw->doorbell_out_set = upd & 0xF;           //Using RP2350 doorbells 0-3 to signal updates needed
    sio_hw->fifo_wr = (aud_regs.all & 0xFFFFFFF0) | (upd & 0xF);
}
 #endif /* _AUD_H_ */