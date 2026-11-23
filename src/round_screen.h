/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENSIDEWALK_ROUND_SCREEN_H
#define OPENSIDEWALK_ROUND_SCREEN_H

#include <stdint.h>

enum {
	ROUND_SIZE = 240,
	ROUND_STATUS_TOP = 28,
	ROUND_STATUS_END = 56,
	ROUND_TEMP_TOP = 62,
	ROUND_TEMP_END = 156,
	ROUND_EVENT_TOP = 168,
	ROUND_EVENT_END = 208,
};

enum round_ttn_status {
	ROUND_TTN_STARTING,
	ROUND_TTN_UNCONFIGURED,
	ROUND_TTN_JOINING,
	ROUND_TTN_RETRY,
	ROUND_TTN_JOINED,
	ROUND_TTN_ERROR,
};

enum round_event_kind {
	ROUND_EVENT_NONE,
	ROUND_EVENT_DOWNLINK,
	ROUND_EVENT_BUTTON_0,
	ROUND_EVENT_BUTTON_1,
	ROUND_EVENT_BLE_CONNECTED,
	ROUND_EVENT_BLE_DISCONNECTED,
};

struct round_event {
	enum round_event_kind kind;
	uint8_t port;
	uint8_t bytes;
	int16_t rssi;
};

struct round_view {
	enum round_ttn_status status;
	char fahrenheit[12];
	char celsius[24];
	const char *event_title;
	char event_detail[32];
};

void round_screen_temperature(struct round_view *view, int16_t centi_c);
void round_screen_event(struct round_view *view, const struct round_event *event);
const char *round_screen_status(enum round_ttn_status status);
void round_screen_render(const struct round_view *view, uint16_t *pixels, int top, int height);

#endif
