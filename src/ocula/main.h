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

#endif /* _MAIN_H_ */
