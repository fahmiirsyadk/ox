#define _GNU_SOURCE
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
    int width, height, padding;
    const char *fg, *bg;
} VStack;

static void vstack_add(VStack *v, OxWidget *w) { v->widgets[v->count++] = w; }

static void vstack_render(VStack *v) {
    ox_draw_rect(v->win, 0, 0, v->width, v->height, v->bg);
    int cy = v->padding;
    for (int i = 0; i < v->count; i++) {
        const char *icon = ox_widget_get_icon(v->widgets[i]);
        const char *label = ox_widget_get_label(v->widgets[i]);
        int th = ox_text_width(v->win, "X") + 4;
        if (icon) {
            ox_draw_text(v->win, v->padding, cy + th, icon, v->fg);
            cy += th;
        }
        ox_draw_text(v->win, v->padding, cy + th, label, v->fg);
        cy += th + v->padding;
    }
}

static void time_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(buf, len, "%02d:%02d", tm->tm_hour, tm->tm_min);
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
    static unsigned long long pt = 0, pi = 0;
    unsigned long long dt = total - pt, di = idle - pi;
    pt = total; pi = idle;
    int pct = (dt > 0) ? (int)((dt - di) * 100 / dt) : 0;
    snprintf(buf, len, "%d%%", pct);
}

static void mem_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { snprintf(buf, len, "??"); return; }
    long total = 0, avail = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %ld kB", &total) == 1) continue;
        if (sscanf(line, "MemAvailable: %ld kB", &avail) == 1) break;
    }
    fclose(f);
    snprintf(buf, len, "%ldM", (total - avail) / 1024);
}

static void load_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    double load[1];
    getloadavg(load, 1);
    snprintf(buf, len, "%.1f", load[0]);
}

static void on_timeout(OxMain *m, double now) {
    VStack *v = m->ctx;
    for (int i = 0; i < v->count; i++) {
        OxWidget *w = v->widgets[i];
        if (ox_widget_get_interval(w) > 0 &&
            now - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
            ox_widget_update(w);
            ox_widget_set_last_update(w, now);
        }
    }
    vstack_render(v);
}

static void on_event(OxMain *m, XEvent *ev) {
    if (ev->type == Expose) vstack_render(m->ctx);
}

int main(void) {
    ox_init();
    int sh = DisplayHeight(ox_display(), ox_screen());
    int width = 60, height = sh, pad = 8;

    VStack stack = { .width = width, .height = height, .padding = pad,
                     .fg = "#ffffff", .bg = "#111111" };
    stack.win = ox_window_new(0, 0, width, height);
    ox_window_set_bg(stack.win, stack.bg);
    ox_window_set_font(stack.win, "monospace:size=10");
    ox_window_show(stack.win);

    vstack_add(&stack, ox_widget_new("time", 1.0));
    ox_widget_set_icon(stack.widgets[0], "T");
    ox_widget_set_update(stack.widgets[0], time_update, NULL);

    vstack_add(&stack, ox_widget_new("cpu", 2.0));
    ox_widget_set_icon(stack.widgets[1], "C");
    ox_widget_set_update(stack.widgets[1], cpu_update, NULL);

    vstack_add(&stack, ox_widget_new("mem", 2.0));
    ox_widget_set_icon(stack.widgets[2], "M");
    ox_widget_set_update(stack.widgets[2], mem_update, NULL);

    vstack_add(&stack, ox_widget_new("load", 1.0));
    ox_widget_set_icon(stack.widgets[3], "L");
    ox_widget_set_update(stack.widgets[3], load_update, NULL);

    for (int i = 0; i < stack.count; i++) ox_widget_update(stack.widgets[i]);
    vstack_render(&stack);

    OxMain loop = { .on_event = on_event, .on_timeout = on_timeout, .ctx = &stack, .timeout_ms = 100 };
    ox_main(&loop);
    ox_cleanup();
    return 0;
}
