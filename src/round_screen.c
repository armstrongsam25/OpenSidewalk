/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include "round_screen.h"
#include "telemetry_packet.h"

#define RGB565(r, g, b) (((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3))

static const uint16_t background = RGB565(17, 30, 46);
static const uint16_t edge = RGB565(10, 17, 28);
static const uint16_t rim = RGB565(55, 83, 108);
static const uint16_t separator = RGB565(56, 78, 101);
static const uint16_t pill = RGB565(32, 50, 71);
static const uint16_t ink = RGB565(245, 250, 253);
static const uint16_t muted = RGB565(166, 188, 210);
static const uint16_t aqua = RGB565(115, 222, 235);

static const struct {
	const char *label;
	uint16_t color;
} ttn[] = {
	[ROUND_TTN_STARTING] = {"TTN starting", RGB565(166, 188, 210)},
	[ROUND_TTN_UNCONFIGURED] = {"TTN setup needed", RGB565(255, 205, 131)},
	[ROUND_TTN_JOINING] = {"Joining TTN", RGB565(255, 205, 131)},
	[ROUND_TTN_RETRY] = {"TTN retry scheduled", RGB565(255, 205, 131)},
	[ROUND_TTN_JOINED] = {"TTN joined", RGB565(129, 235, 179)},
	[ROUND_TTN_ERROR] = {"TTN error", RGB565(255, 146, 138)},
};

struct round_glyph {
	uint16_t offset;
	uint8_t width;
};

struct round_font {
	const struct round_glyph *glyphs;
	const uint8_t *pixels;
	uint8_t height;
};

#include "round_screen_font.h"

struct strip {
	uint16_t *pixels;
	int top;
	int end;
};

static void fill(const struct strip *strip, int x, int y, int w, int h, uint16_t color)
{
	for (int row = y < strip->top ? strip->top : y; row < y + h && row < strip->end; row++) {
		uint16_t *dst = strip->pixels + (row - strip->top) * ROUND_SIZE + x;

		for (int col = 0; col < w; col++) {
			dst[col] = color;
		}
	}
}

static void disc(const struct strip *strip, int x, int y, int r, uint16_t color)
{
	for (int dy = -r; dy <= r; dy++) {
		int dx = r;

		while (dx * dx + dy * dy > r * r) {
			dx--;
		}
		fill(strip, x - dx, y + dy, dx * 2 + 1, 1, color);
	}
}

static uint16_t blend(uint16_t fg, uint16_t bg, unsigned int alpha)
{
	unsigned int inverse = 3 - alpha;
	unsigned int r = ((fg >> 11) * alpha + (bg >> 11) * inverse + 1) / 3;
	unsigned int g = (((fg >> 5) & 63) * alpha + ((bg >> 5) & 63) * inverse + 1) / 3;
	unsigned int b = ((fg & 31) * alpha + (bg & 31) * inverse + 1) / 3;

	return (r << 11) | (g << 5) | b;
}

static int text_width(const struct round_font *font, const char *label)
{
	int width = 0;

	for (const unsigned char *ch = (const unsigned char *)label; *ch; ch++) {
		width += font->glyphs[*ch].width;
	}
	return width;
}

static void text(const struct strip *strip, const struct round_font *font, const char *label,
		 int x, int top, uint16_t color, uint16_t backdrop)
{
	int first = strip->top > top ? strip->top - top : 0;
	int last = strip->end - top;
	const uint16_t shades[] = {backdrop, blend(color, backdrop, 1),
				   blend(color, backdrop, 2), color};

	if (first >= font->height || last <= 0) {
		return;
	}
	if (last > font->height) {
		last = font->height;
	}
	for (const unsigned char *ch = (const unsigned char *)label; *ch; ch++) {
		const struct round_glyph *glyph = &font->glyphs[*ch];

		for (int y = first; y < last; y++) {
			uint16_t *dst = strip->pixels + (top + y - strip->top) * ROUND_SIZE;

			for (int dx = 0; dx < glyph->width; dx++) {
				unsigned int i = y * glyph->width + dx;
				unsigned int a = (font->pixels[glyph->offset + i / 4] >>
						  (6 - 2 * (i % 4))) & 3;

				if (a && x + dx >= 0 && x + dx < ROUND_SIZE) {
					dst[x + dx] = shades[a];
				}
			}
		}
		x += glyph->width;
	}
}

