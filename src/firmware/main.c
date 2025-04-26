/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "modes/mode1.h"
#include "modes/mode2.h"
#include "modes/mode3.h"
#include "modes/mode4.h"
#include "mon/mon.h"
#include "mon/ram.h"
#include "sys/com.h"
#include "sys/cfg.h"
#include "sys/cpu.h"
#include "sys/dvi.h"
#include "sys/lfs.h"
#include "sys/sys.h"
#include "sys/vga.h"
#include "term/font.h"
#include "term/term.h"
#include "usb/cdc.h"
#include "usb/serno.h"
#include "pico/stdlib.h"
#include "tusb.h"

#ifdef OCULA
#include "oric/rst.h"
#include "oric/ula.h"
#endif

#ifdef PIVIC
#include "vic/aud.h"
#include "vic/vic.h"
#include "vic/cvbs.h"
#endif

static void init(void)
{
#ifdef OCULA
    rst_init();
#endif
    cpu_init();
    com_init();

    // Print startup message
    sys_init();

    // Load config before we continue
    lfs_init();
    cfg_init();
    //vga_init();
    //font_init();
    //term_init();
    serno_init(); // before tusb
    tusb_init();
    cdc_init();
    dvi_init();
#ifdef PIVIC
    vic_init();
    //cvbs_init();
    aud_init();
#endif
#ifdef OCULA
    ula_init();
#endif
}

static void task(void)
{
    cpu_task();
    dvi_task();
#ifdef PIVIC
    vic_task();
    //cvbs_task();
    aud_task();
#endif
#ifdef OCULA
    ula_task();
#endif 
    //vga_task();
    //term_task();
    tud_task();
    cdc_task();

    com_task();
    mon_task();
    //ram_task();
}

void main_flush(void)
{
}

void main_reclock(void)
{
}

bool main_prog(uint16_t *xregs)
{
    switch (xregs[1])
    {
    case 0:
        return term_prog(xregs);
    case 1:
        return mode1_prog(xregs);
    case 2:
        return mode2_prog(xregs);
    case 3:
        return mode3_prog(xregs);
    case 4:
        return mode4_prog(xregs);
    default:
        return false;
    }
}

void main()
{
    init();
    while (1)
        task();
}
