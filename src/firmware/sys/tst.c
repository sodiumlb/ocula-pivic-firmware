/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "sys/tst.h"
#include "sys/rev.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include <stdio.h>
#include "str.h"

static bool testing;

static uint64_t toggled;
static uint64_t prev;

const char *const tst_labels[] = {
    "\033[31m0\033[39m",    //red 0
    "\033[31m1\033[39m",    //red 1
    "\033[32m0\033[39m",    //green 0
    "\033[32m1\033[39m",    //green 1
}; 

#define TST_MASK(bitmap,n) (bitmap & (1ull << n) ? 1 : 0) 
#define TST(col,val,n) tst_labels[TST_MASK(col,n)<<1 | TST_MASK(val,n)]
void tst_init(void){
    testing = false;
    toggled = 0;
    prev = gpio_get_all64();
}

void tst_task(void){
    static absolute_time_t print_timer = 0;
    if(testing){
        toggled |= prev ^ gpio_get_all64();
        prev = gpio_get_all64();
        if(absolute_time_diff_us(get_absolute_time(),print_timer) < 0){
            tst_print_status();
            print_timer = delayed_by_ms(get_absolute_time(),100);
        }
    }
}

void tst_print_status(void){
#ifdef PIVIC
    uint8_t potx, poty;
    switch(rev_get()){
        case(REV_1_1):
            potx = POTX_PIN_1_1;
            poty = POTY_PIN_1_1;
            break;
        case(REV_1_2):
        case(REV_1_3):
            potx = POTX_PIN_1_2;
            poty = POTY_PIN_1_2;
            break;
        default:
            puts("Test print error: !Unknown hardware revision\n");
            return;
    }
    printf( "\033[s\033[0;0H");
    printf( "__________________________\n");
    printf( "  NC 01 -   USB    40 VDD \n"
            " CHR 02 <        - 39 XT1 \n"
            " LUM 03 <        - 38 XT2 \n");
    printf( " RNW 04 > %s    %s < 37 OPT \n", TST(toggled,prev,RNW_PIN), TST(toggled,prev,PEN_PIN));
    printf( " D11 05 -        - 36 PHI2\n"
            " D10 06 -        > 35 PHI1\n");
    printf( "  D9 07 -      %s < 34 A13 \n", TST(toggled,prev,ADDR_PIN_BASE+13));
    printf( "  D8 08 -      %s < 33 A12 \n", TST(toggled,prev,ADDR_PIN_BASE+12));
    printf( "  D7 09 > %s    %s < 32 A11 \n", TST(toggled,prev,DATA_PIN_BASE+7), TST(toggled,prev,ADDR_PIN_BASE+11));
    printf( "  D6 10 > %s    %s < 31 A10 \n", TST(toggled,prev,DATA_PIN_BASE+6), TST(toggled,prev,ADDR_PIN_BASE+10));
    printf( "  D5 11 > %s    %s < 30 A9  \n", TST(toggled,prev,DATA_PIN_BASE+5), TST(toggled,prev,ADDR_PIN_BASE+9));
    printf( "  D4 12 > %s    %s < 29 A8  \n", TST(toggled,prev,DATA_PIN_BASE+4), TST(toggled,prev,ADDR_PIN_BASE+8));
    printf( "  D3 13 > %s    %s < 28 A7  \n", TST(toggled,prev,DATA_PIN_BASE+3), TST(toggled,prev,ADDR_PIN_BASE+7));
    printf( "  D2 14 > %s    %s < 27 A6  \n", TST(toggled,prev,DATA_PIN_BASE+2), TST(toggled,prev,ADDR_PIN_BASE+6));
    printf( "  D1 15 > %s    %s < 26 A5  \n", TST(toggled,prev,DATA_PIN_BASE+1), TST(toggled,prev,ADDR_PIN_BASE+5));
    printf( "  D0 16 > %s    %s < 25 A4  \n", TST(toggled,prev,DATA_PIN_BASE+0), TST(toggled,prev,ADDR_PIN_BASE+4));
    printf( "POTX 17 > %s    %s < 24 A3  \n", TST(toggled,prev,potx), TST(toggled,prev,ADDR_PIN_BASE+3));
    printf( "POTY 18 > %s    %s < 23 A2  \n", TST(toggled,prev,poty), TST(toggled,prev,ADDR_PIN_BASE+2));
    printf( " AUD 19 <      %s < 22 A1  \n", TST(toggled,prev,ADDR_PIN_BASE+1));
    printf( " VSS 20        %s < 21 A0  \n", TST(toggled,prev,ADDR_PIN_BASE+0));
    printf( "__________________________\n");
    printf( "\033[u");
#endif
#ifdef OCULA
    printf( "\033[s\033[0;0H");
    printf( "____________USB___________\n");
    printf( " MUX 01 -      %s < 40 MA6 \n", TST(toggled,prev,ADDR_PIN_BASE+6));
    printf( " MA5 02 > %s    %s < 39 MA7 \n", TST(toggled,prev,ADDR_PIN_BASE+5), TST(toggled,prev,ADDR_PIN_BASE+7));
    printf( " MA4 03 > %s    %s < 38 MA0 \n", TST(toggled,prev,ADDR_PIN_BASE+4), TST(toggled,prev,ADDR_PIN_BASE+0));
    printf( " MA3 04 > %s    %s < 37 MA2 \n", TST(toggled,prev,ADDR_PIN_BASE+3), TST(toggled,prev,ADDR_PIN_BASE+2));
    printf( "  D5 05 > %s    %s < 36 MA1  \n", TST(toggled,prev,DATA_PIN_BASE+5), TST(toggled,prev,ADDR_PIN_BASE+1));
    printf( " GND 06        %s < 35 A12  \n", TST(toggled,prev,ADDR_PIN_BASE+12));
    printf( " XT1 07 -      %s < 34 D6   \n", TST(toggled,prev,DATA_PIN_BASE+6));
    printf( "  D0 08 > %s    %s < 33 A9   \n", TST(toggled,prev,DATA_PIN_BASE+0), TST(toggled,prev,ADDR_PIN_BASE+9));
    printf( " RAS 09 -      %s < 32 A8   \n", TST(toggled,prev,ADDR_PIN_BASE+8));
    printf( " CAS 10 -      %s < 31 A10   \n", TST(toggled,prev,ADDR_PIN_BASE+10));
    printf( "  D2 11 > %s    %s < 30 A15   \n", TST(toggled,prev,DATA_PIN_BASE+2), TST(toggled,prev,ADDR_PIN_BASE+15));
    printf( "  D3 12 > %s    %s < 29 A14   \n", TST(toggled,prev,DATA_PIN_BASE+3), TST(toggled,prev,ADDR_PIN_BASE+14));
    printf( "  D4 13 > %s      - 28 WEN   \n", TST(toggled,prev,DATA_PIN_BASE+4));
    printf( "PHI0 14 <      %s < 27 RNW_IN\n", TST(toggled,prev,RNW_PIN));
    printf( " A11 15 > %s    %s < 26 NMAP  \n", TST(toggled,prev,ADDR_PIN_BASE+11), TST(toggled,prev,NMAP_PIN));
    printf( "SYNC 16 <        > 25 IO    \n");
    printf( "  D1 17 > %s        24 VSS   \n", TST(toggled,prev,DATA_PIN_BASE+1));
    printf( "  D7 18 > %s      > 23 ROMSEL\n", TST(toggled,prev,DATA_PIN_BASE+7));
    printf( " BLU 19 <      %s < 22 A13  \n", TST(toggled,prev,ADDR_PIN_BASE+13));
    printf( " GRN 20 <        > 21 RED  \n");
    printf( "__________________________\n");
    printf( "\033[u");
#endif
}

void tst_mon_test(const char *args, size_t len){
    if(parse_end(args,len)){  //No arguments
        toggled = 0;
        testing = true;
    }else{
        testing = false;
    }
}
