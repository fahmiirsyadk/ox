#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "bar.h"
#include "oxbar.h"
#include "plugin.h"
#include "config.h"

static volatile int running = 1;
static volatile int reload = 0;
static volatile int show_osd = 0;

static void sig_handler(int sig)
{
    if (sig == SIGHUP)
        reload = 1;
    else if (sig == SIGUSR1)
        show_osd = 1;
    else
        running = 0;
}

static void bars_destroy(Bar **bars)
{
    Bar *bar = *bars;
    while (bar) {
        Bar *next = bar->next;
        bar_destroy(bar);
        bar = next;
    }
    *bars = NULL;
}

static int bars_create(Display *dpy, int screen, Config *cfg, Bar **bars)
{
    Bar *last = NULL;
    *bars = NULL;
    for (ConfigBlock *bc = cfg->bars; bc; bc = bc->next) {
        Bar *bar = bar_create(dpy, screen, bc);
        if (bar) {
            if (!*bars) *bars = bar;
            else last->next = bar;
            last = bar;
        }
    }
    return *bars != NULL;
}

int main(int argc, char *argv[])
{
    const char *config_path = getenv("OXBAR_CONFIG");
    if (!config_path) {
        config_path = getenv("HOME");
        char path[256];
        snprintf(path, sizeof(path), "%s/.config/oxbar/config", config_path);
        config_path = strdup(path);
    }
    if (argc > 1) config_path = argv[1];

    struct sigaction sa = { .sa_handler = sig_handler, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    plugin_init_all();

    Config *cfg = config_parse(config_path);
    if (!cfg) { fprintf(stderr, "oxbar: failed to parse config\n"); return 1; }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "oxbar: cannot open display\n"); config_free(cfg); return 1; }

    int screen = DefaultScreen(dpy);
    Bar *bars = NULL;

    if (!bars_create(dpy, screen, cfg, &bars)) {
        fprintf(stderr, "oxbar: no bars configured\n");
        XCloseDisplay(dpy); config_free(cfg); return 1;
    }

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
    struct timespec tick;
    clock_gettime(CLOCK_MONOTONIC, &tick);

    while (running) {
        if (reload) {
            reload = 0;
            bars_destroy(&bars);
            config_free(cfg);
            cfg = config_parse(config_path);
            if (!cfg) { fprintf(stderr, "oxbar: config reload failed\n"); break; }
            bars_create(dpy, screen, cfg, &bars);
            fprintf(stderr, "oxbar: config reloaded\n");
        }

        if (show_osd) {
            show_osd = 0;
            for (Bar *b = bars; b; b = b->next) bar_show(b);
        }

        time_t now = time(NULL);
        for (Bar *b = bars; b; b = b->next)
            if (b->type == BAR_OSD && b->shown && now >= b->hide_at)
                bar_hide(b);

        double now_f = tick.tv_sec + tick.tv_nsec / 1e9;
        for (Bar *b = bars; b; b = b->next)
            for (int i = 0; i < b->widget_count; i++) {
                Widget *w = b->widgets[i];
                if (w->interval > 0 && now_f - w->last_update >= w->interval) {
                    widget_update(w);
                    w->last_update = now_f;
                }
            }

        for (Bar *b = bars; b; b = b->next) {
            if (b->type == BAR_OSD && !b->shown) continue;
            bar_render(b);
        }

        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == ButtonPress) {
                for (Bar *b = bars; b; b = b->next) {
                    if (ev.xbutton.window != b->win) continue;
                    if (b->type == BAR_OSD) { bar_show(b); continue; }
                    if (ev.xbutton.button == 4 || ev.xbutton.button == 5)
                        bar_scroll(b, ev.xbutton.x, ev.xbutton.button);
                    else
                        bar_click(b, ev.xbutton.x);
                }
            }
        }

        XFlush(dpy);
        nanosleep(&ts, NULL);
        clock_gettime(CLOCK_MONOTONIC, &tick);
    }

    bars_destroy(&bars);
    XCloseDisplay(dpy);
    config_free(cfg);
    return 0;
}
