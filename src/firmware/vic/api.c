/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "vic/api.h"
#include "vic/mem.h"
#include "sys/mem.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdio.h>

// Secret knock to turn off vic register aliasing and exposing the extended API registers at 0x9040 to 0x907F
// Poke "P"/80/0x50 to 37632/0x9300 

// API register reside above the VIA window (0x1010-0x1030)

void api_init(void){
    //Test stub set colour reg: SYS 36928
    xram[0x1040] = 0xA9;    //LDA #89
    xram[0x1041] = 0x59;
    xram[0x1042] = 0x8D;    //STA 0x900F (VIC colour reg)
    xram[0x1043] = 0x0F;
    xram[0x1044] = 0x90;
    xram[0x1045] = 0x60;    //RTS
}
 
void api_task(void){
    if(xram[0x1300] == 0x50){
        mem_alias_enable(false);
        xram[0x1300] = 0;           //reset knock register 
    }
}
