/*
* Copyright (c) 2025 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "main.h"
//#include "sys/ria.h"
#include "oric/ula.h"
#include "oric/oric_font.h"
#include "sys/mem.h"
#include "ula.pio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include <string.h>
#include <stdio.h>

#define ULA_MASK_ATTRIB_MARK  0x60
#define ULA_MASK_ATTRIB_INDEX 0x18
#define ULA_MASK_ATTRIB_VALUE 0x07
#define ULA_MASK_CHAR         0x7F
#define ULA_INVERT            0x80
//Styles
#define ULA_FLASH  0x04
#define ULA_DOUBLE 0x02
#define ULA_ALTCHR 0x01
//Modes
#define ULA_HIRES  0x04
#define ULA_50HZ   0x02
//Attribute IDs
#define ULA_ATTRIB_INK   (0x00<<3)
#define ULA_ATTRIB_STYLE (0x01<<3)
#define ULA_ATTRIB_PAPER (0x02<<3)
#define ULA_ATTRIB_MODE  (0x03<<3)

union {
    struct { 
        uint8_t ink;
        uint8_t style;
        uint8_t paper;
        uint8_t mode;
    };
    uint8_t attrib[4];
} ula;

const uint32_t pixel2mask[64] = {
    0x00000000, 0x00000002, 0x00000020, 0x00000022, 0x00000200, 0x00000202, 0x00000220, 0x00000222,
    0x00002000, 0x00002002, 0x00002020, 0x00002022, 0x00002200, 0x00002202, 0x00002220, 0x00002222,
    0x00020000, 0x00020002, 0x00020020, 0x00020022, 0x00020200, 0x00020202, 0x00020220, 0x00020222,
    0x00022000, 0x00022002, 0x00022020, 0x00022022, 0x00022200, 0x00022202, 0x00022220, 0x00022222,
    0x00200000, 0x00200002, 0x00200020, 0x00200022, 0x00200200, 0x00200202, 0x00200220, 0x00200222,
    0x00202000, 0x00202002, 0x00202020, 0x00202022, 0x00202200, 0x00202202, 0x00202220, 0x00202222,
    0x00220000, 0x00220002, 0x00220020, 0x00220022, 0x00220200, 0x00220202, 0x00220220, 0x00220222,
    0x00222000, 0x00222002, 0x00222020, 0x00222022, 0x00222200, 0x00222202, 0x00222220, 0x00222222,
};

uint16_t verticalCounter = 0;
uint8_t horizontalCounter = 0;

#define ADDR_LORES_STD_CHRSET 0xB400
#define ADDR_LORES_ALT_CHRSET 0xB800
#define ADDR_HIRES_STD_CHRSET 0x9800
#define ADDR_HIRES_ALT_CHRSET 0x9C00
#define ADDR_LORES_SCR  0xBB80
#define ADDR_HIRES_SCR  0xA000

// Oric pixels are shifted out MSB first, so the PIO program is also set to shift left out
// NB colour mapping is B[3] G[2] R[1] S[0]
#define RGBS_CMD(v0,v1,v2,v3,v4,v5,count) (((count-1)&0xFF)<<24 | (v0&0xF)<<20 | (v1&0xF)<<16 | (v2&0xF)<<12 | (v3&0xF)<<8 | (v4&0xF)<<4 | (v5&0xF))
#define RGBS_CMD1(v,count) RGBS_CMD(v,v,v,v,v,v,count)
#define VAL_BLANK 0x1
#define VAL_SYNC 0x0
#define VAL_BLACK 0x1
#define VAL_WHITE 0xF
#define VAL_BLUE  0x9
#define VAL_GREEN 0x5
#define VAL_RED   0x3
#define CMD_BLANK9 RGBS_CMD1(VAL_BLANK,9)
#define CMD_BLANK11 RGBS_CMD1(VAL_BLANK,11)
#define CMD_BLANK40 RGBS_CMD1(VAL_BLANK,40)
#define CMD_HSYNC RGBS_CMD1(VAL_SYNC,4)
#define CMD_VSYNC RGBS_CMD1(VAL_SYNC,64)

#define RGBS_TX  RGBS_PIO->txf[RGBS_SM] 

uint32_t inline __attribute__((always_inline)) rgbs_cmd_pixel(uint8_t ink, uint8_t paper, uint8_t data){
    uint32_t cmd = 0x00111111;                   //Default repeat=0, sync=1 for 6 values
    if(data & ULA_INVERT){
        ink = ~ink & 0x7;
        paper = ~paper & 0x7;
    }
    // Lookup table + multiply was faster than this assembly
    /* Corex-M33 assembly for building the command word
       tst tests a bit in the data word to see if ink or paper should be inserted, updates Z
       ite is Arm Thumb IF-THEN block for the two next instructions (T-then E-else)
       bfi is bit field insert that inserts the 3 bits from ink or paper depending on the test result
       So if bit==0 paper value is inserted, else ink value is inserted
       Unrolled for the 6 pixels
    */
   /*
    asm(
        "tst %[data], 0x01\n\t"
        "ite eq\n\t"
        "bfieq %[cmd], %[paper], #1, 3\n\t"
        "bfine %[cmd], %[ink], #1, 3\n\t"        
        "tst %[data], 0x02\n\t"
        "ite eq\n\t"
        "bfieq %[cmd], %[paper], #5, 3\n\t"
        "bfine %[cmd], %[ink], #5, 3\n\t"        
        "tst %[data], 0x04\n\t"
        "ite eq\n\t"
        "bfieq %[cmd], %[paper], #9, 3\n\t"
        "bfine %[cmd], %[ink], #9, 3\n\t"        
        "tst %[data], 0x08\n\t"
        "ite eq\n\t"
        "bfieq %[cmd], %[paper], #13, 3\n\t"
        "bfine %[cmd], %[ink], #13, 3\n\t"        
        "tst %[data], 0x10\n\t"
        "ite eq\n\t"
        "bfieq %[cmd], %[paper], #17, 3\n\t"
        "bfine %[cmd], %[ink], #17, 3\n\t"        
        "tst %[data], 0x20\n\t"
        "ite eq\n\t"
        "bfieq %[cmd], %[paper], #21, 3\n\t"
        "bfine %[cmd], %[ink], #21, 3\n\t"        
        : [cmd] "+r" (cmd)
        : [ink] "r"  (ink),
          [paper] "r" (paper),
          [data] "r" (data)
    );
*/

    uint32_t ink_expanded   = pixel2mask[data & 0x3F] * ink;
    uint32_t paper_expanded = pixel2mask[~data & 0x3F] * paper;
    cmd = cmd | ink_expanded | paper_expanded;
    return cmd;
}

