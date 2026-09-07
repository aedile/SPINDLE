/*
 * render.cpp - rasterize the AVG's vector list onto the 240x280 panel from a dedicated task.
 *
 * Tempest's monitor is mounted a quarter turn round (MAME calls it ROT270), which makes the
 * picture portrait - 570 wide by 580 tall - and that is very nearly the shape of the medal's
 * panel. Scaled to the full 240 columns it comes out 244 rows tall, so there are eighteen
 * blank rows above and below and nothing has to be cropped.
 *
 * The colour is the interesting part. Asteroids and Star Wars only vary the beam's brightness,
 * so their frame buffer can be one byte of intensity per pixel. Tempest has colour, and a full
 * RGB565 buffer at this size is 134 KB - more than this board can spare next to the emulator.
 * So a pixel is still one byte: three bits of hue and five of brightness. Where two vectors of
 * the same hue cross, the brightnesses add, which is most of what makes a vector picture look
 * like one; where different hues cross, the brighter wins. The real phosphor would mix them,
 * but Tempest almost never crosses colours and 67 KB against 134 KB is the whole argument.
 */
#include "render.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "RENDER";

#define FB_W DISPLAY_WIDTH                        /* 240 */
#define FB_H DISPLAY_HEIGHT                       /* 280 */
#define ROWS_PER_CHUNK 14
#define NUM_LISTS 2
#define MAX_SEGS 1400

/* the rotated picture, scaled to the full width of the panel */
#define PIC_W FB_W
#define PIC_H (FB_W * AVG_XMAX / AVG_YMAX)        /* 244 */
#define TOP_BAR ((FB_H - PIC_H) / 2)              /* 18 blank rows above and below */

#define BRIGHT_BITS 5
#define BRIGHT_MAX ((1 << BRIGHT_BITS) - 1)       /* 31 */

typedef struct { int16_t x0, y0, x1, y1; uint8_t hue, b; } seg_t;
typedef struct { seg_t s[MAX_SEGS]; int n; } vlist_t;

static vlist_t *lists[NUM_LISTS];
static QueueHandle_t free_q, frame_q;
static uint8_t *fb;                               /* FB_W * FB_H, hue<<5 | brightness */
static uint16_t *chunk;
static uint16_t palette[256];
static uint32_t frames_drawn, frames_dropped;
static uint64_t busy_us;

/* the eight hues the colour PROM can produce, at full brightness */
static const uint8_t hue_rgb[8][3] = {
    { 0, 0, 0 }, { 0, 0, 255 }, { 0, 255, 0 }, { 0, 255, 255 },
    { 255, 0, 0 }, { 255, 0, 255 }, { 255, 255, 0 }, { 255, 255, 255 },
};

static void build_palette(void)
{
    for (int h = 0; h < 8; h++) {
        for (int v = 0; v <= BRIGHT_MAX; v++) {
            unsigned r = hue_rgb[h][0] * v / BRIGHT_MAX;
            unsigned g = hue_rgb[h][1] * v / BRIGHT_MAX;
            unsigned b = hue_rgb[h][2] * v / BRIGHT_MAX;
            uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            palette[(h << BRIGHT_BITS) | v] = (uint16_t)((c >> 8) | (c << 8));  /* big-endian panel */
        }
    }
    for (int h = 0; h < 8; h++) palette[h << BRIGHT_BITS] = 0;
}

/* beam space (already rotated by the caller) onto the letterboxed picture */
static inline void beam_to_panel(int bx, int by, int *px, int *py)
{
    *px = bx * (PIC_W - 1) / AVG_YMAX;
    *py = TOP_BAR + by * (PIC_H - 1) / AVG_XMAX;
}

static inline void add_px(int x, int y, int hue, int b)
{
    if ((unsigned)x >= FB_W || (unsigned)y >= FB_H || b <= 0) return;
    uint8_t *p = &fb[y * FB_W + x];
    int old_h = *p >> BRIGHT_BITS, old_b = *p & BRIGHT_MAX;
    if (old_b == 0)      { *p = (uint8_t)((hue << BRIGHT_BITS) | (b > BRIGHT_MAX ? BRIGHT_MAX : b)); }
    else if (old_h == hue) { int n = old_b + b; *p = (uint8_t)((hue << BRIGHT_BITS) | (n > BRIGHT_MAX ? BRIGHT_MAX : n)); }
    else if (b > old_b)  { *p = (uint8_t)((hue << BRIGHT_BITS) | (b > BRIGHT_MAX ? BRIGHT_MAX : b)); }
}

/*
 * An anti-aliased stroke. A hard one-pixel Bresenham line at this size shreds the small text
 * Tempest puts on the screen, so each step is spread across two pixels by its fractional part.
 */
