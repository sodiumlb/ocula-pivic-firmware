/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "sys/clk.h"
#include "sys/cfg.h"
#include "vic/vic.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include <stdbool.h>
#include <stdio.h>

void clk_init(void){
#ifdef PIVIC
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
            set_sys_clock_khz(315000, true);
            clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, 315000000, 315000000/2);
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
        default:
            set_sys_clock_khz(319200, true);
            clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, 319200000, 319200000/2);
            break;
    }
#endif
#ifdef OCULA
    set_sys_clock_khz(276000, true);
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, 276000000, 276000000/2);
#endif
    
}
void clk_print_status(void){
    printf("CLK sys_clk %ld\n",clock_get_hz(clk_sys));
}
 