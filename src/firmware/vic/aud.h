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

typedef union {
    uint32_t all;
    uint8_t ch[4];
} aud_union_t;


extern aud_union_t aud_counters;
extern aud_union_t aud_ticks;

 void aud_init(void);
 void aud_task(void);

 void aud_print_status(void);
 
 // Per CPU clock tick audio progress
 // Assembly version. Assumes running on other core than aud_task(), uses doorbell signaling
 void inline __attribute__((always_inline)) aud_tick_inline(void){
    //Running 4 separate tick and channel counters in packed 32 bit words
    uint32_t tmp;
    const uint32_t zero = 0;
    const uint32_t one = 0x01010101;
    const uint32_t mask = 0x1F03070F;
    const uint32_t *regs = (uint32_t*)(&xram[0x100a]);
    uint32_t upd;
    asm volatile (
        "uadd8 %[tick], %[tick], %[one]\n\t"        //Increment ticks
        "and %[tmp], %[tick], %[mask]\n\t"          //Mask ticks lower bits for testing
        "ssub8 %[tmp], %[zero], %[tmp]\n\t"         //Idenitify ticks of interrest, if zero result GE bits are set
        "sel %[tmp], %[one], %[zero]\n\t"           //Pick bytes from "one" if GE bit=1 else from "zero"
        "sadd8 %[cntr], %[cntr], %[tmp]\n\t"        //Increment only counters for ticks of interest (zero in lower bits) 
        "sadd8 %[cntr], %[cntr], %[zero]\n\t"       //First sadd8 doesn't sign-extend our 0x80 MSB. This does and GE bits updates, GE=0 if overlow
        "mrs %[upd], apsr\n\t"                      //Get status bits (incl GE) in upd
        "mvn %[upd], %[upd], lsr 16\n\t"            //Get inverted GE bits in [3:0]
        "ldr %[tmp], %[regs]\n\t"                   //Load counter reload values
        "and %[tmp], %[tmp], #0x7F7F7F7F\n\t"       //Mask out 8th bit in each counter (enable bit from registers)
        "sel %[cntr], %[cntr], %[tmp]\n\t"          //Pick bytes from reload values if GE bit=0 else from old (unchange)
        : [tick] "+r" (aud_ticks.all),
          [cntr] "+r" (aud_counters.all),
          [tmp]  "=r" (tmp),
          [upd]  "=r" (upd)
        : [zero] "r"  (zero),          
          [one]  "r"  (one),
          [mask] "r"  (mask),
          [regs] "m" (*(uint32_t(*))regs)
    );
    sio_hw->doorbell_out_set = upd & 0xF;           //Using RP2350 doorbells 0-3 to signal updates needed
}
 #endif /* _AUD_H_ */