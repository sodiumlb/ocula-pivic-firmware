/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #ifndef _POT_H_
 #define _POT_H_

 #define POT_MODE_COUNT 2
 #define POT_MODE_CMB 0
 #define POT_MODE_ATARI 1

 #include <stddef.h>

 void pot_init(void);
 void pot_task(void);
 
 void pot_print_status(void);
 
 #endif /* _POT_H_ */