/*
 * mipiscreen-ui - commercial MIPI framebuffer dashboard for NWRT
 * Native UI: boot animation, themes, touch menu, no browser.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include "font_cn.h"
#include <dirent.h>

#define RELOAD_FLAG   "/tmp/mipiscreen.reload"
#define TEST_FLAG     "/tmp/mipiscreen.test"
#define BOOT_WAIT_MS  45000
#define FPS_BOOT      30
#define FPS_DASH      5
#define TOUCH_RETRY_MS 5000
#define SAFE_TOP      20
#define SAFE_BOTTOM   30
#define SAFE_SIDE     12

static volatile int g_running = 1;

typedef struct {
	uint32_t bg, card, primary, accent, text, dim, warn, bar_bg, stripe;
} theme_t;

typedef struct {
	int fd;
	uint8_t *mem;
	uint8_t *back;
	size_t mem_len;
	int w, h, stride, bpp; // Physical size
	int vw, vh;            // Virtual size (after rotation)
	int rotation;          // 0, 90, 180, 270
	uint32_t r_off, g_off, b_off, a_off;
	int r_len, g_len, b_len, a_len;
} fb_t;

typedef struct {
	int fd;
	int x, y, max_x, max_y;
	int down;
	int last_down;
	char name[64];
} touch_t;

typedef struct {
	int cpu_usage, cpu_temp, mem_usage;
	int cpu_freq, mem_total, storage_usage, storage_free;
	int rx_speed, tx_speed;
	int uptime, clients, cell_signal;
	char wan_ip[40], lan_ip[40];
	char hostname[32];
	char kernel[32], local_time[32];
	char cell_op[48];
	char cell_type[24];
	char cell_status[16];
	char load_avg[24];
	int ready;
} status_t;

typedef struct {
	float boot_progress, boot_target;
	float ring_angle;
	float disp_cpu, disp_mem, disp_speed;
	int show_menu, menu_page, page, max_pages;
	int blanked;
	int enabled;
	int backlight;
	int timeout_sec;
	int touch_wake;
	int boot_anim;
	int show_network, show_system;
	int theme_idx;
	int rotation;
	int test_pattern;
	uint64_t last_touch_ms;
	uint64_t boot_start_ms;
	uint64_t last_uci_ms;
	uint64_t test_until_ms;
	enum { UI_BOOT, UI_DASH } mode;
} ui_t;

static const theme_t g_themes[] = {
	{ 0xFF05080C, 0xFF0D1219, 0xFF1565C0, 0xFF42A5F5, 0xFFECEFF1, 0xFF78909C, 0xFFFFB300, 0xFF1A2330, 0xFF0D47A1 },
	{ 0xFF050810, 0xFF101820, 0xFF00B8D4, 0xFF18FFFF, 0xFFE0F7FA, 0xFF607D8B, 0xFF00E5FF, 0xFF152535, 0xFF006064 },
	{ 0xFF100A05, 0xFF1A120C, 0xFFFF6D00, 0xFFFFAB40, 0xFFFFF3E0, 0xFF8D6E63, 0xFFFF9100, 0xFF251A10, 0xFFE65100 },
	{ 0xFFF5F7FA, 0xFFFFFFFF, 0xFF1976D2, 0xFF42A5F5, 0xFF263238, 0xFF78909C, 0xFFFF8F00, 0xFFECEFF1, 0xFF1565C0 },
};
static const char *g_theme_names[] = { "dark", "cyan", "orange", "light" };

static int backlight_set(int val);
static void display_wake(ui_t *ui);
static void display_sleep(ui_t *ui);
static void draw_menu(fb_t *fb, ui_t *ui, const theme_t *th);

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

static void on_signal(int sig)
{
	(void)sig;
	g_running = 0;
}

static const theme_t *ui_theme(const ui_t *ui)
{
	if (ui->theme_idx < 0 || ui->theme_idx >= (int)(sizeof(g_themes) / sizeof(g_themes[0])))
		return &g_themes[0];
	return &g_themes[ui->theme_idx];
}

/* minimal 8x8 font */
static const uint8_t font8x8[95][8] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 },
	{ 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00 },
	{ 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00 },
	{ 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00 },
	{ 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00 },
	{ 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00 },
	{ 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00 },
	{ 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 },
	{ 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06 },
	{ 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00 },
	{ 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00 },
	{ 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00 },
	{ 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00 },
	{ 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00 },
	{ 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00 },
	{ 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00 },
	{ 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00 },
	{ 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00 },
	{ 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00 },
	{ 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00 },
	{ 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00 },
	{ 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00 },
	{ 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06 },
	{ 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00 },
	{ 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00 },
	{ 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00 },
	{ 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00 },
	{ 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00 },
	{ 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00 },
	{ 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00 },
	{ 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00 },
	{ 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00 },
	{ 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00 },
	{ 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00 },
	{ 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00 },
	{ 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00 },
	{ 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },
	{ 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00 },
	{ 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00 },
	{ 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00 },
	{ 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00 },
	{ 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00 },
	{ 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00 },
	{ 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00 },
	{ 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00 },
	{ 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00 },
	{ 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00 },
	{ 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },
	{ 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00 },
	{ 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 },
	{ 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 },
	{ 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00 },
	{ 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00 },
	{ 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00 },
	{ 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00 },
	{ 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00 },
	{ 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00 },
	{ 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF },
	{ 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00 },
	{ 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00 },
	{ 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00 },
	{ 0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00 },
	{ 0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00 },
	{ 0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00 },
	{ 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F },
	{ 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00 },
	{ 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },
	{ 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E },
	{ 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00 },
	{ 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },
	{ 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00 },
	{ 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00 },
	{ 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00 },
	{ 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F },
	{ 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78 },
	{ 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00 },
	{ 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00 },
	{ 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00 },
	{ 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00 },
	{ 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 },
	{ 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00 },
	{ 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00 },
	{ 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F },
	{ 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00 },
	{ 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00 },
	{ 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 },
	{ 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00 },
	{ 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static void get_virt_coordinates(fb_t *fb, int phys_x, int phys_y, int *virt_x, int *virt_y)
{
	switch (fb->rotation) {
		case 90:
			*virt_x = phys_y;
			*virt_y = fb->w - 1 - phys_x;
			break;
		case 180:
			*virt_x = fb->w - 1 - phys_x;
			*virt_y = fb->h - 1 - phys_y;
			break;
		case 270:
			*virt_x = fb->h - 1 - phys_y;
			*virt_y = phys_x;
			break;
		case 0:
		default:
			*virt_x = phys_x;
			*virt_y = phys_y;
			break;
	}
}

static void fb_put_pixel(fb_t *fb, int x, int y, uint32_t c)
{
	if (x < 0 || y < 0 || x >= fb->vw || y >= fb->vh)
		return;

	int phys_x = x;
	int phys_y = y;

	switch (fb->rotation) {
		case 90:
			phys_x = fb->w - 1 - y;
			phys_y = x;
			break;
		case 180:
			phys_x = fb->w - 1 - x;
			phys_y = fb->h - 1 - y;
			break;
		case 270:
			phys_x = y;
			phys_y = fb->h - 1 - x;
			break;
		case 0:
		default:
			break;
	}

	// Double safeguard against physical bounds overflow
	if (phys_x < 0 || phys_y < 0 || phys_x >= fb->w || phys_y >= fb->h)
		return;

	uint8_t *p = fb->back + phys_y * fb->stride + phys_x * (fb->bpp / 8);
	if (fb->bpp == 16) {
		/* Panel wiring uses a B-R-G channel cycle. */
		uint32_t rv = (c & 0xFF) >> (8 - fb->r_len);
		uint32_t gv = ((c >> 16) & 0xFF) >> (8 - fb->g_len);
		uint32_t bv = ((c >> 8) & 0xFF) >> (8 - fb->b_len);
		uint32_t px = (rv << fb->r_off) | (gv << fb->g_off) | (bv << fb->b_off);
		p[0] = px & 0xFF;
		p[1] = (px >> 8) & 0xFF;
	} else if (fb->bpp == 32) {
		uint32_t rv = (c & 0xFF) >> (8 - fb->r_len);
		uint32_t gv = ((c >> 16) & 0xFF) >> (8 - fb->g_len);
		uint32_t bv = ((c >> 8) & 0xFF) >> (8 - fb->b_len);
		uint32_t av = ((c >> 24) & 0xFF);
		if (fb->a_len > 0) av >>= (8 - fb->a_len);
		else av = 0;
		uint32_t px = (rv << fb->r_off) | (gv << fb->g_off) | (bv << fb->b_off) | (av << fb->a_off);
		*((uint32_t*)p) = px;
	}
}

static void fb_present(fb_t *fb)
{
	memcpy(fb->mem, fb->back, fb->mem_len);
}

static void fb_fill(fb_t *fb, uint32_t c)
{
	int x, y;
	for (y = 0; y < fb->vh; y++)
		for (x = 0; x < fb->vw; x++)
			fb_put_pixel(fb, x, y, c);
}

static void fb_rect(fb_t *fb, int x, int y, int w, int h, uint32_t c)
{
	int i, j;
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++)
			fb_put_pixel(fb, x + i, y + j, c);
}

