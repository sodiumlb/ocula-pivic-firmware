/*
* Copyright (c) 2025 Sodiumlightbaby
* Copyright (c) 2025 dreamseal
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
#include "vic/mem.h"
#include "sys/mem.h"
#include "mem.pio.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include <string.h>
#include <stdio.h>


uint8_t xread_dma_addr_chan;
uint8_t xread_dma_data_chan;
uint8_t xread_dma_mask_chan;
uint xread_mask_pio_offset;
void xread_pio_init(void){
    pio_set_gpio_base (XREAD_PIO, XREAD_PIN_OFFS);
    for(uint32_t i = 0; i < DATA_PIN_COUNT; i++){
        gpio_init(DATA_PIN_BASE+i);
        gpio_set_input_enabled(DATA_PIN_BASE+i, true);
        gpio_set_pulls(DATA_PIN_BASE+i, false, true);              //E9 work-around
        pio_gpio_init(XREAD_PIO, DATA_PIN_BASE+i);
        gpio_set_drive_strength(DATA_PIN_BASE+i, GPIO_DRIVE_STRENGTH_2MA);
    }
    for(uint32_t i = 0; i < ADDR_PIN_COUNT; i++){
        gpio_init(ADDR_PIN_BASE+i);
        gpio_set_pulls(ADDR_PIN_BASE+i, false, false);   //E9 work-around
        pio_gpio_init(XREAD_PIO, ADDR_PIN_BASE+i);
        gpio_set_input_enabled(ADDR_PIN_BASE+i, true);
    }
    pio_sm_set_consecutive_pindirs(XREAD_PIO, XREAD_SM, DATA_PIN_BASE, DATA_PIN_COUNT, false);
    pio_sm_set_consecutive_pindirs(XREAD_PIO, XREAD_SM, ADDR_PIN_BASE, ADDR_PIN_COUNT, false);
    gpio_init(RNW_PIN);
    gpio_set_input_enabled(RNW_PIN, true);
    gpio_set_pulls(RNW_PIN, false, false);              //E9 work-around

    uint offset = pio_add_program(XREAD_PIO, &xread_program);
    pio_sm_config config = xread_program_get_default_config(offset);
    //Pin counts and autopush/autopull set in program
    sm_config_set_in_pin_base(&config, ADDR_PIN_BASE);  
    sm_config_set_out_pin_base(&config, DATA_PIN_BASE);
    pio_sm_init(XREAD_PIO, XREAD_SM, offset, &config);
    pio_sm_put_blocking(XREAD_PIO, XREAD_SM, ((uintptr_t)xram >> 14));
    pio_sm_exec_wait_blocking(XREAD_PIO, XREAD_SM, pio_encode_pull(false, true));
    //pio_sm_exec_wait_blocking(XREAD_PIO, XREAD_SM, pio_encode_mov(pio_x, pio_osr));
    pio_sm_exec_wait_blocking(XREAD_PIO, XREAD_SM, pio_encode_out(pio_x, 32));
    //Autopull/autopush enabled. Clear the FIFOs before use
    pio_sm_clear_fifos(XREAD_PIO, XREAD_SM);

    offset = pio_add_program(XREAD_MASK_PIO, &mask_address_program);
    xread_mask_pio_offset = offset;
    pio_sm_config config2 = mask_address_program_get_default_config(offset);
    pio_sm_init(XREAD_MASK_PIO, XREAD_MASK_SM, offset, &config2);
    pio_sm_put_blocking(XREAD_MASK_PIO, XREAD_MASK_SM, ((uintptr_t)xram >> 8) | 0x10);
    pio_sm_exec_wait_blocking(XREAD_MASK_PIO, XREAD_MASK_SM, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(XREAD_MASK_PIO, XREAD_MASK_SM, pio_encode_out(pio_x, 32));
    

    // Set up two DMA channels for fetching address then data
    int addr_chan = dma_claim_unused_channel(true);
    int data_chan = dma_claim_unused_channel(true);
    int mask_chan = dma_claim_unused_channel(true);
    xread_dma_addr_chan = addr_chan;
    xread_dma_data_chan = data_chan;
    xread_dma_mask_chan = mask_chan;

    // DMA move address from XREAD SM to masking SM
    dma_channel_config mask_dma = dma_channel_get_default_config(mask_chan);
    channel_config_set_high_priority(&mask_dma, true);
    channel_config_set_read_increment(&mask_dma, false);
    channel_config_set_write_increment(&mask_dma, false);
    channel_config_set_dreq(&mask_dma, pio_get_dreq(XREAD_PIO, XREAD_SM, false));
    dma_channel_configure(
        mask_chan,
        &mask_dma,
        &XREAD_MASK_PIO->txf[XREAD_MASK_SM],  // dst
        &XREAD_PIO->rxf[XREAD_SM],            // src
        -1,                                   // endless transfers
        true);

    // DMA move the requested memory data to PIO for output
    dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    channel_config_set_high_priority(&data_dma, true);
    //channel_config_set_dreq(&data_dma, pio_get_dreq(XREAD_PIO, XREAD_SM, true));
    channel_config_set_read_increment(&data_dma, false);
    channel_config_set_write_increment(&data_dma, false);
    channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_8);
    channel_config_set_chain_to(&data_dma, addr_chan);
    dma_channel_configure(
        data_chan,
        &data_dma,
        &XREAD_PIO->txf[XREAD_SM],       // dst
        xram,                            // src
        1,
        false);

    // DMA move address from PIO into the data DMA config
    dma_channel_config addr_dma = dma_channel_get_default_config(addr_chan);
    channel_config_set_high_priority(&addr_dma, true);
    channel_config_set_dreq(&addr_dma, pio_get_dreq(XREAD_MASK_PIO, XREAD_MASK_SM, false));
    channel_config_set_read_increment(&addr_dma, false);
    channel_config_set_write_increment(&addr_dma, false);
    channel_config_set_chain_to(&addr_dma, data_chan);
    dma_channel_configure(
        addr_chan,
        &addr_dma,
        &dma_channel_hw_addr(data_chan)->read_addr,      // dst
        &XREAD_MASK_PIO->rxf[XREAD_MASK_SM],             // src
        1,
        true);
    
    //pio_sm_set_enabled(XREAD_PIO, XREAD_SM, true);
}

uint8_t xwrite_dma_addr_chan;
uint8_t xwrite_dma_data_chan;
uint8_t xwrite_dma_mask_chan;
void xwrite_pio_init(void){
    pio_set_gpio_base (XWRITE_PIO, XWRITE_PIN_OFFS);
    uint offset = pio_add_program(XWRITE_PIO, &xwrite_program);
    pio_sm_config config = xwrite_program_get_default_config(offset);
    sm_config_set_in_pin_base(&config, DATA_PIN_BASE); 
    sm_config_set_jmp_pin(&config, ADDR_PIN_BASE+13);   //BLK4 detection 
    pio_sm_init(XWRITE_PIO, XWRITE_SM, offset, &config);
    pio_sm_put_blocking(XWRITE_PIO, XWRITE_SM, (uintptr_t)xram >> 14);
    pio_sm_exec_wait_blocking(XWRITE_PIO, XWRITE_SM, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(XWRITE_PIO, XWRITE_SM, pio_encode_mov(pio_x, pio_osr));
    //pio_sm_set_enabled(XWRITE_PIO, XWRITE_SM, true);

    //TODO Make both mask programs use the same instruction memory
    offset = pio_add_program(XWRITE_MASK_PIO, &mask_address_program);
    pio_sm_config config2 = mask_address_program_get_default_config(offset);
    pio_sm_init(XWRITE_MASK_PIO, XWRITE_MASK_SM, offset, &config2);
    pio_sm_put_blocking(XWRITE_MASK_PIO, XWRITE_MASK_SM, ((uintptr_t)xram >> 8) | 0x10);
    pio_sm_exec_wait_blocking(XWRITE_MASK_PIO, XWRITE_MASK_SM, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(XWRITE_MASK_PIO, XWRITE_MASK_SM, pio_encode_out(pio_x, 32));

    // Set up two DMA channels for fetching address then data
    int addr_chan = dma_claim_unused_channel(true);
    int data_chan = dma_claim_unused_channel(true);
    int mask_chan = dma_claim_unused_channel(true);
    xwrite_dma_addr_chan = addr_chan;
    xwrite_dma_data_chan = data_chan;
    xwrite_dma_mask_chan = mask_chan;

    // DMA move address and data from XWRITE SM to masking SM
    dma_channel_config mask_dma = dma_channel_get_default_config(mask_chan);
    channel_config_set_high_priority(&mask_dma, true);
    channel_config_set_read_increment(&mask_dma, false);
    channel_config_set_write_increment(&mask_dma, false);
    channel_config_set_dreq(&mask_dma, pio_get_dreq(XWRITE_PIO, XWRITE_SM, false));
    dma_channel_configure(
        mask_chan,
        &mask_dma,
        &XWRITE_MASK_PIO->txf[XWRITE_MASK_SM],  // dst
        &XWRITE_PIO->rxf[XWRITE_SM],            // src
        -1,                                     // endless transfers
        true);

    // DMA move the requested memory data to PIO for output
    dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    channel_config_set_high_priority(&data_dma, true);
    channel_config_set_dreq(&data_dma, pio_get_dreq(XWRITE_MASK_PIO, XWRITE_MASK_SM, false));
    channel_config_set_read_increment(&data_dma, false);
    channel_config_set_write_increment(&data_dma, false);
    channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_8);
    channel_config_set_chain_to(&data_dma, addr_chan);
    dma_channel_configure(
        data_chan,
        &data_dma,
        xram,                                        // dst - dynamically updated by addr_dma
        &XWRITE_MASK_PIO->rxf[XWRITE_MASK_SM],       // src
        1,
        false);

    // DMA move address from PIO into the data DMA config
    dma_channel_config addr_dma = dma_channel_get_default_config(addr_chan);
    channel_config_set_high_priority(&addr_dma, true);
    channel_config_set_dreq(&addr_dma, pio_get_dreq(XWRITE_MASK_PIO, XWRITE_MASK_SM, false));
    channel_config_set_read_increment(&addr_dma, false);
    channel_config_set_write_increment(&addr_dma, false);
    channel_config_set_chain_to(&addr_dma, data_chan);
    dma_channel_configure(
        addr_chan,
        &addr_dma,
        &dma_channel_hw_addr(data_chan)->write_addr,  // dst
        &XWRITE_MASK_PIO->rxf[XWRITE_MASK_SM],        // src
        1,
        true);  
}

#define TRACE_BUF &xram[0x10000]
int trace_dma_chan;
void trace_pio_init(void){
    pio_set_gpio_base (TRACE_PIO, TRACE_PIN_OFFS);

    uint offset = pio_add_program(TRACE_PIO, &trace_program);
    pio_sm_config config = trace_program_get_default_config(offset);
    //Pin counts and autopush/autopull set in program
    sm_config_set_in_pin_base(&config, DATA_PIN_BASE);  
    pio_sm_init(TRACE_PIO, TRACE_SM, offset, &config);
    //pio_sm_set_enabled(TRACE_PIO, TRACE_SM, true);

    // Set up two DMA channels for fetching address then data
    int trace_chan = dma_claim_unused_channel(true);
    trace_dma_chan = trace_chan;

    // DMA move the requested memory data to PIO for output
    dma_channel_config trace_dma = dma_channel_get_default_config(trace_chan);
    channel_config_set_high_priority(&trace_dma, true);
    channel_config_set_dreq(&trace_dma, pio_get_dreq(TRACE_PIO, TRACE_SM, false));
    channel_config_set_read_increment(&trace_dma, false);
    channel_config_set_write_increment(&trace_dma, true);
    channel_config_set_ring(&trace_dma, true, 12);
    dma_channel_configure(
        trace_chan,
        &trace_dma,
        TRACE_BUF,                        // dst
        &TRACE_PIO->rxf[TRACE_SM],        // src
        0xf0001000,                       // continuous
        true);    
}

void xdir_pio_init(void){
    pio_set_gpio_base (XDIR_PIO, XDIR_PIN_OFFS);
    uint offset = pio_add_program(XDIR_PIO, &xdir_program);
    pio_sm_config config = xdir_program_get_default_config(offset);
    sm_config_set_out_pin_base(&config, DATA_PIN_BASE);
    sm_config_set_out_pin_count(&config, 8);                        //Only output on lower 8 bit of data bus
    sm_config_set_in_pin_base(&config, ADDR_PIN_BASE+8);
    sm_config_set_jmp_pin(&config, RNW_PIN);
    pio_sm_init(XDIR_PIO, XDIR_SM, offset, &config);
    pio_sm_put_blocking(XDIR_PIO, XDIR_SM, 0b1010000);  //Matching value RnW='1', A[13:8]=0b010000
    pio_sm_exec_wait_blocking(XDIR_PIO, XDIR_SM, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(XDIR_PIO, XDIR_SM, pio_encode_mov(pio_x, pio_osr));
    //pio_sm_set_enabled(XDIR_PIO, XDIR_SM, true);   
}

void xuncon_pio_init(void){
    pio_set_gpio_base (XUNCON_PIO, XUNCON_PIN_OFFS);
    uint offset = pio_add_program(XUNCON_PIO, &xuncon_program);
    pio_sm_config config = xuncon_program_get_default_config(offset);
    sm_config_set_in_pin_base(&config, DATA_PIN_BASE);
    pio_sm_init(XUNCON_PIO, XUNCON_SM, offset, &config);
}

void mem_init(void){
    //Only using input address bus. Put DIR to input
    gpio_set_function(DIR_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(DIR_PIN, true);
    gpio_put(DIR_PIN, false);

    xread_pio_init();
    xdir_pio_init();
    xwrite_pio_init();
    xuncon_pio_init();
    //trace_pio_init();    
    //Synchronized start of irq dependent PIO programs
    pio_set_sm_mask_enabled(XREAD_MASK_PIO, (1u << XREAD_MASK_SM) | (1u << XWRITE_MASK_SM), true);
    pio_set_sm_mask_enabled(XREAD_PIO, (1u << XREAD_SM) | (1u << XDIR_SM) | (1u << XWRITE_SM) | (1u << XUNCON_SM) /*| (1u << TRACE_SM)*/, true);
}

void mem_task(void){
}

void mem_alias_enable(bool enable){
    if(enable){
        XREAD_MASK_PIO->instr_mem[xread_mask_pio_offset + mask_address_offset_enable] = (uint16_t)(pio_encode_jmp_x_ne_y(xread_mask_pio_offset + mask_address_offset_no_mask));
    }else{
        XREAD_MASK_PIO->instr_mem[xread_mask_pio_offset + mask_address_offset_enable] = (uint16_t)(pio_encode_jmp(xread_mask_pio_offset + mask_address_offset_no_mask));
    }
}