/*
    The core1_loop timing critical emulation
    Does the ULA rendering operation
    Runs at 1MHz and feeds the RGBS output
*/

void core1_loop(void){
    static bool hscan = false;
    static bool vscan = false;
    static bool hsync = false;
    static bool vsync = false;
    static bool mode_50hz = true;      //Updated only at the end of frame
    static bool force_txt = false;      //Forced text mode at the bottom of hires

    uint8_t screen_data;
    uint8_t char_data;
    uint8_t pixel_data;     //Monochrome
    uint8_t invert_flag;

    pio_sm_put(RGBS_PIO,RGBS_SM,CMD_BLANK40); //Add some latency between PIO and this loop.

    while(1){
        //Wait for falling edge of PHI clock 
        while(!(PHI_PIO->irq & 0x1)){
            tight_loop_contents();
        }
        //Clear PIO IRQ flag 0
        PHI_PIO->irq = 0x1;


        /* Output data fetched in previous cycle */
        // Active data is output here.
        // Blanking and sync data is output in the counter section
        if(vsync || hsync){
            RGBS_TX = RGBS_CMD1(VAL_SYNC,1);
        }else if(hscan && vscan){
            //output pixeldata with invertion
            RGBS_TX = rgbs_cmd_pixel(ula.ink,ula.paper,char_data | invert_flag);
        }else{
            RGBS_TX = RGBS_CMD1(VAL_BLANK,1);
        }
        /* Lookups values for output at the beginning of next cycle */
        // ULA Phase 1
        if((ula.mode & ULA_HIRES) && !force_txt){
            screen_data = xram[ADDR_HIRES_SCR + (verticalCounter*40) + horizontalCounter];
        }else{ //LORES
            screen_data = xram[ADDR_LORES_SCR + ((verticalCounter>>3)*40) + horizontalCounter];
        }
        //Check for and update attribute registers

        if((screen_data & ULA_MASK_ATTRIB_MARK)==0x00){
            uint8_t value = screen_data & ULA_MASK_ATTRIB_VALUE;
            uint8_t index = (screen_data & ULA_MASK_ATTRIB_INDEX)>>3;
            ula.attrib[index] = value;
            char_data = 0x00;   //Show paper when on attributes
        }else{
            // ULA Phase 2
            uint16_t ch_offs = ((screen_data & ULA_MASK_CHAR)*8) + (verticalCounter & 0x7);
            if(ula.mode & ULA_HIRES){
                if(ula.style & ULA_ALTCHR){
                    char_data = xram[ADDR_HIRES_ALT_CHRSET + ch_offs];
                }else{ //STDCHAR
                    char_data = xram[ADDR_HIRES_STD_CHRSET + ch_offs];
                }
            }else{ //LORES
                if(ula.style & ULA_ALTCHR){
                    char_data = xram[ADDR_LORES_ALT_CHRSET + ch_offs];
                }else{ //STDCHAR
                    char_data = xram[ADDR_LORES_STD_CHRSET + ch_offs];
                }
            }
        }
        invert_flag = screen_data & ULA_INVERT;

        // Counter updates that will be used in the next cycle
        // This results in some numbers looking off by one,
        // but this follows the actual ULA HW operation
        switch(++horizontalCounter){
            case(1):
                hscan = true;
                break;
            case(41):   
                hscan = false;
                break;
            case(50):   
                verticalCounter++;
                hsync = true;
                break;
            case(54):
                hsync = false;
                break;
            case(64):
                horizontalCounter = 0;
                ula.ink = 0x07;
                ula.paper = 0x00;
                ula.style = 0x00;
                break;
            default:    
                break;
        }

        if(hsync){
            if(mode_50hz){
                switch(verticalCounter){
                    case(0):
                        vscan = true;
                        break;
                    case(200):
                        force_txt = true;
                        break;
                    case(224):
                        vscan = false;
                        break;
                    case(256):
                        vsync = true;
                        break;
                    case(260):
                        vsync = false;
                        break;
                    case(312):
                        verticalCounter = 0;
                        mode_50hz = true; //!!(ula_mode & ULA_50HZ);
                        force_txt = false;
                        break;
                    default:
                        break;
                }
            }else{  //60HZ
                switch(verticalCounter){
                    case(0):
                        vscan = true;
                        break;
                    case(200):
                        force_txt = true;
                        break;
                    case(224):
                        vscan = false;
                        break;
                    case(236):
                        vsync = true;
                        break;
                    case(240):
                        vsync = false;
                        break;
                    case(264):
                        verticalCounter = 0;
                        mode_50hz = true; //!!(ula_mode & ULA_50HZ);
                        force_txt = false;
                        break;
                    default:
                        break;
                }   
            }
        }
    }
}

