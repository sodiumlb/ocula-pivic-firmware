/*
 * Copyright (c) 2025 Sodiumlightbaby
 * Copyright (c) 2025 dreamseal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _VIC_MEM_H_
#define _VIC_MEM_H_

#include <stddef.h>

void mem_set_wdelay(uint8_t delay);

void mem_init(void);
void mem_task(void);

#endif /* _VIC_MEM_H_ */