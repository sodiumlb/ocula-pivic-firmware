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
#include "pico_hdmi/hstx_packet.h"

void dvi_audio_init(void);
void dvi_audio_task(void);

bool dvi_audio_push_sample(uint16_t sample);
bool dvi_audio_pop_di(uint32_t *di);

#endif /* _DVI_AUDIO_H_ */
