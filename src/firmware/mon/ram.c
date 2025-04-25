/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "str.h"
//#include "api/api.h"
#include "sys/com.h"
//#include "sys/pix.h"
//#include "sys/ria.h"
#include "sys/mem.h"
#include <stdio.h>

#define TIMEOUT_MS 200

static enum {
    SYS_IDLE,
    SYS_BINARY,
} cmd_state;

static uint32_t rw_addr;
static uint32_t rw_len;
static uint32_t rw_crc;

// Commands that start with a hex address. Read or write memory.
void ram_mon_address(const char *args, size_t len)
{
    // addr syntax is already validated by dispatch
    rw_addr = 0;
    size_t i = 0;
    for (; i < len; i++)
    {
        char ch = args[i];
        if (char_is_hex(ch))
            rw_addr = rw_addr * 16 + char_to_int(ch);
        else
            break;
    }
    for (; i < len; i++)
        if (args[i] != ' ')
            break;
    if (rw_addr > 0x3FFFF)
    {
        printf("?invalid address\n");
        return;
    }
    if (i == len)
    {
        mbuf_len = (rw_addr | 0xF) - rw_addr + 1;
        printf("%05lX", rw_addr);
        for (size_t i = 0; i < mbuf_len; i++)
            printf(" %02X", xram[rw_addr + i]);
        printf("\n");
        return;
    }
    uint32_t data = 0x80000000;
    mbuf_len = 0;
    for (; i < len; i++)
    {
        char ch = args[i];
        if (char_is_hex(ch))
            data = data * 16 + char_to_int(ch);
        else if (ch != ' ')
        {
            printf("?invalid data character\n");
            return;
        }
        if (ch == ' ' || i == len - 1)
        {
            if (data < 0x100)
            {
                mbuf[mbuf_len++] = data;
                data = 0x80000000;
            }
            else
            {
                printf("?invalid data value\n");
                return;
            }
            for (; i + 1 < len; i++)
                if (args[i + 1] != ' ')
                    break;
        }
    }
    for (size_t i = 0; i < mbuf_len; i++)
    {
        xram[rw_addr + i] = mbuf[i];
    }
    return;
}

static void sys_com_rx_mbuf(bool timeout, const char *buf, size_t length)
{
    (void)buf;
    mbuf_len = length;
    cmd_state = SYS_IDLE;
    if (timeout)
    {
        puts("?timeout");
        return;
    }

    for (size_t i = 0; i < rw_len; i++)
        xram[rw_addr + i] = buf[i];
}

void ram_mon_binary(const char *args, size_t len)
{
    if (parse_uint32(&args, &len, &rw_addr) &&
        parse_uint32(&args, &len, &rw_len) &&
        parse_uint32(&args, &len, &rw_crc) &&
        parse_end(args, len))
    {
        if (rw_addr > 0x3FFFF)
        {
            printf("?invalid address\n");
            return;
        }
        if (!rw_len || rw_len > MBUF_SIZE ||
            rw_addr + rw_len > 0x40000)
        {
            printf("?invalid length\n");
            return;
        }
        com_read_binary(TIMEOUT_MS, sys_com_rx_mbuf, mbuf, rw_len);
        cmd_state = SYS_BINARY;
        return;
    }
    printf("?invalid argument\n");
}

void ram_task(void)
{
    // if (ria_active())
    //     return;
    switch (cmd_state)
    {
    case SYS_IDLE:
    case SYS_BINARY:
        break;
    }
}

bool ram_active(void)
{
    return cmd_state != SYS_IDLE;
}

void ram_reset(void)
{
    cmd_state = SYS_IDLE;
}
