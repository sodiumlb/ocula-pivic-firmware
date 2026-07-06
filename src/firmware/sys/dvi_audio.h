/*
 * Copyright (c) 2026 Sodiumlightbaby
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _DVI_AUDIO_H_
#define _DVI_AUDIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pico/stdlib.h>
#include "pico_hdmi/hstx_packet.h"

// #define DVI_AUDIO_FS 32000
// #define DVI_AUDIO_ACR_N 4096
#define DVI_AUDIO_FS 48000
#define DVI_AUDIO_ACR_N 6144

void dvi_audio_init(void);
void dvi_audio_task(void);

//Sample source is a fixed location that is sampled
//at the DVI audio frequency for digital output.
//Must be set before dvi_audio_init() is called
void dvi_audio_set_sample_source(volatile audio_sample_t *src_ptr);
bool dvi_audio_buf_is_full(void);
bool dvi_audio_push_sample(audio_sample_t sample);

bool dvi_audio_di_buf_is_empty(void);
bool dvi_audio_di_buf_is_full(void);
bool dvi_audio_pop_di(uint32_t *di);
void dvi_audio_cpy_di(uint32_t *di_out, hstx_data_island_t *di_in);

void dvi_audio_set_fs_cb(irq_handler_t fn);

void dvi_audio_print_status(void);

#endif /* _DVI_AUDIO_H_ */
