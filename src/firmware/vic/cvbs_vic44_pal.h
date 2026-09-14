/*
 * Copyright (c) 2026 Sodiumlightbaby
 * Copyright (c) 2026 dreamseal
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _CVBS_VIC44_PAL_H_
#define _CVBS_VIC44_PAL_H_ 
#include "vic/cvbs_pal.h"

#define VIC44_PAL_HSYNC        CVBS_CMD_PAL_DC_RUN( 0,20*2)
#define VIC44_PAL_FRONTPORCH   CVBS_CMD_PAL_DC_RUN(36, 7*2)
#define VIC44_PAL_FRONTPORCH_1 CVBS_CMD_PAL_DC_RUN(36, 5*2)        // First part is from last pixel of HC=70 to end of HC=0.
#define VIC44_PAL_FRONTPORCH_2 CVBS_CMD_PAL_DC_RUN(36, 2*2)        // Other two in HC=1 up to HC=1.5
#define VIC44_PAL_BREEZEWAY    CVBS_CMD_PAL_DC_RUN(36, 3*2)        // Actual breezeway this delay + burst command delay
#define VIC44_PAL_BACKPORCH    CVBS_CMD_PAL_DC_RUN(36, 4*2)


// Vertical blanking and sync.
// TODO: From original cvbs.c code. Doesn't match VIC chip timings.
#define VIC44_PAL_LONG_SYNC_L_CVBS  CVBS_CMD_PAL_DC_RUN( 0,133*2)
#define VIC44_PAL_LONG_SYNC_H_CVBS  CVBS_CMD_PAL_DC_RUN( 9,  9*2)
#define VIC44_PAL_SHORT_SYNC_L_CVBS CVBS_CMD_PAL_DC_RUN( 0,  9*2)
#define VIC44_PAL_SHORT_SYNC_H_CVBS CVBS_CMD_PAL_DC_RUN( 9,133*2)
#define VIC44_PAL_LONG_SYNC_L  CVBS_CMD_PAL_DC_RUN( 0,133*2)
#define VIC44_PAL_LONG_SYNC_L1  CVBS_CMD_PAL_DC_RUN( 0,66*2)
#define VIC44_PAL_LONG_SYNC_L2  CVBS_CMD_PAL_DC_RUN( 0,67*2)
#define VIC44_PAL_LONG_SYNC_H  CVBS_CMD_PAL_DC_RUN(36,  9*2)
#define VIC44_PAL_SHORT_SYNC_L CVBS_CMD_PAL_DC_RUN( 0,  9*2)
#define VIC44_PAL_SHORT_SYNC_H CVBS_CMD_PAL_DC_RUN(36,133*2)
#define VIC44_PAL_SHORT_SYNC_H1 CVBS_CMD_PAL_DC_RUN(36,66*2)
#define VIC44_PAL_SHORT_SYNC_H2 CVBS_CMD_PAL_DC_RUN(36,67*2)

#define VIC44_PAL_BLANKING     CVBS_CMD_PAL_DC_RUN(36,234*2)


#endif /* _CVBS_VIC44_PAL_H_ */
 
