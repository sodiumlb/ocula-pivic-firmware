/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #ifndef _TST_H_
 #define _TST_H_
 
 #include <stdbool.h>
 #include <stddef.h>

  
 void tst_init(void);
 void tst_task(void);
 void tst_print_status(void);
 void tst_mon_test(const char *args, size_t len);

 #endif /* _TST_H_ */
 