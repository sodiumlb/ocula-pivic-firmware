/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _REV_H_
#define _REV_H_

#include <stddef.h>

typedef enum { REV_UNDEF, REV_1_1, REV_1_2 } rev_t;
void rev_init(void);
rev_t rev_get(void);
void rev_print(void);

#endif /* _REV_H_ */