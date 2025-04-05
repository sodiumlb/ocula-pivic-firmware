/*
* Copyright (c) 2025 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
//#include "sys/ria.h"
#include "oric/ula.h"
#include "sys/mem.h"
#include "ula.pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>

void core1_loop(void){
    __wfe();    //Just placeholder.
}

void ula_init(void){
    multicore_launch_core1(core1_loop);

}
void ula_task(void){
    
}