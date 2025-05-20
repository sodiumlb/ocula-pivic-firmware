/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "sys/sys.h"
#include "sys/dvi.h"
#ifdef PIVIC
#include "vic/aud.h"
#include "vic/vic.h"
#endif
#ifdef OCULA
#include "oric/ula.h"
#endif
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <string.h>

static void sys_print_status(void)
{
    puts(RP6502_NAME);
    if (strlen(RP6502_VERSION))
        puts("FW Version " RP6502_VERSION);
    else
        puts("FW " __DATE__ " " __TIME__);
}

void sys_mon_reboot(const char *args, size_t len)
{
    (void)(args);
    (void)(len);
    watchdog_reboot(0, 0, 0);
}

void sys_mon_reset(const char *args, size_t len)
{
    (void)(args);
    (void)(len);
    //main_run();
}

void sys_mon_status(const char *args, size_t len)
{
    (void)(args);
    (void)(len);
    sys_print_status();
    dvi_print_status();
#ifdef PIVIC
    vic_print_status();
    aud_print_status();
#endif
#ifdef OCULA
    ula_print_status();
#endif
}

void sys_init(void)
{
    // Reset terminal.
    puts("\30\33[0m\f");
    // Hello, world.
    sys_print_status();
}