void phi_pio_init(void){
    pio_set_gpio_base (PHI_PIO, PHI_PIN_OFFS);
    pio_gpio_init(PHI_PIO, PHI_PIN);
    gpio_set_input_enabled(PHI_PIN, true);
    gpio_set_slew_rate(PHI_PIN, GPIO_SLEW_RATE_SLOW);
    gpio_set_drive_strength(PHI_PIN, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(PHI_PIO, PHI_SM, PHI_PIN, 1, true);
    uint offset = pio_add_program(PHI_PIO, &phi_program);
    pio_sm_config config = phi_program_get_default_config(offset);
    sm_config_set_sideset_pin_base(&config, PHI_PIN);          
    pio_sm_init(PHI_PIO, PHI_SM, offset, &config);
    pio_sm_set_enabled(PHI_PIO, PHI_SM, true);   
    //printf("PHI PIO init done\n");
}

void rgbs_pio_init(void){
    pio_set_gpio_base (RGBS_PIO, RGBS_PIN_OFFS);
    for(uint32_t i = 0; i < 4; i++){
        pio_gpio_init(RGBS_PIO, RGBS_PIN_BASE+i);
        gpio_set_drive_strength(RGBS_PIN_BASE+i, GPIO_DRIVE_STRENGTH_2MA);
     }
    pio_sm_set_consecutive_pindirs(RGBS_PIO, RGBS_SM, RGBS_PIN_BASE, 4, true);
    uint offset = pio_add_program(RGBS_PIO, &rgbs_program);
    pio_sm_config config = rgbs_program_get_default_config(offset);
    sm_config_set_out_pins(&config, RGBS_PIN_BASE, 4);
    //sm_config_set_out_shift(&config, false, false, 32);  //Set in PIO program           
    pio_sm_init(RGBS_PIO, RGBS_SM, offset, &config);
    pio_sm_set_enabled(RGBS_PIO, RGBS_SM, true);   
    //printf("RGBS PIO init done\n");
}

void decode_pio_init(void){
    pio_set_gpio_base (DECODE_PIO, DECODE_PIN_OFFS);
    gpio_init(NMAP_PIN);
    gpio_set_input_enabled(NMAP_PIN, true);
    gpio_set_pulls(NMAP_PIN, true, false);             //E9 work-around
    //Invert input polarity of nMAP to make it active high (MAP)
    gpio_set_inover(NMAP_PIN, GPIO_OVERRIDE_INVERT);
    uint offset = pio_add_program(DECODE_PIO, &decode_program);
    pio_sm_config config = decode_program_get_default_config(offset);
    sm_config_set_in_pin_base(&config, ADDR_PIN_BASE + 8);
    sm_config_set_in_pin_count(&config, 8);
    sm_config_set_jmp_pin(&config, NMAP_PIN);
    pio_sm_init(DECODE_PIO, DECODE_SM, offset, &config);
    //Set sm x register to 3 for decode matching of 0x03-- and A[15:14]==0x03
    pio_sm_exec_wait_blocking(DECODE_PIO, DECODE_SM, pio_encode_set(pio_x, 0x3));
    pio_sm_set_enabled(DECODE_PIO, DECODE_SM, true);   
    //printf("DECODE PIO init done\n");
}

void nio_pio_init(void){
    pio_set_gpio_base (NIO_PIO, NIO_PIN_OFFS);
    pio_gpio_init(NIO_PIO, NIO_PIN);
    gpio_set_drive_strength(NIO_PIN, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(NIO_PIO, NIO_SM, NIO_PIN, 1, true);
    uint offset = pio_add_program(NIO_PIO, &nio_program);
    pio_sm_config config = nio_program_get_default_config(offset);
    sm_config_set_sideset_pin_base(&config, NIO_PIN);
    pio_sm_init(NIO_PIO, NIO_SM, offset, &config);
    pio_sm_set_enabled(NIO_PIO, NIO_SM, true);   
    //printf("nIO PIO init done\n");
}

void nromsel_pio_init(void){
    pio_set_gpio_base (NROMSEL_PIO, NROMSEL_PIN_OFFS);
    pio_gpio_init(NROMSEL_PIO, NROMSEL_PIN);
    gpio_set_drive_strength(NROMSEL_PIN, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(NROMSEL_PIO, NROMSEL_SM, NROMSEL_PIN, 1, true);
    uint offset = pio_add_program(NROMSEL_PIO, &nromsel_program);
    pio_sm_config config = nromsel_program_get_default_config(offset);
    sm_config_set_sideset_pin_base(&config, NROMSEL_PIN);
    pio_sm_init(NROMSEL_PIO, NROMSEL_SM, offset, &config);
    pio_sm_set_enabled(NROMSEL_PIO, NROMSEL_SM, true);   
    //printf("nROMSEL PIO init done\n");
}

void xread_pio_init(void){
    pio_set_gpio_base (XREAD_PIO, XREAD_PIN_OFFS);
    for(uint32_t i = 0; i < DATA_PIN_COUNT; i++){
        gpio_init(DATA_PIN_BASE+i);
        gpio_set_input_enabled(DATA_PIN_BASE+i, true);
        pio_gpio_init(XREAD_PIO, DATA_PIN_BASE+i);
        gpio_set_drive_strength(DATA_PIN_BASE+i, GPIO_DRIVE_STRENGTH_2MA);
    }
    for(uint32_t i = 0; i < ADDR_PIN_COUNT; i++){
        gpio_init(ADDR_PIN_BASE+i);
        gpio_set_pulls(ADDR_PIN_BASE+i, false, true);   //E9 work-around
        pio_gpio_init(XREAD_PIO, ADDR_PIN_BASE+i);
        gpio_set_input_enabled(ADDR_PIN_BASE+i, true);
    }
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
    pio_sm_put_blocking(XREAD_PIO, XREAD_SM, (uintptr_t)xram >> 16);
    pio_sm_exec_wait_blocking(XREAD_PIO, XREAD_SM, pio_encode_pull(false, true));
    //pio_sm_exec_wait_blocking(XREAD_PIO, XREAD_SM, pio_encode_mov(pio_x, pio_osr));
    pio_sm_exec_wait_blocking(XREAD_PIO, XREAD_SM, pio_encode_out(pio_x, 32));
    //Autopull/autopush enabled. Clear the FIFOs before use
    pio_sm_clear_fifos(XREAD_PIO, XREAD_SM);

    // Set up two DMA channels for fetching address then data
    int addr_chan = dma_claim_unused_channel(true);
    int data_chan = dma_claim_unused_channel(true);

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
    channel_config_set_dreq(&addr_dma, pio_get_dreq(XREAD_PIO, XREAD_SM, false));
    channel_config_set_read_increment(&addr_dma, false);
    channel_config_set_write_increment(&addr_dma, false);
    channel_config_set_chain_to(&addr_dma, data_chan);
    dma_channel_configure(
        addr_chan,
        &addr_dma,
        &dma_channel_hw_addr(data_chan)->read_addr,      // dst
        &XREAD_PIO->rxf[XREAD_SM],                       // src
        1,
        true);
    
    pio_sm_set_enabled(XREAD_PIO, XREAD_SM, true);

    //printf("XREAD PIO init done\n");
}

uint8_t xwrite_dma_addr_chan;
uint8_t xwrite_dma_data_chan;
void xwrite_pio_init(void){
    pio_set_gpio_base (XWRITE_PIO, XWRITE_PIN_OFFS);
    uint offset = pio_add_program(XWRITE_PIO, &xwrite_program);
    pio_sm_config config = xwrite_program_get_default_config(offset);
    sm_config_set_in_pin_base(&config, DATA_PIN_BASE); 
    sm_config_set_jmp_pin(&config, RNW_PIN); 
    pio_sm_init(XWRITE_PIO, XWRITE_SM, offset, &config);
    pio_sm_put_blocking(XWRITE_PIO, XWRITE_SM, (uintptr_t)xram >> 16);
    pio_sm_exec_wait_blocking(XWRITE_PIO, XWRITE_SM, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(XWRITE_PIO, XWRITE_SM, pio_encode_mov(pio_x, pio_osr));
    pio_sm_set_enabled(XWRITE_PIO, XWRITE_SM, true);

    // Set up two DMA channels for fetching address then data
    int addr_chan = dma_claim_unused_channel(true);
    int data_chan = dma_claim_unused_channel(true);
    xwrite_dma_data_chan = data_chan;

    // DMA move the requested memory data to PIO for output
    dma_channel_config data_dma = dma_channel_get_default_config(data_chan);
    channel_config_set_high_priority(&data_dma, true);
    //channel_config_set_dreq(&data_dma, pio_get_dreq(XWRITE_PIO, XWRITE_SM, false));
    channel_config_set_read_increment(&data_dma, false);
    channel_config_set_write_increment(&data_dma, false);
    channel_config_set_transfer_data_size(&data_dma, DMA_SIZE_8);
    channel_config_set_chain_to(&data_dma, addr_chan);
    dma_channel_configure(
        data_chan,
        &data_dma,
        xram,                            // dst
        &XWRITE_PIO->rxf[XWRITE_SM],       // src
        1,
        false);

    // DMA move address from PIO into the data DMA config
    dma_channel_config addr_dma = dma_channel_get_default_config(addr_chan);
    channel_config_set_high_priority(&addr_dma, true);
    channel_config_set_dreq(&addr_dma, pio_get_dreq(XWRITE_PIO, XWRITE_SM, false));
    channel_config_set_read_increment(&addr_dma, false);
    channel_config_set_write_increment(&addr_dma, false);
    channel_config_set_chain_to(&addr_dma, data_chan);
    dma_channel_configure(
        addr_chan,
        &addr_dma,
        &dma_channel_hw_addr(data_chan)->write_addr,            // dst
        &XWRITE_PIO->rxf[XWRITE_SM],                            // src
        1,
        true);
    
    //printf("XWRITE PIO init done\n");
}

void xdir_pio_init(void){
    pio_set_gpio_base (XDIR_PIO, XDIR_PIN_OFFS);
    pio_gpio_init(XDIR_PIO, DIR_PIN);
    gpio_set_drive_strength(DIR_PIN, GPIO_DRIVE_STRENGTH_2MA);
    pio_sm_set_consecutive_pindirs(XDIR_PIO, XDIR_SM, DIR_PIN, 1, true);
    uint offset = pio_add_program(XDIR_PIO, &xdir_program);
    pio_sm_config config = xdir_program_get_default_config(offset);
    sm_config_set_set_pin_base(&config, DIR_PIN);
    sm_config_set_out_pin_base(&config, DATA_PIN_BASE);
    sm_config_set_out_pin_count(&config, DATA_PIN_COUNT);
    sm_config_set_jmp_pin(&config, PHI_PIN);
    sm_config_set_in_pin_base(&config, RNW_PIN);
    pio_sm_init(XDIR_PIO, XDIR_SM, offset, &config);
    pio_sm_set_enabled(XDIR_PIO, XDIR_SM, true);   
    //printf("XDIR PIO init done\n");   
}

void ula_init(void){
    printf("ula_init()\n");
    memcpy((void*)(&xram[ADDR_LORES_STD_CHRSET+(0x20*8)]), (void*)oric_font, sizeof(oric_font));
    memset((void*)&xram[ADDR_LORES_SCR], 0x20, 40*28);
    sprintf((char*)(&xram[ADDR_LORES_SCR]), "Oric OCULA test " __DATE__);
    for(uint8_t i=0; i< 40; i++){
        xram[ADDR_LORES_SCR + 40*2 + i] =  0x10 | (i & 0x7);
    }
    for(uint8_t i=0; i<(0x7F-0x20); i++){
        xram[ADDR_LORES_SCR + 40*4 + i] =  0x20 + i;    
    }
    for(uint8_t i=0; i<(0x7F-0x20); i++){
        xram[ADDR_LORES_SCR + 40*7 + i] =  0x80 | (0x20 + i);    
    }
    printf("PIO inits\n");
    phi_pio_init();
    rgbs_pio_init();
    decode_pio_init();
    nio_pio_init();
    nromsel_pio_init();
    xread_pio_init();
    xwrite_pio_init();
    xdir_pio_init();

    //Set unused outputs to defaults
    gpio_set_function(CAS_PIN, GPIO_FUNC_SIO);
    gpio_set_function(RAS_PIN, GPIO_FUNC_SIO);
    gpio_set_function(MUX_PIN, GPIO_FUNC_SIO);
    gpio_set_function(WREN_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(CAS_PIN, true);
    gpio_set_dir(RAS_PIN, true);
    gpio_set_dir(MUX_PIN, true);
    gpio_set_dir(WREN_PIN, true);
    gpio_put(CAS_PIN, true);
    gpio_put(RAS_PIN, true);
    gpio_put(MUX_PIN, true);
    gpio_put(WREN_PIN, false);
    
    
    multicore_launch_core1(core1_loop);
}

void ula_task(void){
    //if(pio2->irq > 1)
    uint64_t pins = gpio_get_all64();
    uint16_t addr = (pins >> ADDR_PIN_BASE) & 0xFFFF;
    uint8_t data = (pins >> DATA_PIN_BASE) & 0xFF;
    uint8_t map = (pins >> NMAP_PIN) & 0x1;
    uint8_t rnw = (pins >> RNW_PIN) & 0x1;
    uint8_t pirq0 = pio0->irq & 0xFF;
    uint8_t pirq1 = pio1->irq & 0xFF;
    uint8_t pirq2 = pio2->irq & 0xFF;
    uint32_t xwrite_addr = dma_hw->ch[xwrite_dma_data_chan].write_addr;
    uint8_t xw_fifo = pio_sm_get_rx_fifo_level(XWRITE_PIO, XWRITE_SM);
    //printf("%llx %08x %08x %08x\n", gpio_get_all64(), pio0->irq, pio1->irq, pio2->irq);
    sprintf((char*)(&xram[ADDR_LORES_SCR]+20*40), "A:%04x D:%02x %c PIO:%02x%02x%02x WX:%08x %d\n", 
        addr, data, (rnw ? 'R' : 'W'), pirq0, pirq1, pirq2, xwrite_addr, xw_fifo);
}