static void fb_hline(fb_t *fb, int x, int y, int w, uint32_t c)
{
	int i;
	for (i = 0; i < w; i++)
		fb_put_pixel(fb, x + i, y, c);
}

static void fb_text(fb_t *fb, int x, int y, const char *s, uint32_t c, int scale)
{
	int cx = x;
	while (*s) {
		unsigned char ch = (unsigned char)*s++;
		const uint8_t *glyph;
		int row, col;
		if (ch < 32 || ch > 126) {
			cx += 8 * scale;
			continue;
		}
		glyph = font8x8[ch - 32];
		for (row = 0; row < 8; row++) {
			uint8_t bits = glyph[row];
			for (col = 0; col < 8; col++) {
				if (bits & (1 << col)) {
					int px = cx + col * scale;
					int py = y + row * scale;
					int sx, sy;
					for (sy = 0; sy < scale; sy++)
						for (sx = 0; sx < scale; sx++)
							fb_put_pixel(fb, px + sx, py + sy, c);
				}
			}
		}
		cx += 9 * scale;
	}
}


static int utf8_to_unicode(const char **s)
{
	unsigned const char *str = (unsigned const char *)*s;
	int c = *str++;
	int res = 0;
	if (c < 0x80) {
		res = c;
	} else if (c < 0xE0) {
		res = ((c & 0x1F) << 6) | (*str++ & 0x3F);
	} else if (c < 0xF0) {
		res = ((c & 0x0F) << 12) | ((*str & 0x3F) << 6); str++;
		res |= (*str++ & 0x3F);
	} else {
		res = '?';
	}
	*s = (const char *)str;
	return res;
}

static void fb_text_cn(fb_t *fb, int x, int y, const char *s, uint32_t c, int scale)
{
	int cx = x;
	while (*s) {
		if ((unsigned char)*s < 128) {
			char asc[2] = { *s, 0 };
			fb_text(fb, cx, y + (4 * scale), asc, c, scale);
			cx += 9 * scale;
			s++;
		} else {
			int unicode = utf8_to_unicode(&s);
			const uint8_t *glyph = NULL;
			for (int i = 0; i < FONT_CN_NUM_CHARS; i++) {
				if (font_cn_data[i].unicode == unicode) {
					glyph = font_cn_data[i].data;
					break;
				}
			}
			if (glyph) {
				for (int row = 0; row < 16; row++) {
					uint16_t row_bits = (glyph[row * 2] << 8) | glyph[row * 2 + 1];
					for (int col = 0; col < 16; col++) {
						if (row_bits & (1 << (15 - col))) {
							int px = cx + col * scale;
							int py = y + row * scale;
							for (int sy = 0; sy < scale; sy++)
								for (int sx = 0; sx < scale; sx++)
									fb_put_pixel(fb, px + sx, py + sy, c);
						}
					}
				}
			}
			cx += 17 * scale;
		}
	}
}

static void fb_arc(fb_t *fb, int cx, int cy, int r, float a0, float a1, uint32_t c, int thick)
{
	float a;
	for (a = a0; a <= a1; a += 0.025f) {
		int x = cx + (int)(cosf(a) * r);
		int y = cy + (int)(sinf(a) * r);
		int t;
		for (t = 0; t < thick; t++) {
			fb_put_pixel(fb, x + t, y, c);
			fb_put_pixel(fb, x, y + t, c);
		}
	}
}

