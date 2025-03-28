/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "tusb.h"
//#include "sys/std.h"
#include "usb/cdc.h"
#include "pico/stdio/driver.h"

static absolute_time_t break_timer;
static absolute_time_t faux_break_timer;
static bool is_breaking = false;
//static uint8_t read_buf[STD_IN_BUF_SIZE];

static void send_break_ms(uint16_t duration_ms)
{
    break_timer = make_timeout_time_ms(duration_ms);
    is_breaking = true;
    //std_set_break(true);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
    send_break_ms(duration_ms);
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding)
{
    // MacOS/Darwin isn't capable of sending a break with its CDC driver.
    // A common workaround is to drop the baud rate significantly so a
    // bit sequence of zeros will look like a break. Our implementation has
    // strict requirements of sending a full byte of zeros within 100ms
    // of changing the baud rate to 1200. e.g.
    // stty -F /dev/ttyACM1 1200 && echo -ne '\0' > /dev/ttyACM1
    if (p_line_coding->bit_rate == 1200)
        faux_break_timer = make_timeout_time_ms(100);
}

void cdc_stdio_out_chars(const char *buf, int length);
void cdc_stdio_out_flush(void);
static int cdc_stdio_in_chars(char *buf, int length);

static stdio_driver_t cdc_stdio_app = {
    .out_chars = cdc_stdio_out_chars,
    .out_flush = cdc_stdio_out_flush,
    .in_chars = cdc_stdio_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF
#endif
};

void cdc_stdio_out_chars(const char *buf, int length)
{

    if(tud_cdc_connected()){
        //tuh_cdc_write(i, buf, length);
        
        int sent = 0;
        do {
            sent += tud_cdc_write((const char *)(buf+sent),length-sent);
            if(sent < length)
                tud_task();     //TODO This is brute force. Any nicer options?
        } while(sent < length);
        
    }
}

void cdc_stdio_out_flush(void)
{
    if(tud_cdc_connected()){
        tud_cdc_write_flush();
    }
}

static int cdc_stdio_in_chars(char *buf, int length)
{
    int ret = 0;
    if(tud_cdc_available()){
        ret = tud_cdc_read(buf,length);
    }
    return ret;
}


void cdc_init(void)
{
    stdio_set_driver_enabled(&cdc_stdio_app, true);
}


void cdc_task(void)
{
    cdc_stdio_out_flush();
    /*

    if (is_breaking && absolute_time_diff_us(get_absolute_time(), break_timer) < 0)
    {
        is_breaking = false;
        std_set_break(false);
    }

    if (!tud_cdc_connected())
    {
        // flush stdout to null
        while (!std_out_empty())
            std_out_read();
    }
    else
    {
        if (!std_out_empty())
        {
            while (!std_out_empty() && tud_cdc_write_char(std_out_peek()))
                std_out_read();
            tud_cdc_write_flush();
        }
        if (tud_cdc_available())
        {
            size_t bufsize = std_in_free();
            size_t data_len = tud_cdc_read(read_buf, bufsize);
            if (absolute_time_diff_us(get_absolute_time(), faux_break_timer) > 0)
                for (size_t i = 0; i < data_len; i++)
                {
                    char ch = read_buf[i];
                    if (ch)
                        std_in_write(ch);
                    else
                    {
                        faux_break_timer = make_timeout_time_ms(0);
                        send_break_ms(10);
                    }
                }
            else
                for (size_t i = 0; i < data_len; i++)
                    std_in_write(read_buf[i]);
        }
    }
    */
}
