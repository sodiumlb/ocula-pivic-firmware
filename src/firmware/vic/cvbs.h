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
 void cvbs_mon_load(const char *args, size_t len);
 void cvbs_mon_save(const char *args, size_t len);
 void cvbs_print_status(void);

 bool cvbs_load_palette(uint8_t mode, const char *path);
 bool cvbs_save_palette(const char *path);
 
 extern uint32_t cvbs_burst_cmd_odd;
 extern uint32_t cvbs_burst_cmd_even;
 extern uint32_t cvbs_palette[8][16];
 typedef struct cvbs_colour_struct {
    uint8_t delay;
    uint8_t luma;
    int8_t chroma;
    uint8_t _reserved;
 } cvbs_colour_t;

 typedef struct cvbs_palette_struct {
   uint32_t version;
   cvbs_colour_t colours[16];
   cvbs_colour_t burst;
 } cvbs_palette_t;

 #endif /* _CVBS_H_ */
 