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

#define TST_PIN(bitmap,n) (bitmap & (1ull << n) ? 1 : 0) 

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
            potx = POTX_PIN_1_1;
            poty = POTY_PIN_1_1;
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
    printf( " RNW 04 > %d    %d < 37 OPT \n", TST_PIN(toggled,RNW_PIN), TST_PIN(toggled,PEN_PIN));
    printf( " D11 05 -        - 36 PHI2\n"
            " D10 06 -        > 35 PHI1\n");
    printf( "  D9 07 -      %d < 34 A13 \n", TST_PIN(toggled,ADDR_PIN_BASE+13));
    printf( "  D8 08 -      %d < 33 A12 \n", TST_PIN(toggled,ADDR_PIN_BASE+12));
    printf( "  D7 09 > %d    %d < 32 A11 \n", TST_PIN(toggled,DATA_PIN_BASE+7), TST_PIN(toggled,ADDR_PIN_BASE+11));
    printf( "  D6 10 > %d    %d < 31 A10 \n", TST_PIN(toggled,DATA_PIN_BASE+6), TST_PIN(toggled,ADDR_PIN_BASE+10));
    printf( "  D5 11 > %d    %d < 30 A9  \n", TST_PIN(toggled,DATA_PIN_BASE+5), TST_PIN(toggled,ADDR_PIN_BASE+9));
    printf( "  D4 12 > %d    %d < 29 A8  \n", TST_PIN(toggled,DATA_PIN_BASE+4), TST_PIN(toggled,ADDR_PIN_BASE+8));
    printf( "  D3 13 > %d    %d < 28 A7  \n", TST_PIN(toggled,DATA_PIN_BASE+3), TST_PIN(toggled,ADDR_PIN_BASE+7));
    printf( "  D2 14 > %d    %d < 27 A6  \n", TST_PIN(toggled,DATA_PIN_BASE+2), TST_PIN(toggled,ADDR_PIN_BASE+6));
    printf( "  D1 15 > %d    %d < 26 A5  \n", TST_PIN(toggled,DATA_PIN_BASE+1), TST_PIN(toggled,ADDR_PIN_BASE+5));
    printf( "  D0 16 > %d    %d < 25 A4  \n", TST_PIN(toggled,DATA_PIN_BASE+0), TST_PIN(toggled,ADDR_PIN_BASE+4));
    printf( "POTX 17 > %d    %d < 24 A3  \n", TST_PIN(toggled,potx), TST_PIN(toggled,ADDR_PIN_BASE+3));
    printf( "POTY 18 > %d    %d < 23 A2  \n", TST_PIN(toggled,poty), TST_PIN(toggled,ADDR_PIN_BASE+2));
    printf( " AUD 19 <      %d < 22 A1  \n", TST_PIN(toggled,ADDR_PIN_BASE+1));
    printf( " VSS 20        %d < 21 A0  \n", TST_PIN(toggled,ADDR_PIN_BASE+0));
    printf( "__________________________\n");
    printf( "\033[u");
#endif
#ifdef OCULA
    printf( "\033[s\033[0;0H");
    printf( "____________USB___________\n");
    printf( " MUX 01 -      %d < 40 MA6 \n", TST_PIN(toggled,ADDR_PIN_BASE+6));
    printf( " MA5 02 > %d    %d < 39 MA7 \n", TST_PIN(toggled,ADDR_PIN_BASE+5), TST_PIN(toggled,ADDR_PIN_BASE+7));
    printf( " MA4 03 > %d    %d < 38 MA0 \n", TST_PIN(toggled,ADDR_PIN_BASE+4), TST_PIN(toggled,ADDR_PIN_BASE+0));
    printf( " MA3 04 > %d    %d < 37 MA2 \n", TST_PIN(toggled,ADDR_PIN_BASE+3), TST_PIN(toggled,ADDR_PIN_BASE+2));
    printf( "  D5 05 > %d    %d < 36 MA1  \n", TST_PIN(toggled,DATA_PIN_BASE+5), TST_PIN(toggled,ADDR_PIN_BASE+1));
    printf( " GND 06        %d < 35 A12  \n", TST_PIN(toggled,ADDR_PIN_BASE+12));
    printf( " XT1 07 -      %d < 34 D6   \n", TST_PIN(toggled,DATA_PIN_BASE+6));
    printf( "  D0 08 > %d    %d < 33 A9   \n", TST_PIN(toggled,DATA_PIN_BASE+0), TST_PIN(toggled,ADDR_PIN_BASE+9));
    printf( " RAS 09 -      %d < 32 A8   \n", TST_PIN(toggled,ADDR_PIN_BASE+8));
    printf( " CAS 10 -      %d < 31 A10   \n", TST_PIN(toggled,ADDR_PIN_BASE+10));
    printf( "  D2 11 > %d    %d < 30 A15   \n", TST_PIN(toggled,DATA_PIN_BASE+2), TST_PIN(toggled,ADDR_PIN_BASE+15));
    printf( "  D3 12 > %d    %d < 29 A14   \n", TST_PIN(toggled,DATA_PIN_BASE+3), TST_PIN(toggled,ADDR_PIN_BASE+14));
    printf( "  D4 13 > %d      - 28 WEN   \n", TST_PIN(toggled,DATA_PIN_BASE+4));
    printf( "PHI0 14 <      %d < 27 RNW_IN\n", TST_PIN(toggled,RNW_PIN));
    printf( " A11 15 > %d    %d < 26 NMAP  \n", TST_PIN(toggled,ADDR_PIN_BASE+11), TST_PIN(toggled,NMAP_PIN));
    printf( "SYNC 16 <        > 25 IO    \n");
    printf( "  D1 17 > %d        24 VSS   \n", TST_PIN(toggled,DATA_PIN_BASE+1));
    printf( "  D7 18 > %d      > 23 ROMSEL\n", TST_PIN(toggled,DATA_PIN_BASE+7));
    printf( " BLU 19 <      %d < 22 A13  \n", TST_PIN(toggled,ADDR_PIN_BASE+13));
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
