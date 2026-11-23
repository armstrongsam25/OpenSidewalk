/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include "round_display.h"
#include "telemetry.h"

LOG_MODULE_REGISTER(kg100s_display, LOG_LEVEL_INF);

#define LCD_NODE DT_NODELABEL(round_display)
#define STRIP_HEIGHT 8
#define TEMPERATURE_INTERVAL_MS 3000
#define EVENT_HOLD_MS 3000

#ifdef OPENSIDEWALK_BUTTON_ANIMATION
enum {
	ANIMATION_SOURCE_SIZE = 144,
	ANIMATION_PLAYBACK_SIZE = 192,
	ANIMATION_FRAME_MS = 125,
	ANIMATION_PALETTE_SIZE = 16,
	ANIMATION_PALETTE_BYTES = ANIMATION_PALETTE_SIZE * sizeof(uint16_t),
	ANIMATION_FRAME_BYTES = ANIMATION_PALETTE_BYTES +
				ANIMATION_SOURCE_SIZE * ANIMATION_SOURCE_SIZE / 2,
	ANIMATION_HEADER_BYTES = sizeof(uint16_t),
};

/* Packed by tools/pack_button_gif.py. The first two bytes store duration in ms. */
static const uint8_t button_animation[] = {
#include "button-animation.inc"
};

BUILD_ASSERT(sizeof(button_animation) > ANIMATION_HEADER_BYTES);
BUILD_ASSERT((sizeof(button_animation) - ANIMATION_HEADER_BYTES) % ANIMATION_FRAME_BYTES == 0);
BUILD_ASSERT(ANIMATION_PLAYBACK_SIZE <= ROUND_SIZE);
BUILD_ASSERT(ANIMATION_PLAYBACK_SIZE % STRIP_HEIGHT == 0);
static atomic_t animation_requested;
#endif

BUILD_ASSERT(DT_PROP(LCD_NODE, width) == ROUND_SIZE);
BUILD_ASSERT(DT_PROP(LCD_NODE, height) == ROUND_SIZE);

static const struct device *const display = DEVICE_DT_GET(LCD_NODE);
static uint16_t pixels[ROUND_SIZE * STRIP_HEIGHT];
static atomic_t ttn_status = ATOMIC_INIT(ROUND_TTN_STARTING);
static atomic_t color_bars_requested;
K_MSGQ_DEFINE(display_events, sizeof(struct round_event), 8, 4);

void round_display_ttn(enum round_ttn_status status)
{
	atomic_set(&ttn_status, status);
}

void round_display_event(enum round_event_kind kind)
{
	const struct round_event event = {.kind = kind};

	k_msgq_put(&display_events, &event, K_NO_WAIT);
#ifdef OPENSIDEWALK_BUTTON_ANIMATION
	if (kind == ROUND_EVENT_BUTTON_0) {
		atomic_set(&animation_requested, 1);
	}
#endif
}

void round_display_downlink(uint8_t port, uint8_t bytes, int16_t rssi)
{
	const struct round_event event = {
		.kind = ROUND_EVENT_DOWNLINK, .port = port, .bytes = bytes, .rssi = rssi,
	};

	k_msgq_put(&display_events, &event, K_NO_WAIT);
}

static int write_strip(int x, int y, int width, int height)
{
	const struct display_buffer_descriptor strip = {
		.buf_size = width * height * sizeof(uint16_t),
		.width = width,
		.height = height,
		.pitch = width,
	};

	for (int i = 0; i < width * height; i++) {
		pixels[i] = sys_cpu_to_be16(pixels[i]);
	}
	return display_write(display, x, y, &strip, pixels);
}

