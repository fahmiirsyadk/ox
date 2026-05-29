#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "ox.h"

typedef struct {
    OxWindow *win;
    OxWidget *widget;
    int shown;
    time_t hide_at;
    int timeout;
} OSD;

typedef struct {
    OSD vol_osd;
    OSD bright_osd;
    volatile int show_vol;
    volatile int show_bright;
} OSDState;

static void sig_handler(int sig) {
    /* We can't modify globals from signal in ox_main context easily,
       but the signal interrupts select() and we check in on_timeout */
    (void)sig;
}

static void osd_show(OSD *osd) {
    time_t now = time(NULL);
    if (osd->shown && (now - (osd->hide_at - osd->timeout)) < 1) return;
    if (!osd->shown) ox_window_show(osd->win);
    osd->shown = 1;
    osd->hide_at = now + osd->timeout;
    ox_widget_update(osd->widget);
}

static void osd_hide(OSD *osd) {
    if (osd->shown) { ox_window_hide(osd->win); osd->shown = 0; }
}

static void osd_render(OSD *osd) {
    if (!osd->shown) return;
    int w = 30, h = 200;
    ox_draw_rect(osd->win, 0, 0, w, h, "#111111");
    const char *label = ox_widget_get_label(osd->widget);
    int pct = atoi(label);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int fill = (h - 4) * pct / 100;
    ox_draw_rect(osd->win, 2, h - 2 - fill, w - 4, fill, "#4488ff");
}

static void vol_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = popen("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -oP '\\d+%' | head -1 | grep -oP '\\d+'", "r");
    if (!f) { snprintf(buf, len, "0"); return; }
    if (fgets(buf, len, f)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    } else snprintf(buf, len, "0");
    pclose(f);
}

static void bright_update(void *ctx, char *buf, size_t len) {
    (void)ctx;
    FILE *f = fopen("/sys/class/backlight/backlight/brightness", "r");
    if (!f) { snprintf(buf, len, "0"); return; }
    long cur = 0, max = 15360;
    fscanf(f, "%ld", &cur);
    fclose(f);
    snprintf(buf, len, "%ld", cur * 100 / max);
}

static void on_timeout(OxMain *m, double now) {
    OSDState *s = m->ctx;
    if (s->show_vol) { s->show_vol = 0; osd_show(&s->vol_osd); }
    if (s->show_bright) { s->show_bright = 0; osd_show(&s->bright_osd); }

    time_t t = time(NULL);
    if (s->vol_osd.shown && t >= s->vol_osd.hide_at) osd_hide(&s->vol_osd);
    if (s->bright_osd.shown && t >= s->bright_osd.hide_at) osd_hide(&s->bright_osd);

    if (s->vol_osd.shown && now - ox_widget_get_last_update(s->vol_osd.widget) >= 0.1) {
        ox_widget_update(s->vol_osd.widget);
        ox_widget_set_last_update(s->vol_osd.widget, now);
    }
    if (s->bright_osd.shown && now - ox_widget_get_last_update(s->bright_osd.widget) >= 0.1) {
        ox_widget_update(s->bright_osd.widget);
        ox_widget_set_last_update(s->bright_osd.widget, now);
    }

    osd_render(&s->vol_osd);
    osd_render(&s->bright_osd);
}

static void on_event(OxMain *m, XEvent *ev) {
    (void)ev;
    OSDState *s = m->ctx;
    osd_render(&s->vol_osd);
    osd_render(&s->bright_osd);
}

int main(void) {
    struct sigaction sa = { .sa_handler = sig_handler, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    ox_init();
    int sh = DisplayHeight(ox_display(), ox_screen());

    OSDState state = {0};
    OSDState *s = &state;

    s->vol_osd.win = ox_window_new(20, (sh - 200) / 2, 30, 200);
    ox_window_set_bg(s->vol_osd.win, "#111111");
    s->vol_osd.widget = ox_widget_new("vol", 0.1);
    ox_widget_set_update(s->vol_osd.widget, vol_update, NULL);
    s->vol_osd.timeout = 3;
    ox_window_hide(s->vol_osd.win);

    s->bright_osd.win = ox_window_new(60, (sh - 200) / 2, 30, 200);
    ox_window_set_bg(s->bright_osd.win, "#111111");
    s->bright_osd.widget = ox_widget_new("bright", 0.1);
    ox_widget_set_update(s->bright_osd.widget, bright_update, NULL);
    s->bright_osd.timeout = 3;
    ox_window_hide(s->bright_osd.win);

    OxMain loop = { .on_event = on_event, .on_timeout = on_timeout, .ctx = s, .timeout_ms = 100 };
    ox_main(&loop);
    ox_cleanup();
    return 0;
}
