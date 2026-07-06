/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "main.h"
#include "sys/cfg.h"
#include "sys/edid.h"
#include "sys/rev.h"
#include <stdio.h>
#include <pico/stdlib.h>
#include "hardware/i2c.h"

#define EDID_I2C_ADDR 0x50

typedef union {
    uint8_t all[128];
    struct {
        uint8_t header[8];
        uint8_t vendor_product_info[10];
        uint8_t edid_ver;
        uint8_t edid_rev;
        uint8_t basic_display[5];
        uint8_t colour_info[10];
        uint8_t established_timings[3];
        uint8_t standar_timings[16];
        uint8_t data_block[4][18];
        uint8_t ext_block_count;
        uint8_t checksum;
    } v1;
} edid_block_t;

static edid_block_t block0;
static bool edid_enabled = false;
static bool edid_valid = false;

static enum { idle, read_init, reading, validate, done } edid_state;


//Raw I2C funcitons for async use
static void i2c_set_addr(i2c_inst_t *i2c, uint8_t addr){
    i2c->hw->enable = 0;
    i2c->hw->tar = addr;
    i2c->hw->enable = 1;
            
}
static bool i2c_push_write_single(i2c_inst_t *i2c, uint8_t data){
    if(i2c_get_write_available(i2c)){
        i2c->hw->data_cmd =
            //1u << I2C_IC_DATA_CMD_RESTART_LSB |
            1u << I2C_IC_DATA_CMD_STOP_LSB |
            data;
        return true;
    }else{
        return false;
    }
}

static uint16_t i2c_push_read_cmd(i2c_inst_t *i2c, size_t len,  uint16_t idx){
    bool first = (idx == 0);
    bool last = (idx == len-1);
    bool done = (idx >= len);
    if(i2c_get_write_available(i2c) && !done){
        i2c->hw->data_cmd =
            //first << I2C_IC_DATA_CMD_RESTART_LSB |
            last << I2C_IC_DATA_CMD_STOP_LSB |
            I2C_IC_DATA_CMD_CMD_BITS;   //Read CMD eq the mask
        idx++;
    }
    return idx;
}

static uint16_t i2c_pop_read(i2c_inst_t *i2c, uint8_t *buf, size_t len,  uint16_t idx){
    bool last = (idx == len-1);
    bool done = (idx >= len);
    if(i2c_get_read_available(i2c) && !done){
        buf[idx++] = (uint8_t)i2c->hw->data_cmd;
    }
    return idx;
}


void edid_init(void){
#ifdef PIVIC
    if(rev_get() != REV_1_3){   //Implemented from PIVIC Rev 1.3 hardware
        return;
    }
    edid_enabled = true;
    i2c_init(EDID_I2C, 100*1000);
    gpio_set_pulls(EDID_SCL_PIN, true, false);
    gpio_set_pulls(EDID_SDA_PIN, true, false);
    gpio_set_drive_strength(EDID_SCL_PIN, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_drive_strength(EDID_SDA_PIN, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_function(EDID_SCL_PIN, GPIO_FUNC_I2C);
    gpio_set_function(EDID_SDA_PIN, GPIO_FUNC_I2C);
    
    edid_state = read_init;
#endif
}

    static uint16_t rx_count = 0;
    static uint16_t rx_queue = 0;

    void edid_task(void){
//#define PIVIC
#ifdef PIVIC
    static absolute_time_t timer = 0;
    int i;
    uint8_t checksum;
    if(edid_enabled){
        switch(edid_state){
            case(read_init):
                i2c_set_addr(EDID_I2C, EDID_I2C_ADDR);
                if(i2c_push_write_single(EDID_I2C, 0)){
                    edid_state = reading;
                    rx_count = 0;
                    rx_queue = 0;
                }
                break;
            case(reading):
                rx_queue = i2c_push_read_cmd(EDID_I2C, 128, rx_queue);
                rx_count = i2c_pop_read(EDID_I2C, (uint8_t*)&block0.all, 128, rx_count);
                if(rx_count == 128){
                    edid_state = validate;
                }
                break;
            case(validate):
                for(checksum=0, i=0; i<128; i++){
                    checksum += block0.all[i];
                }
                edid_valid = (checksum == 0x00) && (block0.all[1] == 0xFF);
                timer = delayed_by_ms(get_absolute_time(), 5000);
                edid_state = done;
                break;
            case(done):
                if(absolute_time_diff_us(get_absolute_time(), timer) < 0){
                    edid_state = read_init;
                }
                break;
            default:
                edid_state = read_init;
        }
        // if(absolute_time_diff_us(get_absolute_time(), timer) < 0){
        //     uint8_t zero = 0;
        //     i2c_write_blocking(EDID_I2C, EDID_I2C_ADDR, &zero, 1, true);   //Init address counter
        //     i2c_read_blocking(EDID_I2C, EDID_I2C_ADDR, (uint8_t*)&block0.all, 128, false);
        //     timer = delayed_by_ms(timer, 2000);
        // }
    }
#endif
}

void edid_print_status(void){
#ifdef PIVIC
    // printf("EDID state %d rxf:%d txf:%d\n", edid_state, EDID_I2C->hw->rxflr, EDID_I2C->hw->txflr);
    // printf("EDID rxqueue:%d rxcount:%d\n", rx_queue, rx_count);
    if(edid_valid){
        puts("EDID valid");
        for(int i=0; i<128;i++){
            printf(" %02x", block0.all[i]);
            if(i%8 == 8-1)
                puts("");
        }
    }
#endif
}