static int fb_open(fb_t *fb)
{
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	long pagesize;
	FILE *debug_log;

	memset(fb, 0, sizeof(*fb));
	fb->fd = open("/dev/fb0", O_RDWR);
	if (fb->fd < 0)
		return -1;
	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) < 0)
		return -1;
	if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) < 0)
		return -1;
	
	// Debug: log framebuffer format info
	debug_log = fopen("/tmp/fb_debug.log", "w");
	if (debug_log) {
		fprintf(debug_log, "FB Resolution: %dx%d\n", vinfo.xres, vinfo.yres);
		fprintf(debug_log, "FB BPP: %d\n", vinfo.bits_per_pixel);
		fprintf(debug_log, "FB Stride: %d\n", finfo.line_length);
		fprintf(debug_log, "Red: offset=%d, length=%d\n", vinfo.red.offset, vinfo.red.length);
		fprintf(debug_log, "Green: offset=%d, length=%d\n", vinfo.green.offset, vinfo.green.length);
		fprintf(debug_log, "Blue: offset=%d, length=%d\n", vinfo.blue.offset, vinfo.blue.length);
		fprintf(debug_log, "Alpha: offset=%d, length=%d\n", vinfo.transp.offset, vinfo.transp.length);
		fclose(debug_log);
	}
	
	fb->w = (int)vinfo.xres;
	fb->h = (int)vinfo.yres;
	fb->bpp = (int)vinfo.bits_per_pixel;
	fb->stride = (int)finfo.line_length;
	fb->r_off = vinfo.red.offset;
	fb->g_off = vinfo.green.offset;
	fb->b_off = vinfo.blue.offset;
	fb->a_off = vinfo.transp.offset;
	fb->r_len = vinfo.red.length;
	fb->g_len = vinfo.green.length;
	fb->b_len = vinfo.blue.length;
	fb->a_len = vinfo.transp.length;
	
	// Default virtual matches physical
	fb->rotation = 0;
	fb->vw = fb->w;
	fb->vh = fb->h;

	pagesize = sysconf(_SC_PAGESIZE);
	fb->mem_len = ((size_t)fb->stride * fb->h + pagesize - 1) & ~(pagesize - 1);
	fb->mem = mmap(NULL, fb->mem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
	if (fb->mem == MAP_FAILED)
		return -1;
	fb->back = calloc(1, fb->mem_len);
	if (!fb->back) {
		munmap(fb->mem, fb->mem_len);
		fb->mem = MAP_FAILED;
		return -1;
	}
	return 0;
}

static void fb_close(fb_t *fb)
{
	if (fb->mem && fb->mem != MAP_FAILED)
		munmap(fb->mem, fb->mem_len);
	free(fb->back);
	if (fb->fd >= 0)
		close(fb->fd);
}

static void fb_unblank(void)
{
	FILE *f = fopen("/sys/class/graphics/fb0/blank", "w");
	if (f) {
		fprintf(f, "0");
		fclose(f);
	}
}

static int fb_wait_ready(void)
{
	uint64_t start = now_ms();
	while (now_ms() - start < BOOT_WAIT_MS) {
		if (access("/dev/fb0", R_OK | W_OK) == 0)
			return 0;
		usleep(200000);
	}
	return -1;
}

