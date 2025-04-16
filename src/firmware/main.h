/*
 * Copyright (c) 2023 Rumbledethumps
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _MAIN_H_
#define _MAIN_H_

#include <stdbool.h>
#include <stdint.h>

void main_flush(void);
void main_reclock(void);
bool main_prog(uint16_t *xregs);

#define CPU_PHI2_PIN 21

#define COM_UART uart0
#define COM_UART_BAUD_RATE 115200
#define COM_UART_TX_PIN 0
#define COM_UART_RX_PIN 1

//PIVIC CVBS aka luma/chroma DAC
#ifdef PIVIC
#define CVBS_PIO pio0
#define CVBS_SM 0
#define CVBS_PIN_BANK 0
#define CVBS_PIN_BASE 3
#define CVBS_PIN_COUNT 5

#define VIC_PIO pio1
#define VIC_SM 0
#define VIC_PIN_BANK 0
#define VIC_PIN_BASE 8

#define AUDIO_PWM_PIN 2
#define AUDIO_PWM_SLICE 1
#define AUDIO_PWM_CH  PWM_CHAN_A

#define PHI_PIN 8

#define DATA_PIN_BASE 20
#define DATA_PIN_COUNT 12

#define ADDR_PIN_BASE 32
#define ADDR_PIN_COUNT 14

#define RNW_PIN 46
#define DIR_PIN 47
#endif

#ifdef OCULA
#define PIO0_PIN_OFFS 0
#define PIO1_PIN_OFFS 16
#define PIO2_PIN_OFFS 16

#define RGBS_PIN_OFFS PIO0_PIN_OFFS
#define RGBS_PIO pio0
#define RGBS_SM 0

#define NIO_PIN_OFFS PIO0_PIN_OFFS
#define NIO_PIO pio0
#define NIO_SM 1

#define NROMSEL_PIN_OFFS PIO0_PIN_OFFS
#define NROMSEL_PIO pio0
#define NROMSEL_SM 2

#define PHI_PIN_OFFS PIO1_PIN_OFFS
#define PHI_PIO pio1
#define PHI_SM 0

#define DECODE_PIN_OFFS PIO1_PIN_OFFS
#define DECODE_PIO pio1
#define DECODE_SM 1

#define XREAD_PIN_OFFS PIO2_PIN_OFFS
#define XREAD_PIO pio2
#define XREAD_SM 0

#define XWRITE_PIN_OFFS PIO2_PIN_OFFS
#define XWRITE_PIO pio2
#define XWRITE_SM 1

#define XDIR_PIN_OFFS PIO2_PIN_OFFS
#define XDIR_PIO pio2
#define XDIR_SM 2

#define RGBS_PIN_BASE 2
#define SYNC_PIN 2
#define R_PIN 3
#define G_PIN 4
#define B_PIN 5

#define NROMSEL_PIN 6
#define NIO_PIN 7

#define MUX_PIN 8
#define CAS_PIN 9
#define RAS_PIN 10
#define WREN_PIN 11

#define DIR_PIN 20

#define DATA_PIN_BASE 21
#define DATA_PIN_COUNT 8

#define PHI_PIN 29

#define ADDR_PIN_BASE 30
#define ADDR_PIN_COUNT 16

#define RNW_PIN 46
#define NMAP_PIN 47
#endif


#endif /* _MAIN_H_ */
