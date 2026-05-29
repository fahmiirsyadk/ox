#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ox.h"

typedef struct {
    OxWindow *win;
    OxWidget *widgets[16];
    int count;
    int height, padding;
    const char *fg, *bg, *sep;
} Bar;

static void bar_add(Bar *b, OxWidget *w) { b->widgets[b->count++] = w; }

static void bar_render(Bar *b) {
    int sw = DisplayWidth(ox_display(), ox_screen());
    int cx = b->padding;
    int cy = b->height / 2 + ox_text_width(b->win, "X") / 3;
    ox_draw_rect(b->win, 0, 0, sw, b->height, b->bg);
    for (int i = 0; i < b->count; i++) {
        if (i > 0) {
            ox_draw_text(b->win, cx, cy, " | ", b->sep);
            cx += ox_text_width(b->win, " | ") + b->padding;
        }
        const char *icon = ox_widget_get_icon(b->widgets[i]);
        const char *label = ox_widget_get_label(b->widgets[i]);
        if (icon) {
            ox_draw_text(b->win, cx, cy, icon, b->fg);
            cx += ox_text_width(b->win, icon);
        }
        ox_draw_text(b->win, cx, cy, label, b->fg);
        cx += ox_text_width(b->win, label) + b->padding;
    }
}

static void time_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(buf, len, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static void cpu_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/proc/stat", "r");
    if (!f) { snprintf(buf, len, "??%%"); return; }
    char line[256];
    fgets(line, sizeof(line), f);
    fclose(f);
    unsigned long long u, n, s, i, w, q, sq;
    sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &i, &w, &q, &sq);
    unsigned long long total = u + n + s + i + w + q + sq;
    unsigned long long idle = i;
    static unsigned long long prev_total = 0, prev_idle = 0;
    unsigned long long dt_total = total - prev_total;
    unsigned long long dt_idle = idle - prev_idle;
    prev_total = total;
    prev_idle = idle;
    int pct = (dt_total > 0) ? (int)((dt_total - dt_idle) * 100 / dt_total) : 0;
    snprintf(buf, len, "%d%%", pct);
}

static void mem_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { snprintf(buf, len, "??MB"); return; }
    long total = 0, avail = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %ld kB", &total) == 1) continue;
        if (sscanf(line, "MemAvailable: %ld kB", &avail) == 1) break;
    }
    fclose(f);
    snprintf(buf, len, "%ldMB", (total - avail) / 1024);
}

static void on_timeout(OxMain *m, double now) {
    Bar *b = m->ctx;
    int any_dirty = 0;
    for (int i = 0; i < b->count; i++) {
        OxWidget *w = b->widgets[i];
        if (ox_widget_get_interval(w) > 0 &&
            now - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
            ox_widget_update(w);
            ox_widget_set_last_update(w, now);
        }
        if (ox_widget_is_dirty(w)) any_dirty = 1;
    }
    if (any_dirty) {
        bar_render(b);
        for (int i = 0; i < b->count; i++)
            ox_widget_clear_dirty(b->widgets[i]);
    }
}

static void on_event(OxMain *m, XEvent *ev) {
    if (ev->type == Expose) bar_render(m->ctx);
}

int main(void) {
    ox_init();
    int sw = DisplayWidth(ox_display(), ox_screen());

    Bar bar = { .height = 24, .padding = 8, .fg = "#ffffff", .bg = "#000000", .sep = "#555555" };
    bar.win = ox_window_new(0, 0, sw, bar.height);
    ox_window_set_bg(bar.win, bar.bg);
    ox_window_set_font(bar.win, "monospace:size=11");
    ox_window_show(bar.win);

    bar_add(&bar, ox_widget_new("time", 1.0));
    ox_widget_set_icon(bar.widgets[0], "TIME");
    ox_widget_set_update(bar.widgets[0], time_update, NULL);

    bar_add(&bar, ox_widget_new("cpu", 2.0));
    ox_widget_set_icon(bar.widgets[1], "CPU");
    ox_widget_set_update(bar.widgets[1], cpu_update, NULL);

    bar_add(&bar, ox_widget_new("mem", 2.0));
    ox_widget_set_icon(bar.widgets[2], "MEM");
    ox_widget_set_update(bar.widgets[2], mem_update, NULL);

    for (int i = 0; i < bar.count; i++) ox_widget_update(bar.widgets[i]);
    bar_render(&bar);

    OxMain loop = { .on_event = on_event, .on_timeout = on_timeout, .ctx = &bar, .timeout_ms = 100 };
    ox_main(&loop);
    ox_cleanup();
    return 0;
}