static int read_line_file(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	if (!fgets(buf, len, f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

static int backlight_set(int val)
{
	char path[256], max_path[256];
	DIR *d = opendir("/sys/class/backlight");
	struct dirent *e;
	int found = 0;

	if (!d)
		return -1;
	if (val < 0)
		val = 0;
	if (val > 255)
		val = 255;

	while ((e = readdir(d))) {
		FILE *f;
		int max_val = 255;

		if (e->d_name[0] == '.')
			continue;
		found = 1;
		snprintf(max_path, sizeof(max_path),
			 "/sys/class/backlight/%s/max_brightness", e->d_name);
		f = fopen(max_path, "r");
		if (f) {
			if (fscanf(f, "%d", &max_val) != 1 || max_val <= 0)
				max_val = 255;
			fclose(f);
		}

		if (val > 0) {
			snprintf(path, sizeof(path),
				 "/sys/class/backlight/%s/brightness", e->d_name);
			f = fopen(path, "w");
			if (f) {
				fprintf(f, "%d", max_val);
				fclose(f);
			}
		}

		snprintf(path, sizeof(path),
			 "/sys/class/backlight/%s/bl_power", e->d_name);
		f = fopen(path, "w");
		if (f) {
			fprintf(f, "%d", val > 0 ? 0 : 4);
			fclose(f);
		}
	}
	closedir(d);
	return found ? 0 : -1;
}

static void backlight_off(void)
{
	char path[256];
	DIR *d = opendir("/sys/class/backlight");
	struct dirent *e;
	FILE *f;

	if (!d)
		return;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/class/backlight/%s/bl_power", e->d_name);
		f = fopen(path, "w");
		if (f) {
			fprintf(f, "4");
			fclose(f);
		}
	}
	closedir(d);
}

static void display_wake(ui_t *ui)
{
	if (!ui->enabled || !ui->backlight)
		return;
	ui->blanked = 0;
	fb_unblank();
	backlight_set(1);
}

static void display_sleep(ui_t *ui)
{
	ui->blanked = 1;
	backlight_off();
}

static int theme_index_from_name(const char *name)
{
	int i;
	if (!name)
		return 0;
	for (i = 0; i < (int)(sizeof(g_theme_names) / sizeof(g_theme_names[0])); i++)
		if (!strcmp(name, g_theme_names[i]))
			return i;
	return 0;
}

static int uci_get_int(const char *option, int def)
{
	char cmd[128], buf[64];
	FILE *f;
	int value = def;

	snprintf(cmd, sizeof(cmd),
		 "uci -q get mipiscreen.global.%s 2>/dev/null", option);
	f = popen(cmd, "r");
	if (f) {
		if (fgets(buf, sizeof(buf), f))
			value = atoi(buf);
		pclose(f);
	}
	return value;
}

static void uci_get_string(const char *option, char *buf, size_t len,
			   const char *def)
{
	char cmd[128];
	FILE *f;

	snprintf(buf, len, "%s", def);
	snprintf(cmd, sizeof(cmd),
		 "uci -q get mipiscreen.global.%s 2>/dev/null", option);
	f = popen(cmd, "r");
	if (f) {
		if (fgets(buf, len, f))
			buf[strcspn(buf, "\r\n")] = 0;
		pclose(f);
	}
}

static void load_uci(ui_t *ui)
{
	char buf[64];

	ui->enabled = uci_get_int("enabled", 1) != 0;
	ui->backlight = uci_get_int("backlight", 1) != 0;
	ui->timeout_sec = uci_get_int("timeout", 300);
	if (ui->timeout_sec < 0)
		ui->timeout_sec = 300;
	ui->touch_wake = uci_get_int("touch_wake", 1) != 0;
	ui->boot_anim = uci_get_int("boot_animation", 1) != 0;
	ui->show_network = uci_get_int("show_network", 1) != 0;
	ui->show_system = uci_get_int("show_system", 1) != 0;
	ui->rotation = uci_get_int("rotation", 270);
	if (ui->rotation != 0 && ui->rotation != 90 &&
	    ui->rotation != 180 && ui->rotation != 270)
		ui->rotation = 270;
	uci_get_string("theme", buf, sizeof(buf), "dark");
	ui->theme_idx = theme_index_from_name(buf);
}

static int parse_json_int(const char *json, const char *key, int def)
{
	char pat[64];
	char *p;
	snprintf(pat, sizeof(pat), "\"%s\":", key);
	p = strstr(json, pat);
	return p ? atoi(p + strlen(pat)) : def;
}

static char *parse_json_str(const char *json, const char *key, char *out, size_t len)
{
	char pat[64];
	char *p, *q;
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	p = strstr(json, pat);
	if (!p) {
		out[0] = 0;
		return out;
	}
	p += strlen(pat);
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p++ != ':') {
		out[0] = 0;
		return out;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p++ != '"') {
		out[0] = 0;
		return out;
	}
	q = strchr(p, '"');
	if (!q || (size_t)(q - p) >= len) {
		out[0] = 0;
		return out;
	}
	memcpy(out, p, q - p);
	out[q - p] = 0;
	return out;
}

static void status_update(status_t *st)
{
	char json[4096];
	FILE *f;
	size_t n;

	memset(st, 0, sizeof(*st));
	f = popen("ubus call mipiscreen get_status 2>/dev/null", "r");
	if (!f)
		return;
	n = fread(json, 1, sizeof(json) - 1, f);
	pclose(f);
	json[n] = 0;
	if (n < 10)
		return;
	st->cpu_usage = parse_json_int(json, "cpu_usage", 0);
	st->cpu_temp = parse_json_int(json, "cpu_temp", 0);
	st->mem_usage = parse_json_int(json, "mem_usage", 0);
	st->cpu_freq = parse_json_int(json, "cpu_freq", 0);
	st->mem_total = parse_json_int(json, "mem_total", 0);
	st->storage_usage = parse_json_int(json, "storage_usage", 0);
	st->storage_free = parse_json_int(json, "storage_free", 0);
	st->rx_speed = parse_json_int(json, "rx_speed", 0);
	st->tx_speed = parse_json_int(json, "tx_speed", 0);
	st->uptime = parse_json_int(json, "uptime", 0);
	st->clients = parse_json_int(json, "clients", 0);
	st->cell_signal = parse_json_int(json, "cell_signal", -100);
	parse_json_str(json, "wan_ip", st->wan_ip, sizeof(st->wan_ip));
	parse_json_str(json, "lan_ip", st->lan_ip, sizeof(st->lan_ip));
	parse_json_str(json, "hostname", st->hostname, sizeof(st->hostname));
	parse_json_str(json, "kernel", st->kernel, sizeof(st->kernel));
	parse_json_str(json, "local_time", st->local_time, sizeof(st->local_time));
	parse_json_str(json, "cell_operator", st->cell_op, sizeof(st->cell_op));
	parse_json_str(json, "cell_type", st->cell_type, sizeof(st->cell_type));
	parse_json_str(json, "cell_status", st->cell_status, sizeof(st->cell_status));
	parse_json_str(json, "load_avg", st->load_avg, sizeof(st->load_avg));
	st->ready = 1;
}

static float boot_stage_progress(void)
{
	float p = 0.05f;
	FILE *f;

	if (access("/dev/fb0", R_OK | W_OK) == 0)
		p += 0.20f;
	if (access("/var/run/ubus/ubus.sock", F_OK) == 0)
		p += 0.15f;
	f = popen("ubus call system board '{}' 2>/dev/null | grep -q model", "r");
	if (f) {
		if (pclose(f) == 0)
			p += 0.15f;
	}
	f = popen("ip route show default 2>/dev/null | grep -q .", "r");
	if (f) {
		if (pclose(f) == 0)
			p += 0.20f;
	}
	f = popen("ubus call mipiscreen get_status 2>/dev/null | grep -q cpu_usage", "r");
	if (f) {
		if (pclose(f) == 0)
			p += 0.15f;
	}
	if (access("/sys/class/net/wlan0", F_OK) == 0 ||
	    access("/sys/class/net/wwan0", F_OK) == 0 ||
	    access("/sys/class/net/eth0", F_OK) == 0)
		p += 0.10f;
	if (p > 1.0f)
		p = 1.0f;
	return p;
}

static float lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

static void draw_boot(fb_t *fb, ui_t *ui, const theme_t *th)
{
	int bar_w = fb->vw - 64;
	int bar_x = 32;
	int bar_y = fb->vh - 120;
	char buf[64];
	const char *logo = "NWRT";
	int i, lx = fb->vw / 2 - 72;

	fb_fill(fb, th->bg);
	fb_hline(fb, 0, 0, fb->vw, th->primary);
	fb_hline(fb, 0, 1, fb->vw, th->accent & 0x88FFFFFF);

	for (i = 0; logo[i]; i++) {
		char ch[2] = { logo[i], 0 };
		float reveal = (ui->boot_progress * 4.0f) - i;
		uint32_t col = th->text;
		if (reveal < 0.0f)
			col = th->bar_bg;
		else if (reveal < 1.0f)
			col = th->accent;
		fb_text(fb, lx + i * 36, fb->vh / 2 - 40, ch, col, 3);
	}

	ui->ring_angle += 0.10f;
	fb_arc(fb, fb->vw / 2, fb->vh / 2 + 40, 36, ui->ring_angle, ui->ring_angle + 4.0f, th->primary, 3);

	snprintf(buf, sizeof(buf), "Starting services %.0f%%", ui->boot_progress * 100.0f);
	fb_text(fb, 32, bar_y - 28, buf, th->dim, 1);
	fb_rect(fb, bar_x, bar_y, bar_w, 10, th->bar_bg);
	fb_rect(fb, bar_x, bar_y, (int)(bar_w * ui->boot_progress), 10, th->primary);
	fb_text(fb, 32, bar_y + 18, "NWRT Enterprise Gateway", th->accent, 1);
}

static void draw_signal_bars(fb_t *fb, int x, int y, int rssi,
			     int connected, const theme_t *th)
{
	int i, h, bars;

	if (!connected || rssi <= -110) {
		fb_text_cn(fb, x - 18, y + 2, "无信号", th->warn, 1);
		return;
	}

	if (rssi >= -65)
		bars = 4;
	else if (rssi >= -75)
		bars = 3;
	else if (rssi >= -85)
		bars = 2;
	else
		bars = 1;
	for (i = 0; i < 4; i++) {
		h = 8 + i * 5;
		fb_rect(fb, x + i * 10, y + (28 - h), 6, h,
			i < bars ? th->primary : th->bar_bg);
	}
}

static void draw_ring(fb_t *fb, int cx, int cy, int r, float pct, uint32_t c, uint32_t bg)
{
	float end = -1.57f + 6.28f * pct;
	fb_arc(fb, cx, cy, r, -1.57f, 6.28f, bg, 3);
	if (pct > 0.01f)
		fb_arc(fb, cx, cy, r, -1.57f, end, c, 3);
}

static void format_speed(char *buf, size_t len, int bps)
{
	if (bps >= 1024 * 1024)
		snprintf(buf, len, "%.1f MB/s", bps / (1024.0f * 1024.0f));
	else if (bps >= 1024)
		snprintf(buf, len, "%.1f KB/s", bps / 1024.0f);
	else
		snprintf(buf, len, "%d B/s", bps);
}

static int nav_height(const fb_t *fb)
{
	return fb->vh < 600 ? 42 : 48;
}

static int nav_top(const fb_t *fb)
{
	return fb->vh - nav_height(fb) - SAFE_BOTTOM;
}

static int menu_height(const fb_t *fb)
{
	return fb->vh < 600 ? 104 : 140;
}

static int menu_top(const fb_t *fb)
{
	int usable_top = SAFE_TOP;
	int usable_bottom = fb->vh - SAFE_BOTTOM;
	return usable_top + (usable_bottom - usable_top - menu_height(fb)) / 2;
}

static void draw_card_header(fb_t *fb, int x, int y, int w,
			     const char *title, const theme_t *th)
{
	fb_rect(fb, x, y, w, 3, th->primary);
	fb_text_cn(fb, x + 12, y + 10, title, th->accent, 1);
}

static void draw_system_panel(fb_t *fb, int x, int y, int w, int h,
			      ui_t *ui, const status_t *st,
			      const theme_t *th)
{
	char buf[64];
	int ring_y = y + (h > 240 ? 100 : 86);
	int ring_r = h > 240 ? 40 : 34;
	int left = x + w / 4;
	int right = x + (w * 3) / 4;
	int row_y = y + h - 135;

	fb_rect(fb, x, y, w, h, th->card);
	draw_card_header(fb, x, y, w, "系统运行", th);
	draw_ring(fb, left, ring_y, ring_r, ui->disp_cpu / 100.0f,
		  th->accent, th->bar_bg);
	snprintf(buf, sizeof(buf), "%d%%", (int)ui->disp_cpu);
	fb_text(fb, left - 18, ring_y - 8, buf, th->text, 2);
	fb_text_cn(fb, left - 16, ring_y + ring_r + 8, "CPU", th->dim, 1);

	draw_ring(fb, right, ring_y, ring_r, ui->disp_mem / 100.0f,
		  th->warn, th->bar_bg);
	snprintf(buf, sizeof(buf), "%d%%", (int)ui->disp_mem);
	fb_text(fb, right - 18, ring_y - 8, buf, th->text, 2);
	fb_text_cn(fb, right - 18, ring_y + ring_r + 8, "内存", th->dim, 1);

	fb_hline(fb, x + 12, row_y - 14, w - 24, th->bar_bg);
	snprintf(buf, sizeof(buf), "CPU频率 %d MHz   内存 %d MB",
		 st->cpu_freq, st->mem_total);
	fb_text_cn(fb, x + 16, row_y, buf, th->text, 1);
	snprintf(buf, sizeof(buf), "温度 %dC   存储 %d%%  可用 %d MB",
		 st->cpu_temp, st->storage_usage, st->storage_free);
	fb_text_cn(fb, x + 16, row_y + 26, buf,
		   (st->cpu_temp >= 80 || st->storage_usage >= 90) ?
		   th->warn : th->text, 1);
	snprintf(buf, sizeof(buf), "系统负载 %s", st->load_avg[0] ? st->load_avg : "--");
	fb_text_cn(fb, x + 16, row_y + 52, buf, th->dim, 1);
	snprintf(buf, sizeof(buf), "内核 %s", st->kernel[0] ? st->kernel : "--");
	fb_text_cn(fb, x + 16, row_y + 78, buf, th->dim, 1);
	snprintf(buf, sizeof(buf), "运行时间 %dm", st->uptime / 60);
	fb_text_cn(fb, x + 16, row_y + 104, buf, th->accent, 1);
}

static void draw_network_panel(fb_t *fb, int x, int y, int w, int h,
			       ui_t *ui, const status_t *st,
			       const theme_t *th)
{
	char buf[96], rx[24], tx[24];
	int cx = x + w / 2;
	int cy = y + (h > 240 ? 100 : 88);
	int r = h > 240 ? 42 : 34;
	int row_y = y + h - 135;

	fb_rect(fb, x, y, w, h, th->card);
	draw_card_header(fb, x, y, w, "网络与通信", th);
	draw_ring(fb, cx, cy, r, ui->disp_speed / 100.0f,
		  th->primary, th->bar_bg);
	snprintf(buf, sizeof(buf), "%.0f", ui->disp_speed);
	fb_text(fb, cx - 18, cy - 8, buf, th->text, 2);
	fb_text_cn(fb, cx - 16, cy + r + 7, "Mbps", th->dim, 1);

	format_speed(rx, sizeof(rx), st->rx_speed);
	format_speed(tx, sizeof(tx), st->tx_speed);
	fb_hline(fb, x + 12, row_y - 14, w - 24, th->bar_bg);
	snprintf(buf, sizeof(buf), "下载 %s   上传 %s", rx, tx);
	fb_text_cn(fb, x + 16, row_y, buf, th->text, 1);
	snprintf(buf, sizeof(buf), "WAN %s", st->wan_ip[0] ? st->wan_ip : "--");
	fb_text_cn(fb, x + 16, row_y + 26, buf, th->dim, 1);
	snprintf(buf, sizeof(buf), "LAN %s", st->lan_ip[0] ? st->lan_ip : "--");
	fb_text_cn(fb, x + 16, row_y + 52, buf, th->dim, 1);
	snprintf(buf, sizeof(buf), "在线客户端 %d", st->clients);
	fb_text_cn(fb, x + 16, row_y + 78, buf, th->text, 1);
	if (!strcmp(st->cell_status, "connected"))
		snprintf(buf, sizeof(buf), "%s %s %d dBm",
			 st->cell_op, st->cell_type, st->cell_signal);
	else
		snprintf(buf, sizeof(buf), "蜂窝网络未连接");
	fb_text_cn(fb, x + 16, row_y + 104, buf,
		   !strcmp(st->cell_status, "connected") ? th->accent : th->warn, 1);
}

static void draw_dashboard(fb_t *fb, ui_t *ui, const status_t *st, const theme_t *th)
{
	char buf[96];
	int nav_h = nav_height(fb);
	int nav_y = nav_top(fb);
	int content_y = SAFE_TOP + 48;
	int content_h = nav_y - content_y - 10;
	int landscape = fb->vw > fb->vh;

	fb_fill(fb, th->bg);

	// Top Bar
	fb_rect(fb, SAFE_SIDE, SAFE_TOP, fb->vw - SAFE_SIDE * 2, 38, th->card);
	fb_hline(fb, SAFE_SIDE, SAFE_TOP + 38,
		 fb->vw - SAFE_SIDE * 2, th->primary);
	snprintf(buf, sizeof(buf), "%s", st->hostname[0] ? st->hostname : "NWRT 设备就绪");
	fb_text_cn(fb, SAFE_SIDE + 8, SAFE_TOP + 10, buf, th->text, 1);
	fb_text(fb, fb->vw - 190, SAFE_TOP + 12,
		st->local_time[0] ? st->local_time : "--", th->dim, 1);
	draw_signal_bars(fb, fb->vw - 60, SAFE_TOP + 4, st->cell_signal,
			 !strcmp(st->cell_status, "connected"), th);

	if (landscape) {
		int count = ui->show_system + ui->show_network;
		int gap = 12;
		int x = SAFE_SIDE;
		int w = count > 1 ? (fb->vw - SAFE_SIDE * 3) / 2 :
			fb->vw - SAFE_SIDE * 2;
		ui->max_pages = 1;
		ui->page = 0;
		if (ui->show_system) {
			draw_system_panel(fb, x, content_y, w, content_h, ui, st, th);
			x += w + gap;
		}
		if (ui->show_network)
			draw_network_panel(fb, x, content_y, w, content_h, ui, st, th);
		if (!count)
			fb_text_cn(fb, 24, content_y + 30,
				   "请在 LuCI 中启用显示模块", th->dim, 1);
	} else {
		ui->max_pages = ui->show_system + ui->show_network;
		if (ui->max_pages < 1)
			ui->max_pages = 1;
		if (ui->page >= ui->max_pages)
			ui->page = 0;
		if (ui->show_system && ui->page == 0)
			draw_system_panel(fb, SAFE_SIDE, content_y,
					  fb->vw - SAFE_SIDE * 2,
					  content_h, ui, st, th);
		else if (ui->show_network)
			draw_network_panel(fb, SAFE_SIDE, content_y,
					   fb->vw - SAFE_SIDE * 2,
					   content_h, ui, st, th);
		else
			fb_text_cn(fb, 24, content_y + 30,
				   "请在 LuCI 中启用显示模块", th->dim, 1);
	}

	// Bottom Navigation Bar
	fb_rect(fb, SAFE_SIDE, nav_y, fb->vw - SAFE_SIDE * 2, nav_h, th->card);
	fb_hline(fb, SAFE_SIDE, nav_y, fb->vw - SAFE_SIDE * 2, th->bar_bg);
	if (ui->timeout_sec > 0) {
		int elapsed = (int)((now_ms() - ui->last_touch_ms) / 1000);
		int remaining = ui->timeout_sec - elapsed;
		if (remaining < 0)
			remaining = 0;
		snprintf(buf, sizeof(buf), "状态 %d/%d  息屏 %02d:%02d",
			 ui->page + 1, ui->max_pages,
			 remaining / 60, remaining % 60);
	} else {
		snprintf(buf, sizeof(buf), "状态 %d/%d  屏幕常亮",
			 ui->page + 1, ui->max_pages);
	}
	fb_text_cn(fb, 24, nav_y + 10, buf, th->text, 1);
	fb_text_cn(fb, fb->vw - 100, nav_y + 10, "菜单", th->accent, 1);

	if (ui->show_menu) {
		draw_menu(fb, ui, th);
	}
}

static void draw_menu(fb_t *fb, ui_t *ui, const theme_t *th)
{
	char buf[64];
	int left = 12;
	int width = fb->vw - 24;
	int btn_w = width / 4;
	int menu_h = menu_height(fb);
	int top = menu_top(fb);

	fb_rect(fb, left, top, width, menu_h, th->card);
	fb_hline(fb, left, top, width, th->primary);

	if (ui->menu_page == 0) {
		fb_text_cn(fb, left + 12 + 0 * btn_w, top + 18, "上一页", th->text, 1);
		fb_text_cn(fb, left + 12 + 1 * btn_w, top + 18, "下一页", th->text, 1);
		fb_text_cn(fb, left + 12 + 2 * btn_w, top + 18, "主题", th->text, 1);
		fb_text_cn(fb, left + 12 + 3 * btn_w, top + 18, "关闭", th->text, 1);
	} else {
		fb_text_cn(fb, left + 12 + 0 * btn_w, top + 18, "状态", th->text, 1);
		fb_text_cn(fb, left + 12 + 1 * btn_w, top + 18, "诊断", th->text, 1);
		fb_text_cn(fb, left + 12 + 2 * btn_w, top + 18, "首页", th->text, 1);
		fb_text_cn(fb, left + 12 + 3 * btn_w, top + 18, "返回", th->text, 1);
	}
	snprintf(buf, sizeof(buf), "操作 %d/2  无触摸自动熄屏:%s",
		 ui->menu_page + 1, ui->touch_wake ? "开" : "关");
	fb_text_cn(fb, left + 12, top + 58, buf, th->accent, 1);
	fb_text_cn(fb, left + width - 80, top + 58, "切换", th->dim, 1);
}

static void menu_action(ui_t *ui, int btn)
{
	char cmd[128];

	if (ui->menu_page == 0) {
		switch (btn) {
		case 0:
			ui->page = (ui->page + ui->max_pages - 1) %
				(ui->max_pages > 0 ? ui->max_pages : 1);
			break;
		case 1:
			ui->page = (ui->page + 1) %
				(ui->max_pages > 0 ? ui->max_pages : 1);
			break;
		case 2:
			ui->theme_idx = (ui->theme_idx + 1) % 4;
			snprintf(cmd, sizeof(cmd),
				 "uci set mipiscreen.global.theme='%s'; uci commit mipiscreen",
				 g_theme_names[ui->theme_idx]);
			system(cmd);
			break;
		default:
			ui->show_menu = 0;
			break;
		}
	} else {
		switch (btn) {
		case 0:
			ui->page = 0;
			ui->show_menu = 0;
			break;
		case 1:
			ui->test_pattern = 6;
			ui->test_until_ms = now_ms() + 5000;
			ui->show_menu = 0;
			break;
		case 2:
			ui->page = 0;
			ui->show_menu = 0;
			break;
		case 3:
			ui->show_menu = 0;
			break;
		}
	}
}

static void poll_test_pattern(ui_t *ui)
{
	char buf[32];

	if (read_line_file(TEST_FLAG, buf, sizeof(buf)) < 0)
		return;
	unlink(TEST_FLAG);
	if (!strcmp(buf, "red"))
		ui->test_pattern = 1;
	else if (!strcmp(buf, "green"))
		ui->test_pattern = 2;
	else if (!strcmp(buf, "blue"))
		ui->test_pattern = 3;
	else if (!strcmp(buf, "white"))
		ui->test_pattern = 4;
	else if (!strcmp(buf, "gray"))
		ui->test_pattern = 5;
	else if (!strcmp(buf, "grid"))
		ui->test_pattern = 6;
	else
		ui->test_pattern = 0;
	ui->test_until_ms = now_ms() + 5000;
}

static void draw_test_pattern(fb_t *fb, int pattern)
{
	static const uint32_t colors[] = {
		0xFF000000, 0xFFFF0000, 0xFF00FF00,
		0xFF0000FF, 0xFFFFFFFF, 0xFF808080
	};
	int x, y;

	if (pattern >= 0 && pattern <= 5) {
		fb_fill(fb, colors[pattern]);
		return;
	}

	fb_fill(fb, 0xFF101010);
	for (x = 0; x < fb->vw; x += 40)
		for (y = 0; y < fb->vh; y++)
			fb_put_pixel(fb, x, y, 0xFFFFFFFF);
	for (y = 0; y < fb->vh; y += 40)
		fb_hline(fb, 0, y, fb->vw, 0xFFFFFFFF);
}

static void touch_query_bounds(touch_t *t)
{
	struct input_absinfo absinfo;
	t->max_x = 4095;
	t->max_y = 4095;

	if (t->fd >= 0) {
		// First try multi-touch absolute coordinates
		if (ioctl(t->fd, EVIOCGABS(ABS_MT_POSITION_X), &absinfo) >= 0) {
			if (absinfo.maximum > 0)
				t->max_x = absinfo.maximum;
		} else if (ioctl(t->fd, EVIOCGABS(ABS_X), &absinfo) >= 0) {
			if (absinfo.maximum > 0)
				t->max_x = absinfo.maximum;
		}

		if (ioctl(t->fd, EVIOCGABS(ABS_MT_POSITION_Y), &absinfo) >= 0) {
			if (absinfo.maximum > 0)
				t->max_y = absinfo.maximum;
		} else if (ioctl(t->fd, EVIOCGABS(ABS_Y), &absinfo) >= 0) {
			if (absinfo.maximum > 0)
				t->max_y = absinfo.maximum;
		}
	}
}

static int touch_open(touch_t *t)
{
	char path[256];
	DIR *d = opendir("/sys/class/input");
	struct dirent *e;

	memset(t, 0, sizeof(*t));
	t->fd = -1;
	t->max_x = 4095;
	t->max_y = 4095;
	if (!d)
		return -1;
	while ((e = readdir(d))) {
		char name[64];
		FILE *f;
		if (strncmp(e->d_name, "input", 5) != 0)
			continue;
		snprintf(path, sizeof(path), "/sys/class/input/%s/name", e->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (!fgets(name, sizeof(name), f)) {
			fclose(f);
			continue;
		}
		fclose(f);
		if (strstr(name, "ts") || strstr(name, "Touch") ||
		    strstr(name, "touch") || strstr(name, "FTS") ||
		    strstr(name, "fts") || strstr(name, "ft5") ||
		    strstr(name, "focal") ||
		    strstr(name, "goodix") || strstr(name, "EDT")) {
			char evdir[256];
			DIR *ed;
			snprintf(evdir, sizeof(evdir), "/sys/class/input/%s", e->d_name);
			ed = opendir(evdir);
			if (!ed)
				continue;
			while ((e = readdir(ed))) {
				if (strncmp(e->d_name, "event", 5) != 0)
					continue;
				snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
				t->fd = open(path, O_RDONLY | O_NONBLOCK);
				if (t->fd >= 0) {
					name[strcspn(name, "\r\n")] = 0;
					snprintf(t->name, sizeof(t->name), "%s", name);
					touch_query_bounds(t);
					closedir(ed);
					closedir(d);
					return 0;
				}
			}
			closedir(ed);
		}
	}
	closedir(d);
	return -1;
}

static void touch_handle(ui_t *ui, int vx, int vy, fb_t *fb)
{
	int menu_h = menu_height(fb);
	int menu_y = menu_top(fb);
	int nav_y = nav_top(fb);

	if (ui->show_menu) {
		if (vy < menu_y || vy >= menu_y + menu_h ||
		    vx < 12 || vx >= fb->vw - 12) {
			ui->show_menu = 0;
			return;
		}
		if (vy < menu_y + 50) {
			int btn = (vx - 12) / ((fb->vw - 24) / 4);
			menu_action(ui, btn);
		} else {
			ui->menu_page = (ui->menu_page + 1) % 2;
		}
	} else {
		if (vy >= nav_y && vy < nav_y + nav_height(fb)) {
			if (vx >= fb->vw - 140)
				ui->show_menu = 1;
			else if (ui->max_pages > 1)
				ui->page = (ui->page + 1) % ui->max_pages;
		}
	}
}

static void touch_poll(touch_t *t, ui_t *ui, fb_t *fb)
{
	struct input_event ev;
	ssize_t n;

	if (t->fd < 0)
		return;
	while ((n = read(t->fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
		if (ev.type == EV_ABS) {
			if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
				t->x = ev.value * fb->w / (t->max_x ? t->max_x : 4095);
			}
			if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
				t->y = ev.value * fb->h / (t->max_y ? t->max_y : 4095);
			}
		} else if (ev.type == EV_KEY &&
			   (ev.code == BTN_TOUCH || ev.code == KEY_POWER || ev.code == KEY_ENTER)) {
			t->down = ev.value;
		} else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
			// Trigger only on Tap transitions (Up -> Down) to prevent drag double-triggers
			if (t->down && !t->last_down) {
				ui->last_touch_ms = now_ms();
				if (ui->blanked) {
					if (ui->touch_wake)
						display_wake(ui);
				} else {
					int vx, vy;
					get_virt_coordinates(fb, t->x, t->y, &vx, &vy);
					touch_handle(ui, vx, vy, fb);
				}
			}
			t->last_down = t->down;
		}
	}
}

static void apply_rotation(fb_t *fb, int rotation)
{
	fb->rotation = rotation;
	if (rotation == 90 || rotation == 270) {
		fb->vw = fb->h;
		fb->vh = fb->w;
	} else {
		fb->vw = fb->w;
		fb->vh = fb->h;
	}
}

int main(int argc, char **argv)
{
	fb_t fb;
	touch_t touch;
	status_t status;
	ui_t ui;
	uint64_t last_status_ms = 0;
	uint64_t last_touch_retry_ms = 0;

	(void)argc;
	(void)argv;
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	memset(&ui, 0, sizeof(ui));
	memset(&status, 0, sizeof(status));
	memset(&touch, 0, sizeof(touch));
	touch.fd = -1;
	ui.boot_start_ms = now_ms();
	ui.last_touch_ms = ui.boot_start_ms;
	ui.last_uci_ms = ui.boot_start_ms;
	ui.mode = UI_BOOT;
	load_uci(&ui);

	if (fb_wait_ready() < 0) {
		fprintf(stderr, "mipiscreen-ui: timeout waiting for /dev/fb0\n");
		return 1;
	}
	if (fb_open(&fb) < 0) {
		fprintf(stderr, "mipiscreen-ui: open fb0 failed: %s\n", strerror(errno));
		return 1;
	}
	apply_rotation(&fb, ui.rotation);
	if (ui.enabled && ui.backlight)
		display_wake(&ui);
	else
		display_sleep(&ui);
	touch_open(&touch);
	last_touch_retry_ms = now_ms();

	while (g_running) {
		uint64_t t = now_ms();
		int frame_ms = (ui.mode == UI_BOOT) ? (1000 / FPS_BOOT) : (1000 / FPS_DASH);
		const theme_t *th = ui_theme(&ui);

		if (access(RELOAD_FLAG, F_OK) == 0) {
			unlink(RELOAD_FLAG);
			load_uci(&ui);
			apply_rotation(&fb, ui.rotation);
			if (ui.enabled && ui.backlight)
				display_wake(&ui);
			else
				display_sleep(&ui);
			ui.last_touch_ms = t;
		} else if (t - ui.last_uci_ms > 2000) {
			load_uci(&ui);
			apply_rotation(&fb, ui.rotation);
			if (!ui.enabled || !ui.backlight)
				display_sleep(&ui);
			ui.last_uci_ms = t;
		}

		if (touch.fd < 0 && t - last_touch_retry_ms > TOUCH_RETRY_MS) {
			touch_open(&touch);
			last_touch_retry_ms = t;
		}
		touch_poll(&touch, &ui, &fb);
		poll_test_pattern(&ui);
		if (t - last_status_ms > 500) {
			status_update(&status);
			last_status_ms = t;
		}

		ui.boot_target = boot_stage_progress();
		ui.boot_progress = lerp(ui.boot_progress, ui.boot_target, 0.08f);

		if (ui.timeout_sec > 0 && !ui.blanked &&
		    t - ui.last_touch_ms > (uint64_t)ui.timeout_sec * 1000)
			display_sleep(&ui);

		if (ui.blanked) {
			usleep(100000);
			continue;
		}

		if (ui.test_pattern && t < ui.test_until_ms) {
			draw_test_pattern(&fb, ui.test_pattern);
		} else if (ui.test_pattern) {
			ui.test_pattern = 0;
		} else if (ui.mode == UI_BOOT) {
			draw_boot(&fb, &ui, th);
			if ((ui.boot_progress > 0.98f && status.ready) ||
			    (t - ui.boot_start_ms > 12000) ||
			    (!ui.boot_anim && t - ui.boot_start_ms > 1500))
				ui.mode = UI_DASH;
		} else {
			float speed_mbps = (status.rx_speed * 8.0f) / 1000000.0f;
			if (speed_mbps > 100.0f)
				speed_mbps = 100.0f;
			ui.disp_cpu = lerp(ui.disp_cpu, status.cpu_usage, 0.18f);
			ui.disp_mem = lerp(ui.disp_mem, status.mem_usage, 0.18f);
			ui.disp_speed = lerp(ui.disp_speed, speed_mbps, 0.22f);
			draw_dashboard(&fb, &ui, &status, th);
		}

		fb_present(&fb);
		usleep(frame_ms * 1000);
	}

	fb_close(&fb);
	if (touch.fd >= 0)
		close(touch.fd);
	return 0;
}
