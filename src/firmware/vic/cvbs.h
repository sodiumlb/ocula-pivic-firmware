/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #ifndef _CVBS_H_
 #define _CVBS_H_

 #include <stddef.h>

 void cvbs_init(void);
 void cvbs_task(void);
 
 void cvbs_mon_tune(const char *args, size_t len);
 void cvbs_mon_colour(const char *args, size_t len);
 
 #endif /* _CVBS_H_ */
 