static void line(int x0, int y0, int x1, int y1, int hue, int b)
{
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx == 0 && ady == 0) { add_px(x0, y0, hue, b); return; }
    if (adx >= ady) {
        if (x0 > x1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dy = -dy; }
        int32_t y = (int32_t)y0 << 16, step = adx ? ((int32_t)dy << 16) / adx : 0;
        for (int x = x0; x <= x1; x++, y += step) {
            int yi = (int)(y >> 16), f = (int)((y >> 8) & 0xff);
            add_px(x, yi, hue, (b * (256 - f)) >> 8);
            add_px(x, yi + 1, hue, (b * f) >> 8);
        }
    } else {
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; dx = -dx; }
        int32_t x = (int32_t)x0 << 16, step = ady ? ((int32_t)dx << 16) / ady : 0;
        for (int y = y0; y <= y1; y++, x += step) {
            int xi = (int)(x >> 16), f = (int)((x >> 8) & 0xff);
            add_px(xi, y, hue, (b * (256 - f)) >> 8);
            add_px(xi + 1, y, hue, (b * f) >> 8);
        }
    }
}

static void rasterize(const vlist_t *l)
{
    memset(fb, 0, FB_W * FB_H);
    for (int i = 0; i < l->n; i++) {
        const seg_t *s = &l->s[i];
        int x0, y0, x1, y1;
        beam_to_panel(s->x0, s->y0, &x0, &y0);
        beam_to_panel(s->x1, s->y1, &x1, &y1);
        line(x0, y0, x1, y1, s->hue, s->b);
    }
}

static void present(void)
{
    display_set_window(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (int row = 0; row < DISPLAY_HEIGHT; row += ROWS_PER_CHUNK) {
        int rows = (row + ROWS_PER_CHUNK <= DISPLAY_HEIGHT) ? ROWS_PER_CHUNK : (DISPLAY_HEIGHT - row);
        const uint8_t *src = fb + row * FB_W;
        uint16_t *dst = chunk;
        for (int i = 0; i < rows * FB_W; i++) *dst++ = palette[*src++];
        display_write_preswapped(chunk, rows * DISPLAY_WIDTH);
    }
    display_wait_done();
}

static void render_task(void *arg)
{
    (void)arg;
    for (;;) {
        vlist_t *l;
        if (xQueueReceive(frame_q, &l, portMAX_DELAY) != pdTRUE) continue;
        int64_t t0 = esp_timer_get_time();
        rasterize(l);
        present();
        busy_us += esp_timer_get_time() - t0;
        xQueueSend(free_q, &l, 0);
        frames_drawn++;
    }
}

void render_init(void)
{
    build_palette();
    fb = (uint8_t *)heap_caps_malloc(FB_W * FB_H, MALLOC_CAP_8BIT);
    chunk = (uint16_t *)heap_caps_malloc(ROWS_PER_CHUNK * DISPLAY_WIDTH * sizeof(uint16_t), MALLOC_CAP_8BIT);
    free_q = xQueueCreate(NUM_LISTS, sizeof(vlist_t *));
    frame_q = xQueueCreate(NUM_LISTS, sizeof(vlist_t *));
    for (int i = 0; i < NUM_LISTS; i++) {
        lists[i] = (vlist_t *)heap_caps_malloc(sizeof(vlist_t), MALLOC_CAP_8BIT);
        if (!lists[i]) { ESP_LOGE(TAG, "vector list allocation failed"); abort(); }
        xQueueSend(free_q, &lists[i], 0);
    }
    if (!fb || !chunk) { ESP_LOGE(TAG, "frame buffer allocation failed"); abort(); }
    xTaskCreate(render_task, "render", 4096, nullptr, 6, nullptr);
    ESP_LOGI(TAG, "render task started (%dx%d picture, %d-row bars)", PIC_W, PIC_H, TOP_BAR);
}

bool render_submit(const avg_point_t *pts, int n)
{
    vlist_t *l;
    if (xQueueReceive(free_q, &l, 0) != pdTRUE) { frames_dropped++; return false; }

    int out = 0, px = 0, py = 0;
    for (int i = 0; i < n && out < MAX_SEGS; i++) {
        /* the quarter turn the monitor is mounted at; see the file comment */
        int x = (int)(pts[i].y >> 16);
        int y = (int)(pts[i].x >> 16);
        if (i && pts[i].intensity) {
            uint8_t hue = (uint8_t)(((pts[i].r >= 0x80) << 2) | ((pts[i].g >= 0x80) << 1) | (pts[i].b >= 0x80));
            if (hue) {
                seg_t *s = &l->s[out++];
                s->x0 = (int16_t)px; s->y0 = (int16_t)py;
                s->x1 = (int16_t)x;  s->y1 = (int16_t)y;
                s->hue = hue;
                int b = 6 + (pts[i].intensity * BRIGHT_MAX) / 255;
                s->b = (uint8_t)(b > BRIGHT_MAX ? BRIGHT_MAX : b);
            }
        }
        px = x; py = y;
    }
    l->n = out;
    xQueueSend(frame_q, &l, 0);
    return true;
}

uint32_t render_frames_drawn(void) { uint32_t v = frames_drawn; frames_drawn = 0; return v; }
uint32_t render_frames_dropped(void) { uint32_t v = frames_dropped; frames_dropped = 0; return v; }
uint64_t render_busy_us(void) { uint64_t v = busy_us; busy_us = 0; return v; }
