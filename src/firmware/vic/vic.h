/*
 * Copyright (c) 2025 dreamseal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #ifndef _VIC_H_
 #define _VIC_H_

 #define VIC_MODE_COUNT 4
 #define VIC_MODE_NTSC 0
 #define VIC_MODE_PAL 1
 #define VIC_MODE_TEST_NTSC 2
 #define VIC_MODE_TEST_PAL 3

 void vic_init(void);
 void vic_task(void);

 void vic_print_status(void);
 
 #endif /* _VIC_H_ */