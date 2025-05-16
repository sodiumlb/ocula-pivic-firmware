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

void clk_init(void){
#ifdef PIVIC
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    switch(cfg_get_mode()){
        case(VIC_MODE_NTSC):
        case(VIC_MODE_TEST_NTSC):
            set_sys_clock_khz(315000, true);
            clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, 315000000, 135000000);
            break;
        case(VIC_MODE_PAL):
        case(VIC_MODE_TEST_PAL):
        default:
            set_sys_clock_khz(319200, true);
            clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, 319200000, 135000000);
            break;
    }
#endif
#ifdef OCULA
    set_sys_clock_khz(138000, true);
#endif
    
}

 