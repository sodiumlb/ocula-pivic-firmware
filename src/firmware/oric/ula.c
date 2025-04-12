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

#define ULA_MASK_ATTRIB 0x78
#define ULA_MASK_VALUE  0x07
#define ULA_MASK_CHAR   0x7F
#define ULA_INVERT      0x80
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

uint8_t ula_ink;
uint8_t ula_style;
uint8_t ula_paper;
uint8_t ula_mode;

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
#define VAL_BLANK 0x0E
#define VAL_SYNC 0x01
#define CMD_BLANK9 RGBS_CMD1(VAL_BLANK,9)
#define CMD_BLANK11 RGBS_CMD1(VAL_BLANK,11)
#define CMD_BLANK40 RGBS_CMD1(VAL_BLANK,40)
#define CMD_HSYNC RGBS_CMD1(VAL_SYNC,4)
#define CMD_VSYNC RGBS_CMD1(VAL_SYNC,64)

void rgbs_out(uint8_t ink, uint8_t paper, uint8_t data){
    uint32_t cmd = 0;                   //Default repeat=0
    if(data & ULA_INVERT){
        ink = ~ink & 0x7;
        paper = ~paper & 0x7;
    }
    for(uint8_t i=0; i<6; i++){
        if(data & (1u << i)){
            cmd |= ((ink<<1)) << (i*4);   //Add in sync=0x0
        }else{
            cmd |= ((paper<<1)) << (i*4);
        }
    }

    pio_sm_put(RGBS_PIO, RGBS_SM, cmd);
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

    pio_sm_put(RGBS_PIO,RGBS_SM,CMD_BLANK40); //Add some latency between PIO and this loop.

    while(1){
        //Wait for falling edge of PHI clock 
        while (!pio_interrupt_get(PHI_PIO, 0)) {
            tight_loop_contents();
        }
        pio_interrupt_clear(PHI_PIO, 0);


        /* Output data fetched in previous cycle */
        // Active data is output here.
        // Blanking and sync data is output in the counter section
        if(vsync || hsync){
            pio_sm_put(RGBS_PIO, RGBS_SM, RGBS_CMD1(VAL_SYNC,1));
        }else if(hscan && vscan){
            //output pixeldata with invertion
            //rgbs_out(ula_ink, ula_paper, pixel_data);
            rgbs_out(0x7, 0x0, 0x15);
        }else{
            pio_sm_put(RGBS_PIO, RGBS_SM, RGBS_CMD1(VAL_BLANK,1));
        }
        /* Lookups values for output at the beginning of next cycle */

        // ULA Phase 1
        if((ula_mode & ULA_HIRES) && !force_txt){
            screen_data = xram[ADDR_HIRES_SCR + (verticalCounter*40) + horizontalCounter];
        }else{ //LORES
            screen_data = xram[ADDR_LORES_SCR + ((verticalCounter>>3)*40) + horizontalCounter];
        }
        //Check for and update attribute registers

        uint8_t value = screen_data & ULA_MASK_VALUE;
        switch(screen_data & ULA_MASK_ATTRIB){
            case(ULA_ATTRIB_INK):
                ula_ink = value;
                break;
            case(ULA_ATTRIB_MODE):
                ula_mode = value;
                break;
            case(ULA_ATTRIB_PAPER):
                ula_paper = value;
                break;
            case(ULA_ATTRIB_STYLE):
                ula_style = value;
                break;
            default:
                break;
        }

        // ULA Phase 2
        if(ula_mode & ULA_HIRES){
            if(ula_style & ULA_ALTCHR){
                char_data = xram[ADDR_HIRES_ALT_CHRSET + (screen_data & ULA_MASK_CHAR)];
            }else{ //STDCHAR
                char_data = xram[ADDR_HIRES_STD_CHRSET + (screen_data & ULA_MASK_CHAR)];
            }
        }else{ //LORES
            if(ula_style & ULA_ALTCHR){
                char_data = xram[ADDR_LORES_ALT_CHRSET + ((screen_data & ULA_MASK_CHAR)*8) + (verticalCounter & 0x7)];
            }else{ //STDCHAR
                char_data = xram[ADDR_LORES_STD_CHRSET + ((screen_data & ULA_MASK_CHAR)*8) + (verticalCounter & 0x7)];
            }
        }
        pixel_data = 0x15;//HACK

        // Counter updates that will be used in the next cycle
        // This results in some numbers looking off by one,
        // but this follows the actual ULA HW operation
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
                    mode_50hz = !!(ula_mode & ULA_50HZ);
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
                    mode_50hz = !!(ula_mode & ULA_50HZ);
                    force_txt = false;
                    break;
                default:
                    break;
            }   
        }

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
                ula_ink = 0x07;
                ula_paper = 0x00;
                ula_style = 0x00;
                break;
            default:    
                break;
        }
    }
}

void phi_pio_init(void){
    pio_set_gpio_base (PHI_PIO, PHI_PIN_OFFS);
    pio_gpio_init(PHI_PIO, PHI_PIN);
    pio_sm_set_consecutive_pindirs(PHI_PIO, PHI_SM, PHI_PIN, 1, true);
    uint offset = pio_add_program(PHI_PIO, &phi_program);
    pio_sm_config config = phi_program_get_default_config(offset);
    sm_config_set_sideset_pin_base(&config, PHI_PIN);          
    pio_sm_init(PHI_PIO, PHI_SM, offset, &config);
    pio_sm_set_enabled(PHI_PIO, PHI_SM, true);   
    printf("PHI PIO init done\n");
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
    printf("RGBS PIO init done\n");
}

void ula_init(void){
    printf("ula_init()\n");
    memcpy((void*)(&xram[ADDR_LORES_STD_CHRSET+0x20]), (void*)oric_font, sizeof(oric_font));
    memset((void*)&xram[ADDR_LORES_STD_CHRSET], 0x20, 40*28);
    sprintf((char*)(&xram[ADDR_LORES_SCR+1]), "Oric OCULA test");
    printf("PIO inits\n");
    phi_pio_init();
    rgbs_pio_init();

    multicore_launch_core1(core1_loop);
}

void ula_task(void){

}