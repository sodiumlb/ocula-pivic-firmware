/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "main.h"
#include "sys/rev.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

/*  Hardware revision auto detect for PIVIC
    Rev 1.0 is unsupported
    Rev 1.1 has single 5-bit CVBS DAC
    Rev 1.2 has dual 2+5-bit CVBS DAC
    Probing to see if there is connection across
    the two specific DAC bits. True on 1.1, false on 1.2.
*/
static rev_t rev = REV_UNDEF;
void rev_init(void){
    gpio_init(CVBS_LUMA_PIN_BASE-1);
    gpio_set_dir(CVBS_LUMA_PIN_BASE-1, false);
    gpio_set_input_enabled(CVBS_LUMA_PIN_BASE-1, true);
    gpio_set_pulls(CVBS_LUMA_PIN_BASE-1, false, false);
    gpio_set_pulls(CVBS_LUMA_PIN_BASE-2, false, false);
    gpio_set_pulls(CVBS_LUMA_PIN_BASE+1, false, false);
    gpio_set_pulls(CVBS_LUMA_PIN_BASE+2, false, false);
    gpio_init(CVBS_LUMA_PIN_BASE);
    gpio_set_dir(CVBS_LUMA_PIN_BASE, true);
    gpio_put(CVBS_LUMA_PIN_BASE, true);
    sleep_us(5);
    if(gpio_get(CVBS_LUMA_PIN_BASE-1)){
        rev = REV_1_1;
    }else{
        rev = REV_1_2;
    }
}

rev_t rev_get(void){
    return rev;
}

void rev_print(void){
    printf("Hardware Rev ");
    switch(rev){
        case(REV_1_1):
            puts("1.1");
            break;
        case(REV_1_2):
            puts("1.2");
            break;
        default:
            puts("Undefined\n");
            break;
    }
}