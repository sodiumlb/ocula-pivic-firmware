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

// NOTE: VIC chip vs VIC 20 memory map is different. This is why we have
// the control registers appearing at $1000. The Chip Select for reading
// and writing to the VIC chip registers is when A13=A11=A10=A9=A8=0 and 
// A12=1, i.e. $10XX. Bottom 4 bits select one of the 16 registers.
//
// VIC chip addresses     VIC 20 addresses and their normal usage
//
// $0000                  $8000  Unreversed Character ROM
// $0400                  $8400  Reversed Character ROM
// $0800                  $8800  Unreversed upper/lower case ROM
// $0C00                  $8C00  Reversed upper/lower case ROM
// $1000                  $9000  VIC and VIA chips
// $1400                  $9400  Colour memory (at either $9400 or $9600)
// $1800                  $9800  Reserved for expansion (I/O #2)
// $1C00                  $9C00  Reserved for expansion (I/O #3)
// $2000                  $0000  System memory work area
// $2400                  $0400  Reserved for 1st 1K of 3K expansion
// $2800                  $0800  Reserved for 2nd 1K of 3K expansion
// $2C00                  $0C00  Reserved for 3rd 1K of 3K expansion
// $3000                  $1000  BASIC program area / Screen when using 8K+ exp
// $3400                  $1400  BASIC program area
// $3800                  $1800  BASIC program area
// $3C00                  $1C00  BASIC program area / $1E00 screen mem for unexp VIC

// VIC chip control registers, starting at $1000, as per Chip Select.
#define vic_cr0 xram[0x1000]    // ABBBBBBB A=Interlace B=Screen Origin X (4 pixels granularity)
#define vic_cr1 xram[0x1001]    // CCCCCCCC C=Screen Origin Y (2 pixel granularity)
#define vic_cr2 xram[0x1002]    // HDDDDDDD D=Number of Columns
#define vic_cr3 xram[0x1003]    // GEEEEEEF E=Number of Rows F=Double Size Chars
#define vic_cr4 xram[0x1004]    // GGGGGGGG G=Raster Line
#define vic_cr5 xram[0x1005]    // HHHHIIII H=Screen Mem Addr I=Char Mem Addr
#define vic_cr6 xram[0x1006]    // JJJJJJJJ Light pen X
#define vic_cr7 xram[0x1007]    // KKKKKKKK Light pen Y
#define vic_cr8 xram[0x1008]    // LLLLLLLL Paddle X
#define vic_cr9 xram[0x1009]    // MMMMMMMM Paddle Y
#define vic_cra xram[0x100a]    // NRRRRRRR Sound voice 1
#define vic_crb xram[0x100b]    // OSSSSSSS Sound voice 2
#define vic_crc xram[0x100c]    // PTTTTTTT Sound voice 3
#define vic_crd xram[0x100d]    // QUUUUUUU Noise voice
#define vic_cre xram[0x100e]    // WWWWVVVV W=Auxiliary colour V=Volume control
#define vic_crf xram[0x100f]    // XXXXYZZZ X=Background colour Y=Reverse Z=Border colour

// Expressions to access different parts of control registers.
#define border_colour_index      (vic_crf & 0x07)
#define background_colour_index  (vic_crf >> 4)
#define auxiliary_colour_index   (vic_cre >> 4)
#define non_reverse_mode         (vic_crf & 0x08)
#define screen_origin_x          (vic_cr0 & 0x7F)
#define screen_origin_y          (vic_cr1 << 1)
#define num_of_columns           (vic_cr2 & 0x7F)
#define num_of_rows              ((vic_cr3 & 0x7E) >> 1)
#define double_height_mode       (vic_cr3 & 0x01)
#define last_line_of_cell        (7 | (double_height_mode << 3))
#define char_size_shift          (3 + double_height_mode)
#define screen_mem_start         (((vic_cr5 & 0xF0) << 6) | ((vic_cr2 & 0x80) << 2))
#define char_mem_start           ((vic_cr5 & 0x0F) << 10)
#define colour_mem_start         (0x1400 | ((vic_cr2 & 0x80) << 2))

// Constants for the fetch state of the vic_core1_loop.
#define FETCH_OUTSIDE_MATRIX  0
#define FETCH_IN_MATRIX_Y     1
#define FETCH_IN_MATRIX_X     2
#define FETCH_MATRIX_LINE     3
#define FETCH_MATRIX_DLY_1    4
#define FETCH_MATRIX_DLY_2    5
#define FETCH_MATRIX_DLY_3    6
#define FETCH_MATRIX_DLY_4    7
#define FETCH_SCREEN_CODE     8
#define FETCH_CHAR_DATA       9

void vic_init(void);
void vic_task(void);

void vic_print_status(void);
 
#endif /* _VIC_H_ */