static int draw_rows(const struct round_view *view, int first, int end)
{
	for (int y = first; y < end; y += STRIP_HEIGHT) {
		int height = MIN(STRIP_HEIGHT, end - y);

		round_screen_render(view, pixels, y, height);
		int ret = write_strip(0, y, ROUND_SIZE, height);

		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int draw_color_bars(void)
{
	static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f};
	const int band_height = ROUND_SIZE / ARRAY_SIZE(colors);

	for (int y = 0; y < ROUND_SIZE; y += STRIP_HEIGHT) {
		for (size_t i = 0; i < ARRAY_SIZE(pixels); i++) {
			pixels[i] = colors[y / band_height];
		}
		int ret = write_strip(0, y, ROUND_SIZE, STRIP_HEIGHT);

		if (ret) {
			return ret;
		}
	}
	return display_blanking_off(display);
}

#ifdef OPENSIDEWALK_BUTTON_ANIMATION
static int draw_animation_frame(unsigned int frame_number)
{
	const uint8_t *frame = button_animation + ANIMATION_HEADER_BYTES +
			       frame_number * ANIMATION_FRAME_BYTES;
	const uint8_t *indices = frame + ANIMATION_PALETTE_BYTES;
	const int margin = (ROUND_SIZE - ANIMATION_PLAYBACK_SIZE) / 2;
	uint16_t palette[ANIMATION_PALETTE_SIZE];

	for (size_t i = 0; i < ARRAY_SIZE(palette); i++) {
		palette[i] = sys_get_be16(frame + i * sizeof(uint16_t));
	}
	for (int y = 0; y < ANIMATION_PLAYBACK_SIZE; y += STRIP_HEIGHT) {
		for (int row = 0; row < STRIP_HEIGHT; row++) {
			int source_y = (y + row) * ANIMATION_SOURCE_SIZE / ANIMATION_PLAYBACK_SIZE;
			const uint8_t *source_row = indices + source_y * (ANIMATION_SOURCE_SIZE / 2);
			uint16_t *dest = pixels + row * ANIMATION_PLAYBACK_SIZE;

			for (int x = 0; x < ANIMATION_PLAYBACK_SIZE; x++) {
				int source_x = x * ANIMATION_SOURCE_SIZE / ANIMATION_PLAYBACK_SIZE;
				uint8_t packed = source_row[source_x / 2];

				dest[x] = palette[(source_x % 2) ? packed & 0x0f : packed >> 4];
			}
		}
		int ret = write_strip(margin, margin + y, ANIMATION_PLAYBACK_SIZE, STRIP_HEIGHT);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int play_button_animation(void)
{
	const unsigned int duration_ms = sys_get_be16(button_animation);
	const size_t frames = (sizeof(button_animation) - ANIMATION_HEADER_BYTES) /
			      ANIMATION_FRAME_BYTES;

	if (DIV_ROUND_UP(duration_ms, ANIMATION_FRAME_MS) != frames) {
		return -EINVAL;
	}
	memset(pixels, 0, sizeof(pixels));
	for (int y = 0; y < ROUND_SIZE; y += STRIP_HEIGHT) {
		int ret = write_strip(0, y, ROUND_SIZE, STRIP_HEIGHT);
		if (ret) {
			return ret;
		}
	}
	int64_t start = k_uptime_get();
	int64_t elapsed = 0;
	unsigned int drawn = 0;

	while (elapsed < duration_ms) {
		unsigned int frame = elapsed / ANIMATION_FRAME_MS;
		int ret = draw_animation_frame(frame);
		if (ret) {
			return ret;
		}
		drawn++;
		int64_t next = MIN((frame + 1) * ANIMATION_FRAME_MS, duration_ms);
		elapsed = k_uptime_get() - start;
		if (elapsed < next) {
			k_msleep(next - elapsed);
		}
		/* Skip overdue source frames to retain the original playback time. */
		elapsed = k_uptime_get() - start;
	}
	LOG_INF("Button animation finished: %u frames", drawn);
	return 0;
}
#endif

static int draw_dashboard(const struct round_view *view, const struct round_view *previous,
			  bool full, bool event_changed)
{
	int ret = 0;

	if (full) {
		ret = draw_rows(view, 0, ROUND_SIZE);
		return ret ? ret : display_blanking_off(display);
	}
	if (view->status != previous->status) {
		ret = draw_rows(view, ROUND_STATUS_TOP, ROUND_STATUS_END);
	}
	if (!ret && (strcmp(view->fahrenheit, previous->fahrenheit) ||
		     strcmp(view->celsius, previous->celsius))) {
		ret = draw_rows(view, ROUND_TEMP_TOP, ROUND_TEMP_END);
	}
	if (!ret && event_changed) {
		ret = draw_rows(view, ROUND_EVENT_TOP, ROUND_EVENT_END);
	}
	return ret;
}

static void display_loop(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);
	struct round_view view = {0};
	struct round_view previous = {0};
	struct round_event event = {0};
	int64_t next_temperature = 0;
	int64_t event_visible_until = 0;
	bool full = true;
	bool event_changed = false;

	if (!device_is_ready(display)) {
		LOG_ERR("GC9A01 driver is unavailable");
		return;
	}
	round_screen_event(&view, &event);
	while (true) {
#ifdef OPENSIDEWALK_BUTTON_ANIMATION
		if (atomic_get(&animation_requested)) {
			LOG_INF("Playing button animation");
			int ret = play_button_animation();

			if (ret) {
				LOG_WRN("Button animation failed: %d", ret);
			}
			/* A press during playback does not queue another loop. */
			atomic_clear(&animation_requested);
			full = true;
		}
#endif
		if (atomic_cas(&color_bars_requested, 1, 0)) {
			int ret = draw_color_bars();

			if (ret) {
				LOG_WRN("Color-bar test failed: %d", ret);
			} else {
				LOG_INF("Sent color bars to the GC9A01");
				k_sleep(K_SECONDS(3));
			}
			full = true;
		}
		int64_t now = k_uptime_get();
		view.status = atomic_get(&ttn_status);
		if (now >= next_temperature) {
			struct telemetry_sample sample;

			telemetry_read(&sample);
			round_screen_temperature(&view, sample.temperature_centi_c);
			next_temperature = now + TEMPERATURE_INTERVAL_MS;
		}
		if (!event_changed && now >= event_visible_until &&
		    !k_msgq_get(&display_events, &event, K_NO_WAIT)) {
			round_screen_event(&view, &event);
			event_changed = true;
		}
		int ret = draw_dashboard(&view, &previous, full, event_changed);

		if (ret) {
			LOG_WRN("Dashboard update failed: %d", ret);
			full = true;
			k_sleep(K_SECONDS(1));
			continue;
		}
		if (full || view.status != previous.status) {
			LOG_INF("Dashboard: %s, MCU %s F", round_screen_status(view.status),
				view.fahrenheit);
		}
		if (event_changed) {
			LOG_INF("Dashboard: %s", view.event_title);
			event_visible_until = k_uptime_get() + EVENT_HOLD_MS;
			event_changed = false;
		}
		previous = view;
		full = false;
		k_sleep(K_MSEC(200));
	}
}

K_THREAD_DEFINE(dashboard, 2304, display_loop, NULL, NULL, NULL, K_PRIO_PREEMPT(12), 0, 0);

static int cmd_lcd_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!device_is_ready(display)) {
		shell_error(sh, "GC9A01 driver is unavailable");
		return -ENODEV;
	}
	atomic_set(&color_bars_requested, 1);
	shell_print(sh, "Showing color bars for three seconds, then resuming the dashboard");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(lcd_commands,
	SHELL_CMD_ARG(test, NULL, "Draw red, green, and blue bars.", cmd_lcd_test, 1, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(lcd, &lcd_commands, "Round GC9A01 display", NULL);
