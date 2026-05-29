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

typedef struct {
    Bar left, center, right;
    OxWidget *all[6];
    int nw;
} MultiState;

static void bar_add(Bar *b, OxWidget *w) { b->widgets[b->count++] = w; }

static int bar_content_width(Bar *b) {
    int total = b->padding;
    for (int i = 0; i < b->count; i++) {
        if (i > 0) total += ox_text_width(b->win, " | ") + b->padding;
        const char *icon = ox_widget_get_icon(b->widgets[i]);
        const char *label = ox_widget_get_label(b->widgets[i]);
        if (icon) total += ox_text_width(b->win, icon);
        total += ox_text_width(b->win, label) + b->padding;
    }
    return total;
}

static void bar_render(Bar *b) {
    int cy = b->height / 2 + ox_text_width(b->win, "X") / 3;
    int w = bar_content_width(b);
    ox_draw_rect(b->win, 0, 0, w, b->height, b->bg);
    int cx = b->padding;
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

static void date_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    const char *wdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char *mons[] = {"Jan","Feb","Mar","Apr","May","Jun",
                          "Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(buf, len, "%s %s %d", wdays[tm->tm_wday], mons[tm->tm_mon], tm->tm_mday);
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

static void vol_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+%' | head -1 | grep -oP '\\d+'", "r");
    if (!f) { snprintf(buf, len, "??%%"); return; }
    if (fgets(buf, len, f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    } else snprintf(buf, len, "??%%");
    pclose(f);
}

static void bright_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/sys/class/backlight/backlight/brightness", "r");
    if (!f) { snprintf(buf, len, "??%%"); return; }
    long cur = 0, max = 15360;
    fscanf(f, "%ld", &cur);
    fclose(f);
    snprintf(buf, len, "%ld%%", cur * 100 / max);
}

static void on_timeout(OxMain *m, double now) {
    MultiState *s = m->ctx;
    for (int i = 0; i < s->nw; i++) {
        OxWidget *w = s->all[i];
        if (ox_widget_get_interval(w) > 0 &&
            now - ox_widget_get_last_update(w) >= ox_widget_get_interval(w)) {
            ox_widget_update(w);
            ox_widget_set_last_update(w, now);
        }
    }
    bar_render(&s->left);
    bar_render(&s->center);
    bar_render(&s->right);
}

static void on_event(OxMain *m, XEvent *ev) {
    (void)ev;
    MultiState *s = m->ctx;
    bar_render(&s->left);
    bar_render(&s->center);
    bar_render(&s->right);
}

int main(void) {
    ox_init();
    int sw = DisplayWidth(ox_display(), ox_screen());
    int h = 24, pad = 8;
    const char *font = "monospace:size=11";

    MultiState state = {0};
    MultiState *s = &state;

    s->left = (Bar){ .height = h, .padding = pad, .fg = "#ffffff", .bg = "#000000", .sep = "#555555" };
    bar_add(&s->left, ox_widget_new("time", 1.0));
    ox_widget_set_icon(s->left.widgets[0], "TIME");
    ox_widget_set_update(s->left.widgets[0], time_update, NULL);
    bar_add(&s->left, ox_widget_new("cpu", 2.0));
    ox_widget_set_icon(s->left.widgets[1], "CPU");
    ox_widget_set_update(s->left.widgets[1], cpu_update, NULL);
    bar_add(&s->left, ox_widget_new("mem", 2.0));
    ox_widget_set_icon(s->left.widgets[2], "MEM");
    ox_widget_set_update(s->left.widgets[2], mem_update, NULL);

    s->center = (Bar){ .height = h, .padding = pad, .fg = "#ffffff", .bg = "#000000", .sep = "#555555" };
    bar_add(&s->center, ox_widget_new("date", 60.0));
    ox_widget_set_icon(s->center.widgets[0], "DATE");
    ox_widget_set_update(s->center.widgets[0], date_update, NULL);

    s->right = (Bar){ .height = h, .padding = pad, .fg = "#ffffff", .bg = "#000000", .sep = "#555555" };
    bar_add(&s->right, ox_widget_new("vol", 2.0));
    ox_widget_set_icon(s->right.widgets[0], "VOL");
    ox_widget_set_update(s->right.widgets[0], vol_update, NULL);
    bar_add(&s->right, ox_widget_new("bright", 5.0));
    ox_widget_set_icon(s->right.widgets[1], "LIT");
    ox_widget_set_update(s->right.widgets[1], bright_update, NULL);

    s->all[0] = s->left.widgets[0];
    s->all[1] = s->left.widgets[1];
    s->all[2] = s->left.widgets[2];
    s->all[3] = s->center.widgets[0];
    s->all[4] = s->right.widgets[0];
    s->all[5] = s->right.widgets[1];
    s->nw = 6;

    for (int i = 0; i < s->nw; i++) ox_widget_update(s->all[i]);

    OxWindow *tmp = ox_window_new(-100, -100, 1, 1);
    ox_window_set_font(tmp, font);
    int lw = pad;
    for (int i = 0; i < s->left.count; i++) {
        if (i > 0) lw += ox_text_width(tmp, " | ") + pad;
        const char *icon = ox_widget_get_icon(s->left.widgets[i]);
        const char *label = ox_widget_get_label(s->left.widgets[i]);
        if (icon) lw += ox_text_width(tmp, icon);
        lw += ox_text_width(tmp, label) + pad;
    }
    int cw = pad;
    for (int i = 0; i < s->center.count; i++) {
        if (i > 0) cw += ox_text_width(tmp, " | ") + pad;
        const char *icon = ox_widget_get_icon(s->center.widgets[i]);
        const char *label = ox_widget_get_label(s->center.widgets[i]);
        if (icon) cw += ox_text_width(tmp, icon);
        cw += ox_text_width(tmp, label) + pad;
    }
    int rw = pad;
    for (int i = 0; i < s->right.count; i++) {
        if (i > 0) rw += ox_text_width(tmp, " | ") + pad;
        const char *icon = ox_widget_get_icon(s->right.widgets[i]);
        const char *label = ox_widget_get_label(s->right.widgets[i]);
        if (icon) rw += ox_text_width(tmp, icon);
        rw += ox_text_width(tmp, label) + pad;
    }
    ox_window_destroy(tmp);

    s->left.win = ox_window_new(0, 0, lw, h);
    ox_window_set_bg(s->left.win, s->left.bg);
    ox_window_set_font(s->left.win, font);
    ox_window_set_strut(s->left.win, h, 0, 0, 0);

    s->center.win = ox_window_new((sw - cw) / 2, 0, cw, h);
    ox_window_set_bg(s->center.win, s->center.bg);
    ox_window_set_font(s->center.win, font);

    s->right.win = ox_window_new(sw - rw, 0, rw, h);
    ox_window_set_bg(s->right.win, s->right.bg);
    ox_window_set_font(s->right.win, font);

    ox_window_show(s->left.win);
    ox_window_show(s->center.win);
    ox_window_show(s->right.win);

    bar_render(&s->left);
    bar_render(&s->center);
    bar_render(&s->right);

    OxMain loop = { .on_event = on_event, .on_timeout = on_timeout, .ctx = s, .timeout_ms = 100 };
    ox_main(&loop);
    ox_cleanup();
    return 0;
}
