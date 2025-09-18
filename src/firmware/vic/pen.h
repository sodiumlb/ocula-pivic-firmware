/*
* Copyright (c) 2025 Sodiumlightbaby
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef _PEN_H_
#define _PEN_H_

#include <stddef.h>

extern volatile uint16_t *pen_xy;
extern volatile uint32_t *pen_dma_trans_reg;

void pen_init(void);

void pen_print_status(void);

#endif /* _PEN_H_ */