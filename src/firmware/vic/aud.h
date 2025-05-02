/*
 * Copyright (c) 2025 dreamseal
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #ifndef _AUD_H_
 #define _AUD_H_

 #include "sys/mem.h"

typedef union {
    uint32_t all;
    uint8_t ch[4];
} aud_union_t;


extern volatile aud_union_t aud_counters;
extern volatile aud_union_t aud_update;
extern aud_union_t aud_ticks;

 void aud_init(void);
 void aud_task(void);
 
 // Per CPU clock tick audio progress
 void aud_tick(void);
 // Assembly version, use either or
 void inline __attribute__((always_inline)) aud_tick_inline(void){
    //Running 4 separate tick and channel counters in single words
    uint32_t tmp, tmp2;
    const uint32_t zero = 0;
    const uint32_t one = 0x01010101;
    uint32_t *regs = (uint32_t*)(&xram[0x100a]);
    asm(
        "uadd8 %[tick], %[tick], %[one]\n\t"        //Increment ticks
        //"and %[tmp], %[tick], #0x1F03070F\n\t"
        "movw %[tmp2], #0x070F\n\t"        
        "movt %[tmp2], #0x1F03\n\t"        
        "and %[tmp], %[tick], %[tmp2]\n\t"          //Mask ticks lower bits for testing
        "ssub8 %[tmp], %[zero], %[tmp]\n\t"         //Do zero-ticks, if zero result GE bits are set
        "sel %[tmp], %[one], %[zero]\n\t"           //Pick bytes from "one" if GE bit=1 else from "zero"
        "sadd8 %[cntr], %[cntr], %[tmp]\n\t"        //Increment only counters with zero in lower bits. GE bits updated - GE=1 if overlow
        "sel %[tmp], %[zero], %[one]\n\t"           //Pick bytes from "zero" if GE bit=1 else from "one"
        "orr %[upd], %[upd], %[tmp]\n\t"            //Mark overflow channels for update
        "ldr %[tmp2], %[regs]\n\t"                  //Load counter reload values
        "sel %[cntr], %[tmp2], %[cntr]\n\t"         //Pick bytes from reload values if GE bit=1 else from old (unchange)
        "and %[cntr], %[cntr], #0x7F7F7F7F\n\t"     //Mask out 8th bit in each counter (enable bit from registers)

        : [upd]  "+r" (aud_update.all),
          [tick] "+r" (aud_ticks.all),
          [cntr] "+r" (aud_counters.all),
          [tmp]  "=r" (tmp), 
          [tmp2] "=r" (tmp2)
        : [zero] "r"  (zero),          
          [one]  "r"   (one),
          [regs] "m" (*(uint32_t(*))regs)
    );
}
 #endif /* _AUD_H_ */