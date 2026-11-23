/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENSIDEWALK_ROUND_DISPLAY_H
#define OPENSIDEWALK_ROUND_DISPLAY_H

#include "round_screen.h"

#ifdef CONFIG_DISPLAY
void round_display_ttn(enum round_ttn_status status);
void round_display_event(enum round_event_kind kind);
void round_display_downlink(uint8_t port, uint8_t bytes, int16_t rssi);
#else
static inline void round_display_ttn(enum round_ttn_status status)
{
	(void)status;
}

static inline void round_display_event(enum round_event_kind kind)
{
	(void)kind;
}

static inline void round_display_downlink(uint8_t port, uint8_t bytes, int16_t rssi)
{
	(void)port;
	(void)bytes;
	(void)rssi;
}
#endif

#endif
