/*
* Copyright (c) 2025 dreamsel
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

void core1_loop(void){
    __wfe();    //Just placeholder.
}

void vic_init(void){
    multicore_launch_core1(core1_loop);

}
void vic_task(void){
    
}