/*
 * Copyright (c) 2025 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #ifndef _CLK_H_
 #define _CLK_H_
  
 void clk_init(void);
 void clk_print_status(void);
 void clk_set_qmi_clkdiv(uint8_t div);
 
 #endif /* _CLK_H_ */
 