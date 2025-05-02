/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "str.h"
#include "sys/cfg.h"
//#include "sys/cpu.h"
#include "sys/lfs.h"

static void set_print_phi2(void)
{
    uint32_t phi2_khz = cfg_get_phi2_khz();
    printf("PHI2  : %ld kHz", phi2_khz);
    if (phi2_khz < RP6502_MIN_PHI2 || phi2_khz > RP6502_MAX_PHI2)
        printf(" (!!!)");
    printf("\n");
}

static void set_phi2(const char *args, size_t len)
{
    uint32_t val;
    if (len)
    {
        if (!parse_uint32(&args, &len, &val) ||
            !parse_end(args, len))
        {
            printf("?invalid argument\n");
            return;
        }
        if (!cfg_set_phi2_khz(val))
        {
            printf("?invalid speed\n");
            return;
        }
    }
    set_print_phi2();
}

static void set_print_boot(void)
{
    const char *rom = cfg_get_boot();
    if (!rom[0])
        rom = "(none)";
    printf("BOOT  : %s\n", rom);
}

static void set_boot(const char *args, size_t len)
{
    if (len)
    {
        char lfs_name[LFS_NAME_MAX + 1];
        if (args[0] == '-' && parse_end(++args, --len))
        {
            cfg_set_boot("");
        }
        else if (parse_rom_name(&args, &len, lfs_name) &&
                 parse_end(args, len))
        {
            struct lfs_info info;
            if (lfs_stat(&lfs_volume, lfs_name, &info) < 0)
            {
                printf("?ROM not installed\n");
                return;
            }
            cfg_set_boot(lfs_name);
        }
        else
        {
            printf("?Invalid ROM name\n");
            return;
        }
    }
    set_print_boot();
}

static void set_print_caps(void)
{
    const char *const caps_labels[] = {"normal", "inverted", "forced"};
    printf("CAPS  : %s\n", caps_labels[cfg_get_caps()]);
}

static void set_caps(const char *args, size_t len)
{
    uint32_t val;
    if (len)
    {
        if (parse_uint32(&args, &len, &val) &&
            parse_end(args, len))
        {
            cfg_set_caps(val);
        }
        else
        {
            printf("?invalid argument\n");
            return;
        }
    }
    set_print_caps();
}

static void set_print_splash()
{
    printf("SPLASH: %d\n", cfg_get_splash());
}

static void set_splash(const char *args, size_t len)
{
    uint32_t val;
    if (len)
    {
        if (!parse_uint32(&args, &len, &val) ||
            !parse_end(args, len) ||
            !cfg_set_splash(val))
        {
            printf("?invalid argument\n");
            return;
        }
    }
    set_print_splash();
}

static void set_print_dvi(void)
{
    const char *const dvi_labels[] = {"640x480 @ 60Hz", "720x480 @ 60Hz", "720x576 @ 50Hz"};
    printf("DVI   : %s\n", dvi_labels[cfg_get_dvi()]);
}

static void set_dvi(const char *args, size_t len)
{
    uint32_t val;
    if (len)
    {
        if (parse_uint32(&args, &len, &val) &&
            parse_end(args, len))
        {
            cfg_set_dvi(val);
        }
        else
        {
            printf("?invalid argument\n");
            return;
        }
    }
    set_print_dvi();
}

typedef void (*set_function)(const char *, size_t);
static struct
{
    size_t attr_len;
    const char *const attr;
    set_function func;
} const SETTERS[] = {
    {4, "caps", set_caps},
    {4, "phi2", set_phi2},
    {4, "boot", set_boot},
    {6, "splash", set_splash},
    {3, "dvi", set_dvi},
};
static const size_t SETTERS_COUNT = sizeof SETTERS / sizeof *SETTERS;

static void set_print_all(void)
{
    set_print_phi2();
    set_print_caps();
    set_print_boot();
    set_print_splash();
    set_print_dvi();
}

void set_mon_set(const char *args, size_t len)
{
    if (!len)
        return set_print_all();

    size_t i = 0;
    for (; i < len; i++)
        if (args[i] == ' ')
            break;
    size_t attr_len = i;
    for (; i < len; i++)
        if (args[i] != ' ')
            break;
    size_t args_start = i;
    for (i = 0; i < SETTERS_COUNT; i++)
    {
        if (attr_len == SETTERS[i].attr_len &&
            !strnicmp(args, SETTERS[i].attr, attr_len))
        {
            SETTERS[i].func(&args[args_start], len - args_start);
            return;
        }
    }
    printf("?Unknown attribute\n");
}