static void center_label(const struct strip *strip, const char *label, int top, uint16_t color)
{
	text(strip, &label_font, label, (ROUND_SIZE - text_width(&label_font, label)) / 2,
	     top, color, background);
}

static void format_tenths(char *out, size_t length, int number, const char *suffix)
{
	unsigned int absolute = number < 0 ? -number : number;

	snprintf(out, length, "%s%u.%u%s", number < 0 ? "-" : "", absolute / 10,
		 absolute % 10, suffix);
}

void round_screen_temperature(struct round_view *view, int16_t centi_c)
{
	if (centi_c == TELEMETRY_TEMPERATURE_UNAVAILABLE) {
		strcpy(view->fahrenheit, "--");
		strcpy(view->celsius, "Sensor unavailable");
		return;
	}
	int sign = centi_c < 0 ? -1 : 1;
	int celsius = (centi_c + sign * 5) / 10;
	int scaled_f = centi_c * 9 + 16000;
	int fahrenheit = (scaled_f + (scaled_f < 0 ? -25 : 25)) / 50;

	format_tenths(view->fahrenheit, sizeof(view->fahrenheit), fahrenheit, "");
	format_tenths(view->celsius, sizeof(view->celsius), celsius, " \260" "C");
}

void round_screen_event(struct round_view *view, const struct round_event *event)
{
	view->event_detail[0] = '\0';
	switch (event->kind) {
	case ROUND_EVENT_DOWNLINK:
		view->event_title = "Downlink received";
		if (event->port == 0) {
			snprintf(view->event_detail, sizeof(view->event_detail), "Network / %d dBm",
				 event->rssi);
		} else {
			snprintf(view->event_detail, sizeof(view->event_detail), "Port %u / %u B / %d dBm",
				 event->port, event->bytes, event->rssi);
		}
		break;
	case ROUND_EVENT_BUTTON_0:
		view->event_title = "BTN0 pressed";
		break;
	case ROUND_EVENT_BUTTON_1:
		view->event_title = "BTN1 pressed";
		break;
	case ROUND_EVENT_BLE_CONNECTED:
		view->event_title = "Bluetooth connected";
		break;
	case ROUND_EVENT_BLE_DISCONNECTED:
		view->event_title = "Bluetooth disconnected";
		break;
	default:
		view->event_title = "Waiting for activity";
		break;
	}
}

const char *round_screen_status(enum round_ttn_status status)
{
	return ttn[status].label;
}

void round_screen_render(const struct round_view *view, uint16_t *pixels, int top, int height)
{
	const struct strip strip = {.pixels = pixels, .top = top, .end = top + height};
	const char *status = round_screen_status(view->status);
	int status_x = (ROUND_SIZE - text_width(&label_font, status) - 15) / 2;
	int number_width = text_width(&number_font, view->fahrenheit);
	const char *unit = view->fahrenheit[0] == '-' && view->fahrenheit[1] == '-' ?
			   "" : "\260" "F";
	int number_x = (ROUND_SIZE - number_width - text_width(&label_font, unit) - 4) / 2;

	for (int y = top; y < strip.end; y++) {
		int dy = y - ROUND_SIZE / 2;

		for (int x = 0; x < ROUND_SIZE; x++) {
			int dx = x - ROUND_SIZE / 2;
			int radius_squared = dx * dx + dy * dy;

			*pixels++ = radius_squared > 117 * 117 ? edge :
				    radius_squared >= 115 * 115 ? rim : background;
		}
	}
	if (top < ROUND_STATUS_END && strip.end > ROUND_STATUS_TOP) {
		fill(&strip, 58, 29, 125, 26, pill);
		disc(&strip, 58, 42, 12, pill);
		disc(&strip, 182, 42, 12, pill);
		disc(&strip, status_x + 4, 42, 3, ttn[view->status].color);
		text(&strip, &label_font, status, status_x + 15, 32, ink, pill);
	}
	center_label(&strip, "MCU temperature", 63, muted);
	text(&strip, &number_font, view->fahrenheit, number_x, 83, ink, background);
	text(&strip, &label_font, unit, number_x + number_width + 4, 110, aqua, background);
	center_label(&strip, view->celsius, 136, aqua);
	fill(&strip, 54, 162, 132, 1, separator);
	center_label(&strip, view->event_title, 170, ink);
	center_label(&strip, view->event_detail, 188, muted);
}
